// #*#*#*#*#*#*#*#*#*#*#*#* DtNmosSubscription.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - A subscription to the resources of a registry (IS-04 v1.3)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/subscription.h"

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosQuery.h"

struct dtnmos_subscription
{
    dtnmos_websocket_transport websocket;
    void* connection;
    char* url; // of the WebSocket
    dtnmos_change_fn on_change;
    void* on_change_user;
    dtnmos_string message;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_change_kind_name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* dtnmos_change_kind_name(dtnmos_change_kind kind)
{
    switch (kind)
    {
    case DTNMOS_CHANGE_PRESENT:
        return "present";
    case DTNMOS_CHANGE_ADDED:
        return "added";
    case DTNMOS_CHANGE_MODIFIED:
        return "modified";
    case DTNMOS_CHANGE_REMOVED:
        return "removed";
    }
    return "unknown";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_subscription_create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_subscription_create(dtnmos_query* query,
                                         const dtnmos_subscription_config* config,
                                         dtnmos_subscription** subscription,
                                         dtnmos_error* error)
{
    if (query == NULL || config == NULL || config->resource_path == NULL ||
        config->resource_path[0] != '/' || config->on_change == NULL ||
        subscription == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "A subscription needs a query, a resource path such as "
                           "\"/senders\", a function for the changes and a place for "
                           "itself.");
    }
    *subscription = NULL;
    const dtnmos_websocket_transport* websocket =
        config->websocket != NULL ? config->websocket : dtnmos_curl_websocket();
    const char* base = dtnmos_query_base(query);

    // Ask for the subscription: not persistent, so that it goes with its WebSocket, and
    // secure when the registry is asked over https.
    dtnmos_buffer body;
    memset(&body, 0, sizeof(body));
    dtnmos_buffer_printf(
        &body, "{\"max_update_rate_ms\": %u, \"resource_path\": ",
        (unsigned)(config->max_update_rate_ms == 0 ? 100 : config->max_update_rate_ms));
    dtnmos_json_write_string(&body, config->resource_path);
    dtnmos_buffer_printf(&body, ", \"params\": {}, \"persist\": false, \"secure\": %s}",
                         strncmp(base, "https:", 6) == 0 ? "true" : "false");
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%ssubscriptions", base);
    dtnmos_http_response* response = dtnmos_http_response_create();
    dtnmos_result result = DTNMOS_OK;
    if (body.failed || url.failed || response == NULL)
    {
        result = dtnmos_fail_memory(error);
    }
    else
    {
        result = dtnmos_query_request(query, "POST", url.data, "application/json",
                                      body.data, body.length, response, error);
    }
    dtnmos_json* answer = NULL;
    if (result == DTNMOS_OK)
    {
        const int status = dtnmos_http_response_status(response);
        size_t length = 0;
        const char* text = dtnmos_http_response_body(response, &length);
        if (status != 200 && status != 201)
        {
            result = dtnmos_fail(error, DTNMOS_E_HTTP,
                                 "The registry answered the subscription to %s with %d.",
                                 config->resource_path, status);
        }
        else if (dtnmos_json_parse(text, length, &answer, NULL) != DTNMOS_OK ||
                 dtnmos_json_member_text(answer, "ws_href") == NULL)
        {
            result = dtnmos_fail(error, DTNMOS_E_PARSE,
                                 "The registry answered the subscription to %s without "
                                 "the ws_href of its WebSocket.",
                                 config->resource_path);
        }
    }
    dtnmos_subscription* made = NULL;
    if (result == DTNMOS_OK)
    {
        const char* href = dtnmos_json_member_text(answer, "ws_href");
        made = calloc(1, sizeof(*made));
        const size_t length = strlen(href);
        if (made == NULL || (made->url = malloc(length + 1)) == NULL)
        {
            result = dtnmos_fail_memory(error);
        }
        else
        {
            memcpy(made->url, href, length + 1);
            made->websocket = *websocket;
            made->on_change = config->on_change;
            made->on_change_user = config->on_change_user;
            result =
                websocket->connect(websocket->user, made->url,
                                   dtnmos_query_timeout(query), &made->connection, error);
        }
    }
    dtnmos_json_free(answer);
    dtnmos_http_response_free(response);
    dtnmos_buffer_free(&url);
    dtnmos_buffer_free(&body);
    if (result != DTNMOS_OK)
    {
        if (made != NULL)
        {
            free(made->url);
            free(made);
        }
        return result;
    }
    *subscription = made;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- report_change -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reports one item of the data of a grain, {"path", "pre", "post"}; an item with neither
// is skipped.
//
static dtnmos_result report_change(dtnmos_subscription* subscription,
                                   const dtnmos_json* item, dtnmos_error* error)
{
    const dtnmos_json* pre = dtnmos_json_member(item, "pre");
    const dtnmos_json* post = dtnmos_json_member(item, "post");
    const int has_pre = pre != NULL && pre->type == DTNMOS_JSON_OBJECT;
    const int has_post = post != NULL && post->type == DTNMOS_JSON_OBJECT;
    if (!has_pre && !has_post)
    {
        return DTNMOS_OK;
    }
    dtnmos_buffer before;
    dtnmos_buffer after;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    if (has_pre)
    {
        dtnmos_json_write(&before, pre);
    }
    if (has_post)
    {
        dtnmos_json_write(&after, post);
    }
    if (before.failed || after.failed)
    {
        dtnmos_buffer_free(&before);
        dtnmos_buffer_free(&after);
        return dtnmos_fail_memory(error);
    }
    dtnmos_change change;
    memset(&change, 0, sizeof(change));
    const char* path = dtnmos_json_member_text(item, "path");
    change.id = path != NULL ? path : "";
    change.pre = has_pre ? before.data : NULL;
    change.pre_length = has_pre ? before.length : 0;
    change.post = has_post ? after.data : NULL;
    change.post_length = has_post ? after.length : 0;
    if (has_pre && has_post)
    {
        // The first message gives each resource as it is, before and after the same.
        change.kind = before.length == after.length &&
                              memcmp(before.data, after.data, before.length) == 0
                          ? DTNMOS_CHANGE_PRESENT
                          : DTNMOS_CHANGE_MODIFIED;
    }
    else
    {
        change.kind = has_post ? DTNMOS_CHANGE_ADDED : DTNMOS_CHANGE_REMOVED;
    }
    subscription->on_change(subscription->on_change_user, &change);
    dtnmos_buffer_free(&before);
    dtnmos_buffer_free(&after);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_subscription_poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_subscription_poll(dtnmos_subscription* subscription,
                                       uint32_t timeout_ms, dtnmos_error* error)
{
    if (subscription == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_subscription_poll() needs a subscription.");
    }
    dtnmos_result result = subscription->websocket.receive(
        subscription->websocket.user, subscription->connection, timeout_ms,
        &subscription->message, error);
    if (result != DTNMOS_OK)
    {
        return result;
    }
    // A grain: {"grain_type": "event", ..., "grain": {"topic": "/senders/",
    // "data": [{"path": ..., "pre": ..., "post": ...}]}}.
    dtnmos_json* json = NULL;
    const dtnmos_json* data = NULL;
    if (dtnmos_json_parse(dtnmos_string_get(&subscription->message),
                          dtnmos_string_length(&subscription->message), &json,
                          NULL) == DTNMOS_OK)
    {
        data = dtnmos_json_member(dtnmos_json_member(json, "grain"), "data");
    }
    if (data == NULL || data->type != DTNMOS_JSON_ARRAY)
    {
        dtnmos_json_free(json);
        return dtnmos_fail(error, DTNMOS_E_PARSE,
                           "A message of the WebSocket %s is no grain with data.",
                           subscription->url);
    }
    for (size_t i = 0; result == DTNMOS_OK && i < data->count; ++i)
    {
        result = report_change(subscription, &data->items[i], error);
    }
    dtnmos_json_free(json);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_subscription_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* dtnmos_subscription_url(const dtnmos_subscription* subscription)
{
    return subscription == NULL ? "" : subscription->url;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_subscription_destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dtnmos_subscription_destroy(dtnmos_subscription* subscription)
{
    if (subscription == NULL)
    {
        return;
    }
    subscription->websocket.close(subscription->websocket.user, subscription->connection);
    dtnmos_string_clear(&subscription->message);
    free(subscription->url);
    free(subscription);
}
