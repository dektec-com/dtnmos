// SPDX-License-Identifier: BSD-3-Clause
//
// The Query API of an NMOS registry (IS-04 v1.3): GET requests for senders and flows,
// with their paging followed through the Link headers of the answers, and the SDP of a
// sender fetched from its manifest_href.

#include "dtnmos/query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "json.h"

// The most pages a list follows, which bounds a registry whose paging never ends.
#define DTNMOS_MAX_PAGES 1000

// The size of a page the query asks for.
#define DTNMOS_PAGE_LIMIT 100

struct dtnmos_query
{
    char* base; // registry URL followed by /x-nmos/query/v1.3/
    dtnmos_http_fn http;
    void* http_user;
    uint32_t timeout_ms;
    dtnmos_log_fn log;
    void* log_user;
};

struct dtnmos_sender_list
{
    dtnmos_sender_info* senders;
    size_t count;
};

void dtnmos_sender_info_clear(dtnmos_sender_info* sender)
{
    if (sender == NULL)
    {
        return;
    }
    dtnmos_string_clear(&sender->label);
    dtnmos_string_clear(&sender->description);
    dtnmos_string_clear(&sender->transport);
    dtnmos_string_clear(&sender->manifest_href);
    memset(sender, 0, sizeof(*sender));
}

dtnmos_result dtnmos_sender_info_copy(dtnmos_sender_info* target,
                                      const dtnmos_sender_info* source)
{
    if (target == NULL || source == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    dtnmos_sender_info copy;
    memset(&copy, 0, sizeof(copy));
    copy.id = source->id;
    copy.flow_id = source->flow_id;
    copy.device_id = source->device_id;
    copy.media = source->media;
    if (dtnmos_string_copy(&copy.label, &source->label) != DTNMOS_OK ||
        dtnmos_string_copy(&copy.description, &source->description) != DTNMOS_OK ||
        dtnmos_string_copy(&copy.transport, &source->transport) != DTNMOS_OK ||
        dtnmos_string_copy(&copy.manifest_href, &source->manifest_href) != DTNMOS_OK)
    {
        dtnmos_sender_info_clear(&copy);
        return DTNMOS_E_NO_MEMORY;
    }
    dtnmos_sender_info_clear(target);
    *target = copy;
    return DTNMOS_OK;
}

size_t dtnmos_sender_list_count(const dtnmos_sender_list* list)
{
    return list == NULL ? 0 : list->count;
}

const dtnmos_sender_info* dtnmos_sender_list_at(const dtnmos_sender_list* list,
                                                size_t index)
{
    return list == NULL || index >= list->count ? NULL : &list->senders[index];
}

void dtnmos_sender_list_free(dtnmos_sender_list* list)
{
    if (list == NULL)
    {
        return;
    }
    for (size_t i = 0; i < list->count; ++i)
    {
        dtnmos_sender_info_clear(&list->senders[i]);
    }
    free(list->senders);
    free(list);
}

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

dtnmos_result dtnmos_query_create(const dtnmos_query_config* config, dtnmos_query** query,
                                  dtnmos_error* error)
{
    if (query == NULL || config == NULL || config->registry_url == NULL ||
        config->registry_url[0] == '\0' || config->http == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "A query needs the URL of a registry and an HTTP function.");
    }
    *query = NULL;
    if (config->api_version != NULL && strcmp(config->api_version, "v1.3") != 0)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "The Query API %s is not supported; dtnmos speaks v1.3.",
                           config->api_version);
    }
    dtnmos_query* result = calloc(1, sizeof(*result));
    if (result == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    size_t length = strlen(config->registry_url);
    while (length > 0 && config->registry_url[length - 1] == '/')
    {
        --length;
    }
    dtnmos_buffer base;
    memset(&base, 0, sizeof(base));
    dtnmos_buffer_append(&base, config->registry_url, length);
    DTNMOS_APPEND_LITERAL(&base, "/x-nmos/query/v1.3/");
    if (base.failed)
    {
        dtnmos_buffer_free(&base);
        free(result);
        return dtnmos_fail_memory(error);
    }
    result->base = base.data;
    result->http = config->http;
    result->http_user = config->http_user;
    result->timeout_ms = config->timeout_ms == 0 ? 5000 : config->timeout_ms;
    result->log = config->log;
    result->log_user = config->log_user;
    *query = result;
    return DTNMOS_OK;
}

