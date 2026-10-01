// #*#*#*#*#*#*#*#*#*#*#*#* DtNmosSubscription.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - A subscription to the resources of a registry (IS-04 v1.3)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosQuery.h"

struct DtNmosSubscription
{
    DtNmosWebSocketTransport websocket;
    void* connection;
    char* url; // of the WebSocket
    DtNmosChangeFunc on_change;
    void* on_change_user;
    DtNmosString message;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosChangeKind_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosChangeKind_Name(DtNmosChangeKind kind)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSubscription_Create(DtNmosQuery* query,
                                       const DtNmosSubscriptionConfig* config,
                                       DtNmosSubscription** subscription)
{
    if (query == NULL || config == NULL || config->ResourcePath == NULL ||
        config->ResourcePath[0] != '/' || config->OnChange == NULL ||
        subscription == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "A subscription needs a query, a resource path such as "
                           "\"/senders\", a function for the changes and a place for "
                           "itself.");
    }
    *subscription = NULL;
    const DtNmosWebSocketTransport* websocket =
        config->WebSocket != NULL ? config->WebSocket : DtNmos_CurlWebSocket();
    const char* base = dtnmos_query_base(query);

    // Ask for the subscription: not persistent, so that it goes with its WebSocket, and
    // secure when the registry is asked over https.
    dtnmos_buffer body;
    memset(&body, 0, sizeof(body));
    dtnmos_buffer_printf(
        &body, "{\"max_update_rate_ms\": %u, \"resource_path\": ",
        (unsigned)(config->MaxUpdateRateMs == 0 ? 100 : config->MaxUpdateRateMs));
    dtnmos_json_write_string(&body, config->ResourcePath);
    dtnmos_buffer_printf(&body, ", \"params\": {}, \"persist\": false, \"secure\": %s}",
                         strncmp(base, "https:", 6) == 0 ? "true" : "false");
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%ssubscriptions", base);
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    DtNmosResult result = DTNMOS_OK;
    if (body.failed || url.failed || response == NULL)
    {
        result = dtnmos_fail_memory();
    }
    else
    {
        result = dtnmos_query_request(query, "POST", url.data, "application/json",
                                      body.data, body.length, response);
    }
    dtnmos_json* answer = NULL;
    if (result == DTNMOS_OK)
    {
        const int status = DtNmosHttpResponse_Status(response);
        size_t length = 0;
        const char* text = DtNmosHttpResponse_Body(response, &length);
        if (status != 200 && status != 201)
        {
            result = dtnmos_fail(DTNMOS_E_HTTP,
                                 "The registry answered the subscription to %s with %d.",
                                 config->ResourcePath, status);
        }
        else if (dtnmos_json_parse(text, length, &answer) != DTNMOS_OK ||
                 dtnmos_json_member_text(answer, "ws_href") == NULL)
        {
            result = dtnmos_fail(DTNMOS_E_PARSE,
                                 "The registry answered the subscription to %s without "
                                 "the ws_href of its WebSocket.",
                                 config->ResourcePath);
        }
    }
    DtNmosSubscription* made = NULL;
    if (result == DTNMOS_OK)
    {
        const char* href = dtnmos_json_member_text(answer, "ws_href");
        made = calloc(1, sizeof(*made));
        const size_t length = strlen(href);
        if (made == NULL || (made->url = malloc(length + 1)) == NULL)
        {
            result = dtnmos_fail_memory();
        }
        else
        {
            memcpy(made->url, href, length + 1);
            made->websocket = *websocket;
            made->on_change = config->OnChange;
            made->on_change_user = config->OnChangeUser;
            result = websocket->Connect(websocket->User, made->url,
                                        dtnmos_query_timeout(query), &made->connection);
        }
    }
    dtnmos_json_free(answer);
    DtNmosHttpResponse_Free(response);
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
static DtNmosResult report_change(DtNmosSubscription* subscription,
                                  const dtnmos_json* item)
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
        return dtnmos_fail_memory();
    }
    DtNmosChange change;
    memset(&change, 0, sizeof(change));
    const char* path = dtnmos_json_member_text(item, "path");
    change.Id = path != NULL ? path : "";
    change.Pre = has_pre ? before.data : NULL;
    change.PreLength = has_pre ? before.length : 0;
    change.Post = has_post ? after.data : NULL;
    change.PostLength = has_post ? after.length : 0;
    if (has_pre && has_post)
    {
        // The first message gives each resource as it is, before and after the same.
        change.Kind = before.length == after.length &&
                              memcmp(before.data, after.data, before.length) == 0
                          ? DTNMOS_CHANGE_PRESENT
                          : DTNMOS_CHANGE_MODIFIED;
    }
    else
    {
        change.Kind = has_post ? DTNMOS_CHANGE_ADDED : DTNMOS_CHANGE_REMOVED;
    }
    subscription->on_change(subscription->on_change_user, &change);
    dtnmos_buffer_free(&before);
    dtnmos_buffer_free(&after);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSubscription_Poll(DtNmosSubscription* subscription,
                                     uint32_t timeout_ms)
{
    if (subscription == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosSubscription_Poll() needs a subscription.");
    }
    DtNmosResult result = subscription->websocket.Receive(
        subscription->websocket.User, subscription->connection, timeout_ms,
        &subscription->message);
    if (result != DTNMOS_OK)
    {
        return result;
    }
    // A grain: {"grain_type": "event", ..., "grain": {"topic": "/senders/",
    // "data": [{"path": ..., "pre": ..., "post": ...}]}}.
    dtnmos_json* json = NULL;
    const dtnmos_json* data = NULL;
    if (dtnmos_json_parse(DtNmosString_Get(&subscription->message),
                          DtNmosString_Length(&subscription->message),
                          &json) == DTNMOS_OK)
    {
        data = dtnmos_json_member(dtnmos_json_member(json, "grain"), "data");
    }
    if (data == NULL || data->type != DTNMOS_JSON_ARRAY)
    {
        dtnmos_json_free(json);
        return dtnmos_fail(DTNMOS_E_PARSE,
                           "A message of the WebSocket %s is no grain with data.",
                           subscription->url);
    }
    for (size_t i = 0; result == DTNMOS_OK && i < data->count; ++i)
    {
        result = report_change(subscription, &data->items[i]);
    }
    dtnmos_json_free(json);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosSubscription_Url(const DtNmosSubscription* subscription)
{
    return subscription == NULL ? "" : subscription->url;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosSubscription_Destroy(DtNmosSubscription* subscription)
{
    if (subscription == NULL)
    {
        return;
    }
    subscription->websocket.Close(subscription->websocket.User, subscription->connection);
    DtNmosString_Clear(&subscription->message);
    free(subscription->url);
    free(subscription);
}
