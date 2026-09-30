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

// A receiver or a sender whose staged parameters a controller patches.
typedef struct staged_resource
{
    const char* noun; // "receiver" or "sender", as a message names it
    const char* path; // "receivers" or "senders", in the URL of the Connection API
    const char* id;
    const char* device_id;
    const char* label;
} staged_resource;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- receiver_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static staged_resource receiver_resource(const dtnmos_receiver_info* receiver)
{
    const staged_resource resource = {"receiver", "receivers", receiver->id.text,
                                      receiver->device_id.text,
                                      dtnmos_string_get(&receiver->label)};
    return resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sender_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static staged_resource sender_resource(const dtnmos_sender_info* sender)
{
    const staged_resource resource = {"sender", "senders", sender->id.text,
                                      sender->device_id.text,
                                      dtnmos_string_get(&sender->label)};
    return resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- staged_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes into url the URL of the staged parameters of resource, from the Connection API
// that the control of its device names.
//
static dtnmos_result staged_url(dtnmos_query* query, const staged_resource* resource,
                                dtnmos_buffer* url, dtnmos_error* error)
{
    const char* label = resource->label;
    if (resource->device_id[0] == '\0')
    {
        return dtnmos_fail(error, DTNMOS_E_NOT_FOUND,
                           "The %s %s ('%s') names no device, so it has no Connection "
                           "API to control it through.",
                           resource->noun, resource->id, label);
    }
    dtnmos_buffer device_url;
    memset(&device_url, 0, sizeof(device_url));
    dtnmos_buffer_printf(&device_url, "%sdevices/%s", dtnmos_query_base(query),
                         resource->device_id);
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
                           "The registry has no device %s of %s %s ('%s').",
                           resource->device_id, resource->noun, resource->id, label);
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
                             "Device %s of %s %s ('%s') has no control %s, so it has no "
                             "Connection API to control it through.",
                             resource->device_id, resource->noun, resource->id, label,
                             DTNMOS_CONNECTION_CONTROL);
    }
    else
    {
        const size_t length = strlen(href);
        dtnmos_buffer_printf(url, "%s%ssingle/%s/%s/staged", href,
                             href[length - 1] == '/' ? "" : "/", resource->path,
                             resource->id);
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
// Sends body as a PATCH of the staged parameters of resource; fails unless the node
// answers with 200, naming the error the node gave.
//
static dtnmos_result patch(dtnmos_query* query, const staged_resource* resource,
                           const dtnmos_buffer* body, dtnmos_error* error)
{
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_result result = staged_url(query, resource, &url, error);
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
        result =
            dtnmos_fail(error, DTNMOS_E_HTTP,
                        "The node of %s %s ('%s') answered the PATCH of %s with %d: "
                        "%s",
                        resource->noun, resource->id, resource->label, url.data, status,
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
        const staged_resource resource = receiver_resource(&found.receiver);
        result = body.failed ? dtnmos_fail_memory(error)
                             : patch(query, &resource, &body, error);
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
        const staged_resource resource = receiver_resource(&found);
        result = body.failed ? dtnmos_fail_memory(error)
                             : patch(query, &resource, &body, error);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_move_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_move_sender(dtnmos_query* query, const char* sender,
                                 const char* destination_ip, uint16_t destination_port,
                                 dtnmos_sender_info* moved, dtnmos_error* error)
{
    if (query == NULL || sender == NULL || sender[0] == '\0' || destination_ip == NULL ||
        destination_ip[0] == '\0' || destination_port == 0)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_move_sender() needs a query, a sender, and an IP "
                           "address and a UDP port to move it to.");
    }
    dtnmos_sender_info_clear(moved);
    dtnmos_sender_info found;
    memset(&found, 0, sizeof(found));
    dtnmos_result result = dtnmos_query_find_sender(query, sender, &found, error);
    const char* transport = dtnmos_string_get(&found.transport);
    if (result == DTNMOS_OK && transport[0] != '\0' &&
        strncmp(transport, "urn:x-nmos:transport:rtp", 24) != 0)
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                             "Sender %s ('%s') sends over %s, not RTP, so it has no "
                             "destination IP address and port to move.",
                             found.id.text, dtnmos_string_get(&found.label), transport);
    }
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"transport_params\": [{\"destination_ip\": ");
        dtnmos_json_write_string(&body, destination_ip);
        dtnmos_buffer_printf(&body,
                             ", \"destination_port\": %u}], \"activation\": "
                             "{\"mode\": \"activate_immediate\"}}",
                             (unsigned)destination_port);
        const staged_resource resource = sender_resource(&found);
        result = body.failed ? dtnmos_fail_memory(error)
                             : patch(query, &resource, &body, error);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && moved != NULL)
    {
        *moved = found;
        return DTNMOS_OK;
    }
    dtnmos_sender_info_clear(&found);
    return result;
}