void dtnmos_query_destroy(dtnmos_query* query)
{
    if (query == NULL)
    {
        return;
    }
    free(query->base);
    free(query);
}

static void log_message(dtnmos_query* query, dtnmos_log_level level, const char* message)
{
    if (query->log != NULL)
    {
        query->log(query->log_user, level, message);
    }
}

// Performs a GET of url into response; fails unless the answer is 200, with
// DTNMOS_E_NOT_FOUND for 404.
static dtnmos_result get(dtnmos_query* query, const char* url,
                         dtnmos_http_response* response, dtnmos_error* error)
{
    dtnmos_http_request request;
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.method = "GET";
    request.url = url;
    request.timeout_ms = query->timeout_ms;
    char message[600];
    snprintf(message, sizeof(message), "GET %s", url);
    log_message(query, DTNMOS_LOG_DEBUG, message);
    dtnmos_result result = query->http(query->http_user, &request, response, error);
    if (result != DTNMOS_OK)
    {
        if (error != NULL && error->message[0] == '\0')
        {
            dtnmos_fail(error, result, "GET %s failed.", url);
        }
        return result;
    }
    const int status = dtnmos_http_response_status(response);
    if (status == 404)
    {
        return dtnmos_fail(error, DTNMOS_E_NOT_FOUND, "The registry has no %s (404).",
                           url);
    }
    if (status != 200)
    {
        return dtnmos_fail(error, DTNMOS_E_HTTP, "GET %s was answered with %d.", url,
                           status);
    }
    return DTNMOS_OK;
}

