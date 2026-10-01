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

// A query is allocated empty and closed; DtNmosQuery_Open() fills it in, and
// DtNmosQuery_Close() empties it again.
struct DtNmosQuery
{
    int open;
    char* base; // registry URL followed by /x-nmos/query/v1.3/
    DtNmosHttpFunc http;
    void* http_user;
    uint32_t timeout_ms;
    DtNmosLogFunc log;
    void* log_user;
};

// A list owns its strings in its store; a list of one, as finding a sender returns, is
// made with room for that one.
struct DtNmosSenderList
{
    DtNmosSenderInfo* senders;
    size_t count;
    NmosStore store;
};

struct DtNmosReceiverList
{
    DtNmosReceiverInfo* receivers;
    size_t count;
    NmosStore store;
};

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
    NmosStore_Free(&list->store);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_Open(DtNmosQuery* query, const DtNmosQueryConfig* config)
{
    if (query == NULL || config == NULL || config->RegistryUrl == NULL ||
        config->RegistryUrl[0] == '\0' || config->Http == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "A query needs the URL of a registry and an HTTP function.");
    }
    if (query->open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "The query is open already; close it first.");
    }
    const DtNmosResult sized =
        DTNMOS_CHECK_SIZE(config, DtNmosQueryConfig, sizeof(DtNmosQueryConfig));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    if (config->ApiVersion != NULL && strcmp(config->ApiVersion, "v1.3") != 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The Query API %s is not supported; dtnmos speaks v1.3.",
                              config->ApiVersion);
    }
    DtNmosQuery* result = query;
    size_t length = strlen(config->RegistryUrl);
    while (length > 0 && config->RegistryUrl[length - 1] == '/')
    {
        --length;
    }
    NmosBuffer base;
    memset(&base, 0, sizeof(base));
    NmosBuffer_Append(&base, config->RegistryUrl, length);
    DTNMOS_APPEND_LITERAL(&base, "/x-nmos/query/v1.3/");
    if (base.failed)
    {
        NmosBuffer_Free(&base);
        return NmosError_FailMemory();
    }
    result->base = base.data;
    result->http = config->Http;
    result->http_user = config->HttpUser;
    result->timeout_ms = config->TimeoutMs == 0 ? 5000 : config->TimeoutMs;
    result->log = config->Log;
    result->log_user = config->LogUser;
    result->open = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosQuery* DtNmosQuery_Alloc(void)
{
    return calloc(1, sizeof(DtNmosQuery));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Close(DtNmosQuery* query)
{
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_Close");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    free(query->base);
    memset(query, 0, sizeof(*query));
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosQuery_Free(DtNmosQuery* query)
{
    if (query == NULL)
    {
        return;
    }
    if (query->open)
    {
        DtNmosQuery_Close(query);
    }
    free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosQuery_Freep(DtNmosQuery** query)
{
    if (query != NULL)
    {
        DtNmosQuery_Free(*query);
        *query = NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosQuery_CheckOpen(const DtNmosQuery* query, const char* function)
{
    if (query == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "%s() needs a query.", function);
    }
    if (!query->open)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "%s() needs an open query.", function);
    }
    return DTNMOS_OK;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Base -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* NmosQuery_Base(const DtNmosQuery* query)
{
    return query->base;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Timeout -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t NmosQuery_Timeout(const DtNmosQuery* query)
{
    return query->timeout_ms;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Request -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosQuery_Request(DtNmosQuery* query, const char* method, const char* url,
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
    NmosError_Clear();
    DtNmosResult result = query->http(query->http_user, &request, response);
    if (result != DTNMOS_OK && DtNmos_GetLastError()[0] == '\0')
    {
        NmosError_Fail(result, "%s %s failed.", method, url);
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
    DtNmosResult result = NmosQuery_Request(query, "GET", url, NULL, NULL, 0, response);
    if (result != DTNMOS_OK)
    {
        return result;
    }
    const int status = DtNmosHttpResponse_Status(response);
    if (status == 404)
    {
        return NmosError_Fail(DTNMOS_E_NOT_FOUND, "The registry has no %s (404).", url);
    }
    if (status != 200)
    {
        return NmosError_Fail(DTNMOS_E_HTTP, "GET %s was answered with %d.", url, status);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get_json -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Performs a GET of url and parses its JSON; the caller frees json and, when next is not
// null, *next, the URL of the next page from the Link header, or null.
//
static DtNmosResult get_json(DtNmosQuery* query, const char* url, NmosJson** json,
                             char** next)
{
    *json = NULL;
    DtNmosHttpResponse* response = DtNmosHttpResponse_Alloc();
    if (response == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult result = get(query, url, response);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = DtNmosHttpResponse_Body(response, &length);
        result = NmosJson_Parse(body, length, json);
        if (result != DTNMOS_OK)
        {
            char reason[512];
            snprintf(reason, sizeof(reason), "%s", DtNmos_GetLastError());
            result = NmosError_Fail(DTNMOS_E_PARSE, "The answer to GET %s is no JSON: %s",
                                    url, reason);
        }
    }
    if (result == DTNMOS_OK && next != NULL)
    {
        *next = NULL;
        // Link: <url>; rel="next", <url>; rel="prev", ...
        const char* link = DtNmosHttpResponse_FindHeader(response, "Link");
        NmosSpan rest = NmosSpan_Of(link);
        while (rest.data != NULL && rest.length > 0)
        {
            NmosSpan entry;
            rest = NmosSpan_Split(rest, ',', &entry);
            entry = NmosSpan_Trim(entry);
            if (entry.length < 2 || entry.data[0] != '<')
            {
                continue;
            }
            const char* close = memchr(entry.data, '>', entry.length);
            if (close == NULL)
            {
                continue;
            }
            const NmosSpan after = {close + 1,
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
                    result = NmosError_FailMemory();
                }
                break;
            }
        }
    }
    if (result != DTNMOS_OK && *json != NULL)
    {
        NmosJson_Free(*json);
        *json = NULL;
    }
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_GetJson -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosQuery_GetJson(DtNmosQuery* query, const char* url, NmosJson** json)
{
    return get_json(query, url, json, NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Builds base + path, and a query of the paging limit.
//
static char* make_url(const DtNmosQuery* query, const char* path, const char* parameters)
{
    NmosBuffer url;
    memset(&url, 0, sizeof(url));
    NmosBuffer_Printf(&url, "%s%s?%s%spaging.limit=%d", query->base, path,
                      parameters == NULL ? "" : parameters, parameters == NULL ? "" : "&",
                      DTNMOS_PAGE_LIMIT);
    if (url.failed)
    {
        NmosBuffer_Free(&url);
        return NULL;
    }
    return url.data;
}

// A growing array of JSON values of one or more pages, which it owns.
typedef struct NmosPages
{
    NmosJson** values;
    size_t count;
    size_t capacity;
} NmosPages;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_pages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void free_pages(NmosPages* p)
{
    for (size_t i = 0; i < p->count; ++i)
    {
        NmosJson_Free(p->values[i]);
    }
    free(p->values);
    memset(p, 0, sizeof(*p));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get_pages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fetches every page of the list at url, each an array, into p.
//
static DtNmosResult get_pages(DtNmosQuery* query, char* url, NmosPages* p)
{
    for (int page = 0; url != NULL; ++page)
    {
        if (page == DTNMOS_MAX_PAGES)
        {
            free(url);
            return NmosError_Fail(DTNMOS_E_HTTP, "The registry pages on beyond %d pages.",
                                  DTNMOS_MAX_PAGES);
        }
        NmosJson* json = NULL;
        char* next = NULL;
        DtNmosResult result = get_json(query, url, &json, &next);
        if (result == DTNMOS_OK && json->type != DTNMOS_JSON_ARRAY)
        {
            NmosJson_Free(json);
            result =
                NmosError_Fail(DTNMOS_E_PARSE, "The answer to GET %s is no array.", url);
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
            NmosJson** values = realloc(p->values, capacity * sizeof(*values));
            if (values == NULL)
            {
                NmosJson_Free(json);
                free(url);
                free(next);
                return NmosError_FailMemory();
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
static DtNmosMedia media_of_flow(const NmosJson* flow)
{
    return media_of(NmosJson_MemberText(flow, "format"),
                    NmosJson_MemberText(flow, "media_type"));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- store_member -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns a copy of the text of member key of resource, owned by store, or "" when it
// has none; sets *failed when the memory ran out.
//
static const char* store_member(NmosStore* store, const NmosJson* resource,
                                const char* key, int* failed)
{
    const char* text = NmosJson_MemberText(resource, key);
    if (text == NULL || text[0] == '\0')
    {
        return "";
    }
    const char* copy = NmosStore_Text(store, text, strlen(text));
    if (copy == NULL)
    {
        *failed = 1;
        return "";
    }
    return copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills sender from a sender resource of IS-04, its strings owned by store; its media
// stays for the caller.
//
static DtNmosResult read_sender(const NmosJson* resource, NmosStore* store, void* info)
{
    DtNmosSenderInfo* sender = info;
    memset(sender, 0, sizeof(*sender));
    sender->Media = DTNMOS_MEDIA_OTHER;
    copy_id(&sender->Id, NmosJson_MemberText(resource, "id"));
    copy_id(&sender->FlowId, NmosJson_MemberText(resource, "flow_id"));
    copy_id(&sender->DeviceId, NmosJson_MemberText(resource, "device_id"));
    int failed = 0;
    sender->Label = store_member(store, resource, "label", &failed);
    sender->Description = store_member(store, resource, "description", &failed);
    sender->Transport = store_member(store, resource, "transport", &failed);
    sender->ManifestHref = store_member(store, resource, "manifest_href", &failed);
    return failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- new_sender_list -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns an empty list with room for capacity senders, or null when the memory ran out.
//
static DtNmosSenderList* new_sender_list(size_t capacity)
{
    DtNmosSenderList* list = calloc(1, sizeof(*list));
    if (list != NULL && capacity > 0 &&
        (list->senders = calloc(capacity, sizeof(*list->senders))) == NULL)
    {
        free(list);
        return NULL;
    }
    return list;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Senders -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Senders(DtNmosQuery* query, DtNmosSenderList** list)
{
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_Senders");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || list == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Senders() needs a query and a place for the list.");
    }
    *list = NULL;
    NmosPages senders;
    NmosPages flows;
    memset(&senders, 0, sizeof(senders));
    memset(&flows, 0, sizeof(flows));
    char* url = make_url(query, "senders", NULL);
    if (url == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult result = get_pages(query, url, &senders);
    if (result == DTNMOS_OK)
    {
        url = make_url(query, "flows", NULL);
        result = url == NULL ? NmosError_FailMemory() : get_pages(query, url, &flows);
    }
    DtNmosSenderList* built = NULL;
    if (result == DTNMOS_OK)
    {
        size_t total = 0;
        for (size_t p = 0; p < senders.count; ++p)
        {
            total += senders.values[p]->count;
        }
        built = new_sender_list(total);
        if (built == NULL)
        {
            result = NmosError_FailMemory();
        }
    }
    for (size_t p = 0; result == DTNMOS_OK && p < senders.count; ++p)
    {
        for (size_t i = 0; result == DTNMOS_OK && i < senders.values[p]->count; ++i)
        {
            const NmosJson* resource = &senders.values[p]->items[i];
            DtNmosSenderInfo* sender = &built->senders[built->count];
            if (read_sender(resource, &built->store, sender) != DTNMOS_OK)
            {
                result = NmosError_FailMemory();
                break;
            }
            ++built->count;
            // The media comes from the flow of the sender, which the list of flows holds.
            for (size_t f = 0; f < flows.count; ++f)
            {
                for (size_t j = 0; j < flows.values[f]->count; ++j)
                {
                    const char* id =
                        NmosJson_MemberText(&flows.values[f]->items[j], "id");
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
static void append_encoded(NmosBuffer* url, const char* text)
{
    for (const unsigned char* c = (const unsigned char*)text; *c != '\0'; ++c)
    {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
            (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.' || *c == '~')
        {
            NmosBuffer_Append(url, (const char*)c, 1);
        }
        else
        {
            NmosBuffer_Printf(url, "%%%02X", (unsigned)*c);
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
    NmosBuffer url;
    memset(&url, 0, sizeof(url));
    NmosBuffer_Printf(&url, "%sflows/%s", query->base, sender->FlowId.Text);
    NmosJson* flow = NULL;
    if (!url.failed && get_json(query, url.data, &flow, NULL) == DTNMOS_OK)
    {
        sender->Media = media_of_flow(flow);
        NmosJson_Free(flow);
    }
    NmosBuffer_Free(&url);
}

// A kind of resource that a search by ID or label finds: its path in the Query API, what
// a message calls one, and how one is read into what the caller gave, its strings owned
// by a store.
typedef struct NmosResourceKind
{
    const char* path; // e.g. "senders"
    const char* noun; // e.g. "sender"
    DtNmosResult (*read)(const NmosJson* resource, NmosStore* store, void* info);
} NmosResourceKind;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- find_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds the resource of kind whose ID is id_or_label when it is a UUID, or else whose
// label it is, and reads it into info, its strings owned by store.
//
static DtNmosResult find_resource(DtNmosQuery* query, const NmosResourceKind* kind,
                                  const char* id_or_label, NmosStore* store, void* info)
{
    if (is_uuid(id_or_label))
    {
        NmosBuffer url;
        memset(&url, 0, sizeof(url));
        NmosBuffer_Printf(&url, "%s%s/%s", query->base, kind->path, id_or_label);
        if (url.failed)
        {
            NmosBuffer_Free(&url);
            return NmosError_FailMemory();
        }
        NmosJson* resource = NULL;
        DtNmosResult result = get_json(query, url.data, &resource, NULL);
        NmosBuffer_Free(&url);
        if (result == DTNMOS_E_NOT_FOUND)
        {
            return NmosError_Fail(DTNMOS_E_NOT_FOUND, "The registry has no %s %s.",
                                  kind->noun, id_or_label);
        }
        if (result != DTNMOS_OK)
        {
            return result;
        }
        result = kind->read(resource, store, info);
        NmosJson_Free(resource);
        return result == DTNMOS_OK ? DTNMOS_OK : NmosError_FailMemory();
    }

    // A label is matched by the registry, and again here, as the Query API compares
    // values of a basic query in ways of its own.
    NmosBuffer parameters;
    memset(&parameters, 0, sizeof(parameters));
    DTNMOS_APPEND_LITERAL(&parameters, "label=");
    append_encoded(&parameters, id_or_label);
    char* url = parameters.failed ? NULL : make_url(query, kind->path, parameters.data);
    NmosBuffer_Free(&parameters);
    if (url == NULL)
    {
        return NmosError_FailMemory();
    }
    NmosPages found;
    memset(&found, 0, sizeof(found));
    DtNmosResult result = get_pages(query, url, &found);
    const NmosJson* match = NULL;
    size_t matches = 0;
    NmosBuffer ids;
    memset(&ids, 0, sizeof(ids));
    for (size_t p = 0; result == DTNMOS_OK && p < found.count; ++p)
    {
        for (size_t i = 0; i < found.values[p]->count; ++i)
        {
            const NmosJson* resource = &found.values[p]->items[i];
            const char* label = NmosJson_MemberText(resource, "label");
            if (label != NULL && strcmp(label, id_or_label) == 0)
            {
                const char* id = NmosJson_MemberText(resource, "id");
                NmosBuffer_Printf(&ids, "%s%s", matches == 0 ? "" : ", ",
                                  id == NULL ? "?" : id);
                match = resource;
                ++matches;
            }
        }
    }
    if (result == DTNMOS_OK && matches == 0)
    {
        result =
            NmosError_Fail(DTNMOS_E_NOT_FOUND, "The registry has no %s labelled '%s'.",
                           kind->noun, id_or_label);
    }
    else if (result == DTNMOS_OK && matches > 1)
    {
        result = NmosError_Fail(
            DTNMOS_E_AMBIGUOUS, "%zu %ss of the registry are labelled '%s': %s.", matches,
            kind->noun, id_or_label, ids.data == NULL ? "" : ids.data);
    }
    else if (result == DTNMOS_OK && kind->read(match, store, info) != DTNMOS_OK)
    {
        result = NmosError_FailMemory();
    }
    NmosBuffer_Free(&ids);
    free_pages(&found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_FindSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_FindSender(DtNmosQuery* query, const char* id_or_label,
                                    DtNmosSenderList** found)
{
    if (found != NULL)
    {
        *found = NULL;
    }
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_FindSender");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || id_or_label == NULL || id_or_label[0] == '\0' || found == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_FindSender() needs an ID or a label and a place "
            "for the sender.");
    }
    DtNmosSenderList* list = new_sender_list(1);
    if (list == NULL)
    {
        return NmosError_FailMemory();
    }
    static const NmosResourceKind senders = {"senders", "sender", read_sender};
    const DtNmosResult result =
        find_resource(query, &senders, id_or_label, &list->store, &list->senders[0]);
    if (result != DTNMOS_OK)
    {
        DtNmosSenderList_Free(list);
        return result;
    }
    list->count = 1;
    read_media(query, &list->senders[0]);
    *found = list;
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
    NmosStore_Free(&list->store);
    free(list->receivers);
    free(list);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- new_receiver_list -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns an empty list with room for capacity receivers, or null when the memory ran
// out.
//
static DtNmosReceiverList* new_receiver_list(size_t capacity)
{
    DtNmosReceiverList* list = calloc(1, sizeof(*list));
    if (list != NULL && capacity > 0 &&
        (list->receivers = calloc(capacity, sizeof(*list->receivers))) == NULL)
    {
        free(list);
        return NULL;
    }
    return list;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills receiver from a receiver resource of IS-04, its strings owned by store: its
// media from its format and the first media type of its caps, and its subscription.
//
static DtNmosResult read_receiver(const NmosJson* resource, NmosStore* store, void* info)
{
    DtNmosReceiverInfo* receiver = info;
    memset(receiver, 0, sizeof(*receiver));
    copy_id(&receiver->Id, NmosJson_MemberText(resource, "id"));
    copy_id(&receiver->DeviceId, NmosJson_MemberText(resource, "device_id"));
    const NmosJson* types =
        NmosJson_Member(NmosJson_Member(resource, "caps"), "media_types");
    const char* media_type =
        types != NULL && types->type == DTNMOS_JSON_ARRAY && types->count > 0
            ? NmosJson_Text(&types->items[0])
            : NULL;
    receiver->Media = media_of(NmosJson_MemberText(resource, "format"), media_type);
    const NmosJson* subscription = NmosJson_Member(resource, "subscription");
    copy_id(&receiver->SenderId, NmosJson_MemberText(subscription, "sender_id"));
    const NmosJson* active = NmosJson_Member(subscription, "active");
    receiver->Active = active != NULL && active->type == DTNMOS_JSON_TRUE;
    int failed = 0;
    receiver->Label = store_member(store, resource, "label", &failed);
    receiver->Description = store_member(store, resource, "description", &failed);
    receiver->Transport = store_member(store, resource, "transport", &failed);
    return failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Receivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Receivers(DtNmosQuery* query, DtNmosReceiverList** list)
{
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_Receivers");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || list == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Receivers() needs a query and a place for the list.");
    }
    *list = NULL;
    NmosPages receivers;
    memset(&receivers, 0, sizeof(receivers));
    char* url = make_url(query, "receivers", NULL);
    if (url == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult result = get_pages(query, url, &receivers);
    DtNmosReceiverList* built = NULL;
    if (result == DTNMOS_OK)
    {
        size_t total = 0;
        for (size_t p = 0; p < receivers.count; ++p)
        {
            total += receivers.values[p]->count;
        }
        built = new_receiver_list(total);
        if (built == NULL)
        {
            result = NmosError_FailMemory();
        }
    }
    for (size_t p = 0; result == DTNMOS_OK && p < receivers.count; ++p)
    {
        for (size_t i = 0; i < receivers.values[p]->count; ++i)
        {
            if (read_receiver(&receivers.values[p]->items[i], &built->store,
                              &built->receivers[built->count]) != DTNMOS_OK)
            {
                result = NmosError_FailMemory();
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
                                      DtNmosReceiverList** found)
{
    if (found != NULL)
    {
        *found = NULL;
    }
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_FindReceiver");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || id_or_label == NULL || id_or_label[0] == '\0' || found == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_FindReceiver() needs an ID or a label and a place "
            "for the receiver.");
    }
    DtNmosReceiverList* list = new_receiver_list(1);
    if (list == NULL)
    {
        return NmosError_FailMemory();
    }
    static const NmosResourceKind receivers = {"receivers", "receiver", read_receiver};
    const DtNmosResult result =
        find_resource(query, &receivers, id_or_label, &list->store, &list->receivers[0]);
    if (result != DTNMOS_OK)
    {
        DtNmosReceiverList_Free(list);
        return result;
    }
    list->count = 1;
    *found = list;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Manifest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosQuery_Manifest(DtNmosQuery* query, const DtNmosSenderInfo* sender,
                                DtNmosHttpResponse** response)
{
    *response = NULL;
    if (sender->ManifestHref == NULL || sender->ManifestHref[0] == '\0')
    {
        return NmosError_Fail(
            DTNMOS_E_NOT_FOUND,
            "Sender %s ('%s') has no manifest_href, so it gives no SDP.", sender->Id.Text,
            sender->Label == NULL ? "" : sender->Label);
    }
    DtNmosHttpResponse* fetched = DtNmosHttpResponse_Alloc();
    if (fetched == NULL)
    {
        return NmosError_FailMemory();
    }
    const DtNmosResult result = get(query, sender->ManifestHref, fetched);
    if (result != DTNMOS_OK)
    {
        DtNmosHttpResponse_Free(fetched);
        return result;
    }
    *response = fetched;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_SenderManifest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_SenderManifest(DtNmosQuery* query,
                                        const DtNmosSenderInfo* sender, char* buffer,
                                        size_t* size)
{
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_SenderManifest");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || sender == NULL || size == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_SenderManifest() needs a query, a sender and a size.");
    }
    DtNmosHttpResponse* response = NULL;
    DtNmosResult result = NmosQuery_Manifest(query, sender, &response);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = DtNmosHttpResponse_Body(response, &length);
        result = NmosText_CopyText(buffer, size, body, length);
    }
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_SenderSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_SenderSdp(DtNmosQuery* query, const DtNmosSenderInfo* sender,
                                   DtNmosSdp** sdp)
{
    const DtNmosResult open = NmosQuery_CheckOpen(query, "DtNmosQuery_SenderSdp");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (sdp == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosQuery_SenderSdp() needs a place for the SDP.");
    }
    *sdp = NULL;
    if (query == NULL || sender == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosQuery_SenderSdp() needs a query and a sender.");
    }
    DtNmosHttpResponse* response = NULL;
    DtNmosResult result = NmosQuery_Manifest(query, sender, &response);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = DtNmosHttpResponse_Body(response, &length);
        result = DtNmosSdp_Parse(body, length, sdp);
    }
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderInfo_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosSenderInfo_Parse(const char* json, size_t length,
                                    DtNmosSenderList** sender)
{
    if (sender != NULL)
    {
        *sender = NULL;
    }
    if (json == NULL || sender == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosSenderInfo_Parse() needs JSON and a place for the "
                              "sender.");
    }
    NmosJson* resource = NULL;
    if (NmosJson_Parse(json, length, &resource) != DTNMOS_OK ||
        resource->type != DTNMOS_JSON_OBJECT)
    {
        NmosJson_Free(resource);
        return NmosError_Fail(DTNMOS_E_PARSE, "The JSON of a sender is no object.");
    }
    DtNmosSenderList* list = new_sender_list(1);
    if (list == NULL ||
        read_sender(resource, &list->store, &list->senders[0]) != DTNMOS_OK)
    {
        DtNmosSenderList_Free(list);
        NmosJson_Free(resource);
        return NmosError_FailMemory();
    }
    NmosJson_Free(resource);
    list->count = 1;
    *sender = list;
    return DTNMOS_OK;
}
