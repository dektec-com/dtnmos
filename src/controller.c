// #*#*#*#*#*#*#*#*#*#*#*#*#*#* controller.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - A controller that connects and disconnects receivers (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/controller.h"

#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "json.h"
#include "query-internal.h"

// The type of the control of a device that is the base of its Connection API.
#define DTNMOS_CONNECTION_CONTROL "urn:x-nmos:control:sr-ctrl/v1.1"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_connection_clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dtnmos_connection_clear(dtnmos_connection* connection)
{
    if (connection == NULL)
    {
        return;
    }
    dtnmos_receiver_info_clear(&connection->receiver);
    dtnmos_sender_info_clear(&connection->sender);
    dtnmos_string_clear(&connection->sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- kind_of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the kind of media, as the format of IS-04 names it, or null for another one.
//
static const char* kind_of(dtnmos_media media)
{
    switch (media)
    {
    case DTNMOS_MEDIA_VIDEO:
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return "video";
    case DTNMOS_MEDIA_AUDIO:
        return "audio";
    case DTNMOS_MEDIA_ANC:
        return "data";
    default:
        return NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- staged_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes into url the URL of the staged parameters of receiver, from the Connection API
// that the control of its device names.
//
static dtnmos_result staged_url(dtnmos_query* query, const dtnmos_receiver_info* receiver,
                                dtnmos_buffer* url, dtnmos_error* error)
{
    const char* label = dtnmos_string_get(&receiver->label);
    if (receiver->device_id.text[0] == '\0')
    {
        return dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                           "Receiver %s ('%s') names no device, so it has no Connection "
                           "API to connect it through.",
                           receiver->id.text, label);
    }
    dtnmos_buffer device_url;
    memset(&device_url, 0, sizeof(device_url));
    dtnmos_buffer_printf(&device_url, "%sdevices/%s", dtnmos_query_base(query),
                         receiver->device_id.text);
    if (device_url.failed)
    {
        dtnmos_buffer_free(&device_url);
        return dtnmos_fail_memory(error);
    }
    dtnmos_json* device = NULL;
    dtnmos_result result = dtnmos_query_get_json(query, device_url.data, &device, error);
    dtnmos_buffer_free(&device_url);
    if (result == DTNMOS_E_NOT_FOUND)
    {
        return dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                           "The registry has no device %s of receiver %s ('%s').",
                           receiver->device_id.text, receiver->id.text, label);
    }
    if (result != DTNMOS_OK)
    {
        return result;
    }
    const char* href = NULL;
    const dtnmos_json* controls = dtnmos_json_member(device, "controls");
    for (size_t i = 0; href == NULL && controls != NULL &&
                       controls->type == DTNMOS_JSON_ARRAY && i < controls->count;
         ++i)
    {
        const char* type = dtnmos_json_member_text(&controls->items[i], "type");
        const char* candidate = dtnmos_json_member_text(&controls->items[i], "href");
        if (type != NULL && strcmp(type, DTNMOS_CONNECTION_CONTROL) == 0 &&
            candidate != NULL && candidate[0] != '\0')
        {
            href = candidate;
        }
    }
    if (href == NULL)
    {
        result = dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                             "Device %s of receiver %s ('%s') has no control %s, so it "
                             "has no Connection API to connect it through.",
                             receiver->device_id.text, receiver->id.text, label,
                             DTNMOS_CONNECTION_CONTROL);
    }
    else
    {
        const size_t length = strlen(href);
        dtnmos_buffer_printf(url, "%s%ssingle/receivers/%s/staged", href,
                             href[length - 1] == '/' ? "" : "/", receiver->id.text);
        if (url->failed)
        {
            result = dtnmos_fail_memory(error);
        }
    }
    dtnmos_json_free(device);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- patch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends body as a PATCH of the staged parameters of receiver; fails unless the node
// answers with 200, naming the error the node gave.
//
static dtnmos_result patch(dtnmos_query* query, const dtnmos_receiver_info* receiver,
                           const dtnmos_buffer* body, dtnmos_error* error)
{
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_result result = staged_url(query, receiver, &url, error);
    if (result != DTNMOS_OK)
    {
        dtnmos_buffer_free(&url);
        return result;
    }
    dtnmos_http_response* response = dtnmos_http_response_create();
    if (response == NULL)
    {
        dtnmos_buffer_free(&url);
        return dtnmos_fail_memory(error);
    }
    result = dtnmos_query_request(query, "PATCH", url.data, "application/json",
                                  body->data, body->length, response, error);
    const int status = dtnmos_http_response_status(response);
    if (result == DTNMOS_OK && status != 200)
    {
        // The Connection API answers an error with {"code", "error", "debug"}.
        size_t length = 0;
        const char* text = dtnmos_http_response_body(response, &length);
        dtnmos_json* answer = NULL;
        const char* reason = NULL;
        if (dtnmos_json_parse(text, length, &answer, NULL) == DTNMOS_OK)
        {
            reason = dtnmos_json_member_text(answer, "error");
        }
        result = dtnmos_fail(error, DTNMOS_E_HTTP,
                             "The node of receiver %s ('%s') answered the PATCH of %s "
                             "with %d: %s",
                             receiver->id.text, dtnmos_string_get(&receiver->label),
                             url.data, status,
                             reason != NULL ? reason : (length > 0 ? text : "no body"));
        dtnmos_json_free(answer);
    }
    dtnmos_http_response_free(response);
    dtnmos_buffer_free(&url);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_connect(dtnmos_query* query, const char* receiver,
                             const char* sender, dtnmos_connection* connection,
                             dtnmos_error* error)
{
    if (query == NULL || receiver == NULL || receiver[0] == '\0' || sender == NULL ||
        sender[0] == '\0')
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_connect() needs a query, a receiver and a sender.");
    }
    dtnmos_connection_clear(connection);
    dtnmos_connection found;
    memset(&found, 0, sizeof(found));
    dtnmos_result result =
        dtnmos_query_find_receiver(query, receiver, &found.receiver, error);
    if (result == DTNMOS_OK)
    {
        result = dtnmos_query_find_sender(query, sender, &found.sender, error);
    }
    if (result == DTNMOS_OK)
    {
        // A kind that is not known on either side is left to the node.
        const char* takes = kind_of(found.receiver.media);
        const char* gives = kind_of(found.sender.media);
        if (takes != NULL && gives != NULL && strcmp(takes, gives) != 0)
        {
            result = dtnmos_fail(
                error, DTNMOS_E_INVALID_ARGUMENT,
                "Sender %s ('%s') sends %s, but receiver %s ('%s') "
                "takes %s.",
                found.sender.id.text, dtnmos_string_get(&found.sender.label), gives,
                found.receiver.id.text, dtnmos_string_get(&found.receiver.label), takes);
        }
    }
    if (result == DTNMOS_OK)
    {
        result = dtnmos_query_sender_manifest(query, &found.sender, &found.sdp, error);
    }
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"sender_id\": ");
        dtnmos_json_write_string(&body, found.sender.id.text);
        DTNMOS_APPEND_LITERAL(&body, ", \"master_enable\": true, \"activation\": "
                                     "{\"mode\": \"activate_immediate\"}, "
                                     "\"transport_file\": {\"type\": "
                                     "\"application/sdp\", \"data\": ");
        dtnmos_json_write_string(&body, dtnmos_string_get(&found.sdp));
        DTNMOS_APPEND_LITERAL(&body, "}}");
        result = body.failed ? dtnmos_fail_memory(error)
                             : patch(query, &found.receiver, &body, error);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && connection != NULL)
    {
        *connection = found;
        return DTNMOS_OK;
    }
    dtnmos_connection_clear(&found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_disconnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
dtnmos_result dtnmos_disconnect(dtnmos_query* query, const char* receiver,
                                dtnmos_receiver_info* disconnected, dtnmos_error* error)
{
    if (query == NULL || receiver == NULL || receiver[0] == '\0')
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_disconnect() needs a query and a receiver.");
    }
    dtnmos_receiver_info_clear(disconnected);
    dtnmos_receiver_info found;
    memset(&found, 0, sizeof(found));
    dtnmos_result result = dtnmos_query_find_receiver(query, receiver, &found, error);
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"sender_id\": null, \"master_enable\": false, "
                                     "\"activation\": {\"mode\": "
                                     "\"activate_immediate\"}}");
        result =
            body.failed ? dtnmos_fail_memory(error) : patch(query, &found, &body, error);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && disconnected != NULL)
    {
        *disconnected = found;
        return DTNMOS_OK;
    }
    dtnmos_receiver_info_clear(&found);
    return result;
}