// Performs a GET of url and parses its JSON; the caller frees json and, when next is not
// null, *next, the URL of the next page from the Link header, or null.
static dtnmos_result get_json(dtnmos_query* query, const char* url, dtnmos_json** json,
                              char** next, dtnmos_error* error)
{
    *json = NULL;
    dtnmos_http_response* response = dtnmos_http_response_create();
    if (response == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    if (error != NULL)
    {
        error->message[0] = '\0';
    }
    dtnmos_result result = get(query, url, response, error);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = dtnmos_http_response_body(response, &length);
        result = dtnmos_json_parse(body, length, json, error);
        if (result != DTNMOS_OK)
        {
            char reason[512];
            snprintf(reason, sizeof(reason), "%s", error != NULL ? error->message : "");
            result = dtnmos_fail(error, DTNMOS_E_PARSE,
                                 "The answer to GET %s is no JSON: %s", url, reason);
        }
    }
    if (result == DTNMOS_OK && next != NULL)
    {
        *next = NULL;
        // Link: <url>; rel="next", <url>; rel="prev", ...
        const char* link = dtnmos_http_response_find_header(response, "Link");
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
                    result = dtnmos_fail_memory(error);
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
    dtnmos_http_response_free(response);
    return result;
}

// Builds base + path, and a query of the paging limit.
static char* make_url(const dtnmos_query* query, const char* path, const char* parameters)
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

static void free_pages(pages* p)
{
    for (size_t i = 0; i < p->count; ++i)
    {
        dtnmos_json_free(p->values[i]);
    }
    free(p->values);
    memset(p, 0, sizeof(*p));
}

// Fetches every page of the list at url, each an array, into p.
static dtnmos_result get_pages(dtnmos_query* query, char* url, pages* p,
                               dtnmos_error* error)
{
    for (int page = 0; url != NULL; ++page)
    {
        if (page == DTNMOS_MAX_PAGES)
        {
            free(url);
            return dtnmos_fail(error, DTNMOS_E_HTTP,
                               "The registry pages on beyond %d pages.",
                               DTNMOS_MAX_PAGES);
        }
        dtnmos_json* json = NULL;
        char* next = NULL;
        dtnmos_result result = get_json(query, url, &json, &next, error);
        if (result == DTNMOS_OK && json->type != DTNMOS_JSON_ARRAY)
        {
            dtnmos_json_free(json);
            result = dtnmos_fail(error, DTNMOS_E_PARSE,
                                 "The answer to GET %s is no array.", url);
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
                return dtnmos_fail_memory(error);
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

// Returns the media of a flow resource of IS-04, from its format and media type.
static dtnmos_media media_of_flow(const dtnmos_json* flow)
{
    const char* format = dtnmos_json_member_text(flow, "format");
    const char* media_type = dtnmos_json_member_text(flow, "media_type");
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

static void copy_id(dtnmos_id* id, const char* text)
{
    memset(id, 0, sizeof(*id));
    if (text != NULL && strlen(text) < sizeof(id->text))
    {
        strcpy(id->text, text);
    }
}

// Fills sender from a sender resource of IS-04; its media stays for the caller.
static dtnmos_result read_sender(const dtnmos_json* resource, dtnmos_sender_info* sender)
{
    memset(sender, 0, sizeof(*sender));
    sender->media = DTNMOS_MEDIA_OTHER;
    copy_id(&sender->id, dtnmos_json_member_text(resource, "id"));
    copy_id(&sender->flow_id, dtnmos_json_member_text(resource, "flow_id"));
    copy_id(&sender->device_id, dtnmos_json_member_text(resource, "device_id"));
    if (dtnmos_string_set_text(&sender->label,
                               dtnmos_json_member_text(resource, "label")) != DTNMOS_OK ||
        dtnmos_string_set_text(&sender->description,
                               dtnmos_json_member_text(resource, "description")) !=
            DTNMOS_OK ||
        dtnmos_string_set_text(&sender->transport,
                               dtnmos_json_member_text(resource, "transport")) !=
            DTNMOS_OK ||
        dtnmos_string_set_text(&sender->manifest_href,
                               dtnmos_json_member_text(resource, "manifest_href")) !=
            DTNMOS_OK)
    {
        dtnmos_sender_info_clear(sender);
        return DTNMOS_E_NO_MEMORY;
    }
    return DTNMOS_OK;
}

dtnmos_result dtnmos_query_senders(dtnmos_query* query, dtnmos_sender_list** list,
                                   dtnmos_error* error)
{
    if (query == NULL || list == NULL)
    {
        return dtnmos_fail(
            error, DTNMOS_E_INVALID_ARGUMENT,
            "dtnmos_query_senders() needs a query and a place for the list.");
    }
    *list = NULL;
    pages senders;
    pages flows;
    memset(&senders, 0, sizeof(senders));
    memset(&flows, 0, sizeof(flows));
    char* url = make_url(query, "senders", NULL);
    if (url == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    dtnmos_result result = get_pages(query, url, &senders, error);
    if (result == DTNMOS_OK)
    {
        url = make_url(query, "flows", NULL);
        result = url == NULL ? dtnmos_fail_memory(error)
                             : get_pages(query, url, &flows, error);
    }
    dtnmos_sender_list* built = NULL;
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
            result = dtnmos_fail_memory(error);
        }
    }
    for (size_t p = 0; result == DTNMOS_OK && p < senders.count; ++p)
    {
        for (size_t i = 0; result == DTNMOS_OK && i < senders.values[p]->count; ++i)
        {
            const dtnmos_json* resource = &senders.values[p]->items[i];
            dtnmos_sender_info* sender = &built->senders[built->count];
            if (read_sender(resource, sender) != DTNMOS_OK)
            {
                result = dtnmos_fail_memory(error);
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
                    if (id != NULL && strcmp(id, sender->flow_id.text) == 0)
                    {
                        sender->media = media_of_flow(&flows.values[f]->items[j]);
                    }
                }
            }
        }
    }
    free_pages(&senders);
    free_pages(&flows);
    if (result != DTNMOS_OK)
    {
        dtnmos_sender_list_free(built);
        return result;
    }
    *list = built;
    return DTNMOS_OK;
}

// Whether text has the form of a UUID.
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

// Appends text to a URL, encoded as RFC 3986 asks of a query value.
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

// Sets the media of sender from its flow, which it fetches; a flow that cannot be fetched
// leaves DTNMOS_MEDIA_OTHER.
static void read_media(dtnmos_query* query, dtnmos_sender_info* sender)
{
    if (sender->flow_id.text[0] == '\0')
    {
        return;
    }
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%sflows/%s", query->base, sender->flow_id.text);
    dtnmos_json* flow = NULL;
    dtnmos_error ignored;
    if (!url.failed && get_json(query, url.data, &flow, NULL, &ignored) == DTNMOS_OK)
    {
        sender->media = media_of_flow(flow);
        dtnmos_json_free(flow);
    }
    dtnmos_buffer_free(&url);
}

dtnmos_result dtnmos_query_find_sender(dtnmos_query* query, const char* id_or_label,
                                       dtnmos_sender_info* sender, dtnmos_error* error)
{
    if (query == NULL || id_or_label == NULL || id_or_label[0] == '\0' || sender == NULL)
    {
        return dtnmos_fail(
            error, DTNMOS_E_INVALID_ARGUMENT,
            "dtnmos_query_find_sender() needs an ID or a label and a sender.");
    }
    dtnmos_sender_info_clear(sender);
    if (is_uuid(id_or_label))
    {
        dtnmos_buffer url;
        memset(&url, 0, sizeof(url));
        dtnmos_buffer_printf(&url, "%ssenders/%s", query->base, id_or_label);
        if (url.failed)
        {
            dtnmos_buffer_free(&url);
            return dtnmos_fail_memory(error);
        }
        dtnmos_json* resource = NULL;
        dtnmos_result result = get_json(query, url.data, &resource, NULL, error);
        dtnmos_buffer_free(&url);
        if (result == DTNMOS_E_NOT_FOUND)
        {
            return dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                               "The registry has no sender %s.", id_or_label);
        }
        if (result != DTNMOS_OK)
        {
            return result;
        }
        result = read_sender(resource, sender);
        dtnmos_json_free(resource);
        if (result != DTNMOS_OK)
        {
            return dtnmos_fail_memory(error);
        }
        read_media(query, sender);
        return DTNMOS_OK;
    }

    // A label is matched by the registry, and again here, as the Query API compares
    // values of a basic query in ways of its own.
    dtnmos_buffer parameters;
    memset(&parameters, 0, sizeof(parameters));
    DTNMOS_APPEND_LITERAL(&parameters, "label=");
    append_encoded(&parameters, id_or_label);
    char* url = parameters.failed ? NULL : make_url(query, "senders", parameters.data);
    dtnmos_buffer_free(&parameters);
    if (url == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    pages found;
    memset(&found, 0, sizeof(found));
    dtnmos_result result = get_pages(query, url, &found, error);
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
        result = dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                             "The registry has no sender labelled '%s'.", id_or_label);
    }
    else if (result == DTNMOS_OK && matches > 1)
    {
        result = dtnmos_fail(error, DTNMOS_E_AMBIGUOUS,
                             "%zu senders of the registry are labelled '%s': %s.",
                             matches, id_or_label, ids.data == NULL ? "" : ids.data);
    }
    else if (result == DTNMOS_OK && read_sender(match, sender) != DTNMOS_OK)
    {
        result = dtnmos_fail_memory(error);
    }
    dtnmos_buffer_free(&ids);
    free_pages(&found);
    if (result == DTNMOS_OK)
    {
        read_media(query, sender);
    }
    return result;
}

dtnmos_result dtnmos_query_sender_manifest(dtnmos_query* query,
                                           const dtnmos_sender_info* sender,
                                           dtnmos_string* text, dtnmos_error* error)
{
    if (query == NULL || sender == NULL || text == NULL)
    {
        return dtnmos_fail(
            error, DTNMOS_E_INVALID_ARGUMENT,
            "dtnmos_query_sender_manifest() needs a query, a sender and a text.");
    }
    if (dtnmos_string_length(&sender->manifest_href) == 0)
    {
        return dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                           "Sender %s ('%s') has no manifest_href, so it gives no SDP.",
                           sender->id.text, dtnmos_string_get(&sender->label));
    }
    dtnmos_http_response* response = dtnmos_http_response_create();
    if (response == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    if (error != NULL)
    {
        error->message[0] = '\0';
    }
    dtnmos_result result =
        get(query, dtnmos_string_get(&sender->manifest_href), response, error);
    if (result == DTNMOS_OK)
    {
        size_t length = 0;
        const char* body = dtnmos_http_response_body(response, &length);
        if (dtnmos_string_set(text, body, length) != DTNMOS_OK)
        {
            result = dtnmos_fail_memory(error);
        }
    }
    dtnmos_http_response_free(response);
    return result;
}

dtnmos_result dtnmos_query_sender_sdp(dtnmos_query* query,
                                      const dtnmos_sender_info* sender, dtnmos_sdp** sdp,
                                      dtnmos_error* error)
{
    if (sdp == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_query_sender_sdp() needs a place for the SDP.");
    }
    *sdp = NULL;
    dtnmos_string text = {0};
    dtnmos_result result = dtnmos_query_sender_manifest(query, sender, &text, error);
    if (result == DTNMOS_OK)
    {
        result = dtnmos_sdp_parse(dtnmos_string_get(&text), dtnmos_string_length(&text),
                                  sdp, error);
    }
    dtnmos_string_clear(&text);
    return result;
}
