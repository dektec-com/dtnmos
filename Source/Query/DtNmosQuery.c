// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosQuery.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The Query API of an NMOS registry (IS-04 v1.3)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosQuery.h"

// The most pages a list follows, which bounds a registry whose paging never ends.
#define DTNMOS_MAX_PAGES 1000

// The size of a page the query asks for.
#define DTNMOS_PAGE_LIMIT 100

struct DtNmosQuery
{
    char* base; // registry URL followed by /x-nmos/query/v1.3/
    DtNmosHttpFunc http;
    void* http_user;
    uint32_t timeout_ms;
    DtNmosLogFunc log;
    void* log_user;
};

struct DtNmosSenderList
{
    DtNmosSenderInfo* senders;
    size_t count;
};

struct DtNmosReceiverList
{
    DtNmosReceiverInfo* receivers;
    size_t count;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderInfo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosSenderInfo_Clear(DtNmosSenderInfo* sender)
{
    if (sender == NULL)
    {
        return;
    }
    DtNmosString_Clear(&sender->Label);
    DtNmosString_Clear(&sender->Description);
    DtNmosString_Clear(&sender->Transport);
    DtNmosString_Clear(&sender->ManifestHref);
    memset(sender, 0, sizeof(*sender));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderInfo_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSenderInfo_Copy(DtNmosSenderInfo* target,
                                   const DtNmosSenderInfo* source)
{
    if (target == NULL || source == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    DtNmosSenderInfo copy;
    memset(&copy, 0, sizeof(copy));
    copy.Id = source->Id;
    copy.FlowId = source->FlowId;
    copy.DeviceId = source->DeviceId;
    copy.Media = source->Media;
    if (DtNmosString_Copy(&copy.Label, &source->Label) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Description, &source->Description) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Transport, &source->Transport) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.ManifestHref, &source->ManifestHref) != DTNMOS_OK)
    {
        DtNmosSenderInfo_Clear(&copy);
        return DTNMOS_E_NO_MEMORY;
    }
    DtNmosSenderInfo_Clear(target);
    *target = copy;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosSenderList_Count(const DtNmosSenderList* list)
{
    return list == NULL ? 0 : list->count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosSenderInfo* DtNmosSenderList_At(const DtNmosSenderList* list, size_t index)
{
    return list == NULL || index >= list->count ? NULL : &list->senders[index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosSenderList_Free(DtNmosSenderList* list)
{
    if (list == NULL)
    {
        return;
    }
    for (size_t i = 0; i < list->count; ++i)
    {
        DtNmosSenderInfo_Clear(&list->senders[i]);
    }
    free(list->senders);
    free(list);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static char* copy_text(const char* text, size_t length)
{
    char* copy = malloc(length + 1);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_Create(const DtNmosQueryConfig* config, DtNmosQuery** query)
{
    if (query == NULL || config == NULL || config->RegistryUrl == NULL ||
        config->RegistryUrl[0] == '\0' || config->Http == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "A query needs the URL of a registry and an HTTP function.");
    }
    *query = NULL;
    if (config->ApiVersion != NULL && strcmp(config->ApiVersion, "v1.3") != 0)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "The Query API %s is not supported; dtnmos speaks v1.3.",
                           config->ApiVersion);
    }
    DtNmosQuery* result = calloc(1, sizeof(*result));
    if (result == NULL)
    {
        return dtnmos_fail_memory();
    }
    size_t length = strlen(config->RegistryUrl);
    while (length > 0 && config->RegistryUrl[length - 1] == '/')
    {
        --length;
    }
    dtnmos_buffer base;
    memset(&base, 0, sizeof(base));
    dtnmos_buffer_append(&base, config->RegistryUrl, length);
    DTNMOS_APPEND_LITERAL(&base, "/x-nmos/query/v1.3/");
    if (base.failed)
    {
        dtnmos_buffer_free(&base);
        free(result);
        return dtnmos_fail_memory();
    }
    result->base = base.data;
    result->http = config->Http;
    result->http_user = config->HttpUser;
    result->timeout_ms = config->TimeoutMs == 0 ? 5000 : config->TimeoutMs;
    result->log = config->Log;
    result->log_user = config->LogUser;
    *query = result;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosQuery_Destroy(DtNmosQuery* query)
{
    if (query == NULL)
    {
        return;
    }
    free(query->base);
    free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- log_message -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void log_message(DtNmosQuery* query, DtNmosLogLevel level, const char* message)
{
    if (query->log != NULL)
    {
        query->log(query->log_user, level, message);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_query_base -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* dtnmos_query_base(const DtNmosQuery* query)
{
    return query->base;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_query_timeout -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t dtnmos_query_timeout(const DtNmosQuery* query)
{
    return query->timeout_ms;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_query_request -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult dtnmos_query_request(DtNmosQuery* query, const char* method, const char* url,
                                  const char* content_type, const char* body,
                                  size_t body_length, DtNmosHttpResponse* response)
{
    DtNmosHttpRequest request;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Method = method;
    request.Url = url;
    request.ContentType = body == NULL ? NULL : content_type;
    request.Body = body;
    request.BodyLength = body == NULL ? 0 : body_length;
    request.TimeoutMs = query->timeout_ms;
    char message[600];
    snprintf(message, sizeof(message), "%s %s", method, url);
    log_message(query, DTNMOS_LOG_DEBUG, message);
    dtnmos_clear_error();
    DtNmosResult result = query->http(query->http_user, &request, response);
    if (result != DTNMOS_OK && DtNmos_GetLastError()[0] == '\0')
    {
        dtnmos_fail(result, "%s %s failed.", method, url);
    }
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Performs a GET of url into response; fails unless the answer is 200, with
// DTNMOS_E_NOT_FOUND for 404.
//
static DtNmosResult get(DtNmosQuery* query, const char* url, DtNmosHttpResponse* response)
{
    DtNmosResult result =
        dtnmos_query_request(query, "GET", url, NULL, NULL, 0, response);
    if (result != DTNMOS_OK)
    {
        return result;
    }
    const int status = DtNmosHttpResponse_Status(response);
    if (status == 404)
    {
        return dtnmos_fail(DTNMOS_E_NOT_FOUND, "The registry has no %s (404).", url);
    }
    if (status != 200)
    {
        return dtnmos_fail(DTNMOS_E_HTTP, "GET %s was answered with %d.", url, status);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get_json -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Performs a GET of url and parses its JSON; the caller frees json and, when next is not
// null, *next, the URL of the next page from the Link header, or null.
//
static DtNmosResult get_json(DtNmosQuery* query, const char* url, dtnmos_json** json,
                             char** next)
{
    *json = NULL;
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    if (response == NULL)
    {
        return dtnmos_fail_memory();
    }
    DtNmosResult result = get(query, url, response);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = DtNmosHttpResponse_Body(response, &length);
        result = dtnmos_json_parse(body, length, json);
        if (result != DTNMOS_OK)
        {
            char reason[512];
            snprintf(reason, sizeof(reason), "%s", DtNmos_GetLastError());
            result = dtnmos_fail(DTNMOS_E_PARSE, "The answer to GET %s is no JSON: %s",
                                 url, reason);
        }
    }
    if (result == DTNMOS_OK && next != NULL)
    {
        *next = NULL;
        // Link: <url>; rel="next", <url>; rel="prev", ...
        const char* link = DtNmosHttpResponse_FindHeader(response, "Link");
        dtnmos_span rest = dtnmos_span_of(link);
        while (rest.data != NULL && rest.length > 0)
        {
            dtnmos_span entry;
            rest = dtnmos_span_split(rest, ',', &entry);
            entry = dtnmos_span_trim(entry);
            if (entry.length < 2 || entry.data[0] != '<')
            {
                continue;
            }
            const char* close = memchr(entry.data, '>', entry.length);
            if (close == NULL)
            {
                continue;
            }
            const dtnmos_span after = {close + 1,
                                       entry.length - (size_t)(close + 1 - entry.data)};
            const char* rel = NULL;
            for (size_t i = 0; i + 10 <= after.length; ++i)
            {
                if (memcmp(after.data + i, "rel=\"next\"", 10) == 0)
                {
                    rel = after.data + i;
                    break;
                }
            }
            if (rel != NULL)
            {
                *next = copy_text(entry.data + 1, (size_t)(close - entry.data - 1));
                if (*next == NULL)
                {
                    result = dtnmos_fail_memory();
                }
                break;
            }
        }
    }
    if (result != DTNMOS_OK && *json != NULL)
    {
        dtnmos_json_free(*json);
        *json = NULL;
    }
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_query_get_json -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult dtnmos_query_get_json(DtNmosQuery* query, const char* url,
                                   dtnmos_json** json)
{
    return get_json(query, url, json, NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Builds base + path, and a query of the paging limit.
//
static char* make_url(const DtNmosQuery* query, const char* path, const char* parameters)
{
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%s%s?%s%spaging.limit=%d", query->base, path,
                         parameters == NULL ? "" : parameters,
                         parameters == NULL ? "" : "&", DTNMOS_PAGE_LIMIT);
    if (url.failed)
    {
        dtnmos_buffer_free(&url);
        return NULL;
    }
    return url.data;
}

// A growing array of JSON values of one or more pages, which it owns.
typedef struct pages
{
    dtnmos_json** values;
    size_t count;
    size_t capacity;
} pages;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_pages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void free_pages(pages* p)
{
    for (size_t i = 0; i < p->count; ++i)
    {
        dtnmos_json_free(p->values[i]);
    }
    free(p->values);
    memset(p, 0, sizeof(*p));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get_pages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fetches every page of the list at url, each an array, into p.
//
static DtNmosResult get_pages(DtNmosQuery* query, char* url, pages* p)
{
    for (int page = 0; url != NULL; ++page)
    {
        if (page == DTNMOS_MAX_PAGES)
        {
            free(url);
            return dtnmos_fail(DTNMOS_E_HTTP, "The registry pages on beyond %d pages.",
                               DTNMOS_MAX_PAGES);
        }
        dtnmos_json* json = NULL;
        char* next = NULL;
        DtNmosResult result = get_json(query, url, &json, &next);
        if (result == DTNMOS_OK && json->type != DTNMOS_JSON_ARRAY)
        {
            dtnmos_json_free(json);
            result =
                dtnmos_fail(DTNMOS_E_PARSE, "The answer to GET %s is no array.", url);
        }
        if (result != DTNMOS_OK)
        {
            free(url);
            free(next);
            return result;
        }
        // An empty page ends the list; a page that points back at itself does too.
        const int end = json->count == 0 || (next != NULL && strcmp(next, url) == 0);
        if (p->count == p->capacity)
        {
            const size_t capacity = p->capacity == 0 ? 4 : p->capacity * 2;
            dtnmos_json** values = realloc(p->values, capacity * sizeof(*values));
            if (values == NULL)
            {
                dtnmos_json_free(json);
                free(url);
                free(next);
                return dtnmos_fail_memory();
            }
            p->values = values;
            p->capacity = capacity;
        }
        p->values[p->count++] = json;
        free(url);
        url = end ? NULL : next;
        if (end)
        {
            free(next);
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- media_of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the media of a format of IS-04 and a media type, which may be null.
//
static DtNmosMedia media_of(const char* format, const char* media_type)
{
    if (format == NULL)
    {
        return DTNMOS_MEDIA_OTHER;
    }
    if (strcmp(format, "urn:x-nmos:format:video") == 0)
    {
        return media_type != NULL && strcmp(media_type, "video/raw") != 0
                   ? DTNMOS_MEDIA_COMPRESSED_VIDEO
                   : DTNMOS_MEDIA_VIDEO;
    }
    if (strcmp(format, "urn:x-nmos:format:audio") == 0)
    {
        return DTNMOS_MEDIA_AUDIO;
    }
    if (strcmp(format, "urn:x-nmos:format:data") == 0 && media_type != NULL &&
        strcmp(media_type, "video/smpte291") == 0)
    {
        return DTNMOS_MEDIA_ANC;
    }
    return DTNMOS_MEDIA_OTHER;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- media_of_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the media of a flow resource of IS-04, from its format and media type.
//
static DtNmosMedia media_of_flow(const dtnmos_json* flow)
{
    return media_of(dtnmos_json_member_text(flow, "format"),
                    dtnmos_json_member_text(flow, "media_type"));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_id -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void copy_id(DtNmosId* id, const char* text)
{
    memset(id, 0, sizeof(*id));
    if (text != NULL && strlen(text) < sizeof(id->Text))
    {
        strcpy(id->Text, text);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills sender from a sender resource of IS-04; its media stays for the caller.
//
static DtNmosResult read_sender(const dtnmos_json* resource, DtNmosSenderInfo* sender)
{
    memset(sender, 0, sizeof(*sender));
    sender->Media = DTNMOS_MEDIA_OTHER;
    copy_id(&sender->Id, dtnmos_json_member_text(resource, "id"));
    copy_id(&sender->FlowId, dtnmos_json_member_text(resource, "flow_id"));
    copy_id(&sender->DeviceId, dtnmos_json_member_text(resource, "device_id"));
    if (DtNmosString_SetText(&sender->Label,
                             dtnmos_json_member_text(resource, "label")) != DTNMOS_OK ||
        DtNmosString_SetText(&sender->Description,
                             dtnmos_json_member_text(resource, "description")) !=
            DTNMOS_OK ||
        DtNmosString_SetText(&sender->Transport,
                             dtnmos_json_member_text(resource, "transport")) !=
            DTNMOS_OK ||
        DtNmosString_SetText(&sender->ManifestHref,
                             dtnmos_json_member_text(resource, "manifest_href")) !=
            DTNMOS_OK)
    {
        DtNmosSenderInfo_Clear(sender);
        return DTNMOS_E_NO_MEMORY;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Senders -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Senders(DtNmosQuery* query, DtNmosSenderList** list)
{
    if (query == NULL || list == NULL)
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Senders() needs a query and a place for the list.");
    }
    *list = NULL;
    pages senders;
    pages flows;
    memset(&senders, 0, sizeof(senders));
    memset(&flows, 0, sizeof(flows));
    char* url = make_url(query, "senders", NULL);
    if (url == NULL)
    {
        return dtnmos_fail_memory();
    }
    DtNmosResult result = get_pages(query, url, &senders);
    if (result == DTNMOS_OK)
    {
        url = make_url(query, "flows", NULL);
        result = url == NULL ? dtnmos_fail_memory() : get_pages(query, url, &flows);
    }
    DtNmosSenderList* built = NULL;
    if (result == DTNMOS_OK)
    {
        built = calloc(1, sizeof(*built));
        size_t total = 0;
        for (size_t p = 0; p < senders.count; ++p)
        {
            total += senders.values[p]->count;
        }
        if (built == NULL || (total > 0 && (built->senders = calloc(
                                                total, sizeof(*built->senders))) == NULL))
        {
            result = dtnmos_fail_memory();
        }
    }
    for (size_t p = 0; result == DTNMOS_OK && p < senders.count; ++p)
    {
        for (size_t i = 0; result == DTNMOS_OK && i < senders.values[p]->count; ++i)
        {
            const dtnmos_json* resource = &senders.values[p]->items[i];
            DtNmosSenderInfo* sender = &built->senders[built->count];
            if (read_sender(resource, sender) != DTNMOS_OK)
            {
                result = dtnmos_fail_memory();
                break;
            }
            ++built->count;
            // The media comes from the flow of the sender, which the list of flows holds.
            for (size_t f = 0; f < flows.count; ++f)
            {
                for (size_t j = 0; j < flows.values[f]->count; ++j)
                {
                    const char* id =
                        dtnmos_json_member_text(&flows.values[f]->items[j], "id");
                    if (id != NULL && strcmp(id, sender->FlowId.Text) == 0)
                    {
                        sender->Media = media_of_flow(&flows.values[f]->items[j]);
                    }
                }
            }
        }
    }
    free_pages(&senders);
    free_pages(&flows);
    if (result != DTNMOS_OK)
    {
        DtNmosSenderList_Free(built);
        return result;
    }
    *list = built;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_uuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether text has the form of a UUID.
//
static int is_uuid(const char* text)
{
    if (strlen(text) != 36)
    {
        return 0;
    }
    for (size_t i = 0; i < 36; ++i)
    {
        const char c = text[i];
        const int dash = i == 8 || i == 13 || i == 18 || i == 23;
        const int hex =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (dash ? c != '-' : !hex)
        {
            return 0;
        }
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- append_encoded -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Appends text to a URL, encoded as RFC 3986 asks of a query value.
//
static void append_encoded(dtnmos_buffer* url, const char* text)
{
    for (const unsigned char* c = (const unsigned char*)text; *c != '\0'; ++c)
    {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
            (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.' || *c == '~')
        {
            dtnmos_buffer_append(url, (const char*)c, 1);
        }
        else
        {
            dtnmos_buffer_printf(url, "%%%02X", (unsigned)*c);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_media -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the media of sender from its flow, which it fetches; a flow that cannot be fetched
// leaves DTNMOS_MEDIA_OTHER.
//
static void read_media(DtNmosQuery* query, DtNmosSenderInfo* sender)
{
    if (sender->FlowId.Text[0] == '\0')
    {
        return;
    }
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%sflows/%s", query->base, sender->FlowId.Text);
    dtnmos_json* flow = NULL;
    if (!url.failed && get_json(query, url.data, &flow, NULL) == DTNMOS_OK)
    {
        sender->Media = media_of_flow(flow);
        dtnmos_json_free(flow);
    }
    dtnmos_buffer_free(&url);
}

// A kind of resource that a search by ID or label finds: its path in the Query API, what
// a message calls one, and how one is read into what the caller gave.
typedef struct resource_kind
{
    const char* path; // e.g. "senders"
    const char* noun; // e.g. "sender"
    DtNmosResult (*read)(const dtnmos_json* resource, void* info);
} resource_kind;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- find_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds the resource of kind whose ID is id_or_label when it is a UUID, or else whose
// label it is, and reads it into info.
//
static DtNmosResult find_resource(DtNmosQuery* query, const resource_kind* kind,
                                  const char* id_or_label, void* info)
{
    if (is_uuid(id_or_label))
    {
        dtnmos_buffer url;
        memset(&url, 0, sizeof(url));
        dtnmos_buffer_printf(&url, "%s%s/%s", query->base, kind->path, id_or_label);
        if (url.failed)
        {
            dtnmos_buffer_free(&url);
            return dtnmos_fail_memory();
        }
        dtnmos_json* resource = NULL;
        DtNmosResult result = get_json(query, url.data, &resource, NULL);
        dtnmos_buffer_free(&url);
        if (result == DTNMOS_E_NOT_FOUND)
        {
            return dtnmos_fail(DTNMOS_E_NOT_FOUND, "The registry has no %s %s.",
                               kind->noun, id_or_label);
        }
        if (result != DTNMOS_OK)
        {
            return result;
        }
        result = kind->read(resource, info);
        dtnmos_json_free(resource);
        return result == DTNMOS_OK ? DTNMOS_OK : dtnmos_fail_memory();
    }

    // A label is matched by the registry, and again here, as the Query API compares
    // values of a basic query in ways of its own.
    dtnmos_buffer parameters;
    memset(&parameters, 0, sizeof(parameters));
    DTNMOS_APPEND_LITERAL(&parameters, "label=");
    append_encoded(&parameters, id_or_label);
    char* url = parameters.failed ? NULL : make_url(query, kind->path, parameters.data);
    dtnmos_buffer_free(&parameters);
    if (url == NULL)
    {
        return dtnmos_fail_memory();
    }
    pages found;
    memset(&found, 0, sizeof(found));
    DtNmosResult result = get_pages(query, url, &found);
    const dtnmos_json* match = NULL;
    size_t matches = 0;
    dtnmos_buffer ids;
    memset(&ids, 0, sizeof(ids));
    for (size_t p = 0; result == DTNMOS_OK && p < found.count; ++p)
    {
        for (size_t i = 0; i < found.values[p]->count; ++i)
        {
            const dtnmos_json* resource = &found.values[p]->items[i];
            const char* label = dtnmos_json_member_text(resource, "label");
            if (label != NULL && strcmp(label, id_or_label) == 0)
            {
                const char* id = dtnmos_json_member_text(resource, "id");
                dtnmos_buffer_printf(&ids, "%s%s", matches == 0 ? "" : ", ",
                                     id == NULL ? "?" : id);
                match = resource;
                ++matches;
            }
        }
    }
    if (result == DTNMOS_OK && matches == 0)
    {
        result = dtnmos_fail(DTNMOS_E_NOT_FOUND, "The registry has no %s labelled '%s'.",
                             kind->noun, id_or_label);
    }
    else if (result == DTNMOS_OK && matches > 1)
    {
        result = dtnmos_fail(DTNMOS_E_AMBIGUOUS,
                             "%zu %ss of the registry are labelled '%s': %s.", matches,
                             kind->noun, id_or_label, ids.data == NULL ? "" : ids.data);
    }
    else if (result == DTNMOS_OK && kind->read(match, info) != DTNMOS_OK)
    {
        result = dtnmos_fail_memory();
    }
    dtnmos_buffer_free(&ids);
    free_pages(&found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_sender_info -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_sender_info(const dtnmos_json* resource, void* info)
{
    return read_sender(resource, info);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_FindSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_FindSender(DtNmosQuery* query, const char* id_or_label,
                                    DtNmosSenderInfo* sender)
{
    if (query == NULL || id_or_label == NULL || id_or_label[0] == '\0' || sender == NULL)
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_FindSender() needs an ID or a label and a sender.");
    }
    DtNmosSenderInfo_Clear(sender);
    static const resource_kind senders = {"senders", "sender", read_sender_info};
    const DtNmosResult result = find_resource(query, &senders, id_or_label, sender);
    if (result == DTNMOS_OK)
    {
        read_media(query, sender);
    }
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverInfo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosReceiverInfo_Clear(DtNmosReceiverInfo* receiver)
{
    if (receiver == NULL)
    {
        return;
    }
    DtNmosString_Clear(&receiver->Label);
    DtNmosString_Clear(&receiver->Description);
    DtNmosString_Clear(&receiver->Transport);
    memset(receiver, 0, sizeof(*receiver));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverInfo_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosReceiverInfo_Copy(DtNmosReceiverInfo* target,
                                     const DtNmosReceiverInfo* source)
{
    if (target == NULL || source == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    DtNmosReceiverInfo copy;
    memset(&copy, 0, sizeof(copy));
    copy.Id = source->Id;
    copy.DeviceId = source->DeviceId;
    copy.Media = source->Media;
    copy.SenderId = source->SenderId;
    copy.Active = source->Active;
    if (DtNmosString_Copy(&copy.Label, &source->Label) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Description, &source->Description) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Transport, &source->Transport) != DTNMOS_OK)
    {
        DtNmosReceiverInfo_Clear(&copy);
        return DTNMOS_E_NO_MEMORY;
    }
    DtNmosReceiverInfo_Clear(target);
    *target = copy;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosReceiverList_Count(const DtNmosReceiverList* list)
{
    return list == NULL ? 0 : list->count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosReceiverInfo* DtNmosReceiverList_At(const DtNmosReceiverList* list,
                                                size_t index)
{
    return list == NULL || index >= list->count ? NULL : &list->receivers[index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosReceiverList_Free(DtNmosReceiverList* list)
{
    if (list == NULL)
    {
        return;
    }
    for (size_t i = 0; i < list->count; ++i)
    {
        DtNmosReceiverInfo_Clear(&list->receivers[i]);
    }
    free(list->receivers);
    free(list);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills receiver from a receiver resource of IS-04: its media from its format and the
// first media type of its caps, and its subscription.
//
static DtNmosResult read_receiver(const dtnmos_json* resource, void* info)
{
    DtNmosReceiverInfo* receiver = info;
    memset(receiver, 0, sizeof(*receiver));
    copy_id(&receiver->Id, dtnmos_json_member_text(resource, "id"));
    copy_id(&receiver->DeviceId, dtnmos_json_member_text(resource, "device_id"));
    const dtnmos_json* types =
        dtnmos_json_member(dtnmos_json_member(resource, "caps"), "media_types");
    const char* media_type =
        types != NULL && types->type == DTNMOS_JSON_ARRAY && types->count > 0
            ? dtnmos_json_text(&types->items[0])
            : NULL;
    receiver->Media = media_of(dtnmos_json_member_text(resource, "format"), media_type);
    const dtnmos_json* subscription = dtnmos_json_member(resource, "subscription");
    copy_id(&receiver->SenderId, dtnmos_json_member_text(subscription, "sender_id"));
    const dtnmos_json* active = dtnmos_json_member(subscription, "active");
    receiver->Active = active != NULL && active->type == DTNMOS_JSON_TRUE;
    if (DtNmosString_SetText(&receiver->Label,
                             dtnmos_json_member_text(resource, "label")) != DTNMOS_OK ||
        DtNmosString_SetText(&receiver->Description,
                             dtnmos_json_member_text(resource, "description")) !=
            DTNMOS_OK ||
        DtNmosString_SetText(&receiver->Transport,
                             dtnmos_json_member_text(resource, "transport")) != DTNMOS_OK)
    {
        DtNmosReceiverInfo_Clear(receiver);
        return DTNMOS_E_NO_MEMORY;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Receivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Receivers(DtNmosQuery* query, DtNmosReceiverList** list)
{
    if (query == NULL || list == NULL)
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Receivers() needs a query and a place for the list.");
    }
    *list = NULL;
    pages receivers;
    memset(&receivers, 0, sizeof(receivers));
    char* url = make_url(query, "receivers", NULL);
    if (url == NULL)
    {
        return dtnmos_fail_memory();
    }
    DtNmosResult result = get_pages(query, url, &receivers);
    DtNmosReceiverList* built = NULL;
    if (result == DTNMOS_OK)
    {
        built = calloc(1, sizeof(*built));
        size_t total = 0;
        for (size_t p = 0; p < receivers.count; ++p)
        {
            total += receivers.values[p]->count;
        }
        if (built == NULL ||
            (total > 0 &&
             (built->receivers = calloc(total, sizeof(*built->receivers))) == NULL))
        {
            result = dtnmos_fail_memory();
        }
    }
    for (size_t p = 0; result == DTNMOS_OK && p < receivers.count; ++p)
    {
        for (size_t i = 0; i < receivers.values[p]->count; ++i)
        {
            if (read_receiver(&receivers.values[p]->items[i],
                              &built->receivers[built->count]) != DTNMOS_OK)
            {
                result = dtnmos_fail_memory();
                break;
            }
            ++built->count;
        }
    }
    free_pages(&receivers);
    if (result != DTNMOS_OK)
    {
        DtNmosReceiverList_Free(built);
        return result;
    }
    *list = built;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_FindReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_FindReceiver(DtNmosQuery* query, const char* id_or_label,
                                      DtNmosReceiverInfo* receiver)
{
    if (query == NULL || id_or_label == NULL || id_or_label[0] == '\0' ||
        receiver == NULL)
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_FindReceiver() needs an ID or a label and a receiver.");
    }
    DtNmosReceiverInfo_Clear(receiver);
    static const resource_kind receivers = {"receivers", "receiver", read_receiver};
    return find_resource(query, &receivers, id_or_label, receiver);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_SenderManifest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_SenderManifest(DtNmosQuery* query,
                                        const DtNmosSenderInfo* sender,
                                        DtNmosString* text)
{
    if (query == NULL || sender == NULL || text == NULL)
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_SenderManifest() needs a query, a sender and a text.");
    }
    if (DtNmosString_Length(&sender->ManifestHref) == 0)
    {
        return dtnmos_fail(DTNMOS_E_NOT_FOUND,
                           "Sender %s ('%s') has no manifest_href, so it gives no SDP.",
                           sender->Id.Text, DtNmosString_Get(&sender->Label));
    }
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    if (response == NULL)
    {
        return dtnmos_fail_memory();
    }
    DtNmosResult result = get(query, DtNmosString_Get(&sender->ManifestHref), response);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = DtNmosHttpResponse_Body(response, &length);
        if (DtNmosString_Set(text, body, length) != DTNMOS_OK)
        {
            result = dtnmos_fail_memory();
        }
    }
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_SenderSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_SenderSdp(DtNmosQuery* query, const DtNmosSenderInfo* sender,
                                   DtNmosSdp** sdp)
{
    if (sdp == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosQuery_SenderSdp() needs a place for the SDP.");
    }
    *sdp = NULL;
    DtNmosString text = {0};
    DtNmosResult result = DtNmosQuery_SenderManifest(query, sender, &text);
    if (result == DTNMOS_OK)
    {
        result =
            DtNmosSdp_Parse(DtNmosString_Get(&text), DtNmosString_Length(&text), sdp);
    }
    DtNmosString_Clear(&text);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderInfo_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosSenderInfo_Parse(const char* json, size_t length,
                                    DtNmosSenderInfo* sender)
{
    if (json == NULL || sender == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosSenderInfo_Parse() needs JSON and a sender.");
    }
    DtNmosSenderInfo_Clear(sender);
    dtnmos_json* resource = NULL;
    if (dtnmos_json_parse(json, length, &resource) != DTNMOS_OK ||
        resource->type != DTNMOS_JSON_OBJECT)
    {
        dtnmos_json_free(resource);
        return dtnmos_fail(DTNMOS_E_PARSE, "The JSON of a sender is no object.");
    }
    const DtNmosResult result = read_sender(resource, sender);
    dtnmos_json_free(resource);
    return result == DTNMOS_OK ? DTNMOS_OK : dtnmos_fail_memory();
}
