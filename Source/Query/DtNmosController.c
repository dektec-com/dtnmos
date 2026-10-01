// #*#*#*#*#*#*#*#*#*#*#*#*# DtNmosController.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A controller that connects and disconnects receivers (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdio.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosQuery.h"

// The type of the control of a device that is the base of its Connection API.
#define DTNMOS_CONNECTION_CONTROL "urn:x-nmos:control:sr-ctrl/v1.1"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosConnection_Clear(DtNmosConnection* connection)
{
    if (connection == NULL)
    {
        return;
    }
    DtNmosReceiverInfo_Clear(&connection->Receiver);
    DtNmosSenderInfo_Clear(&connection->Sender);
    DtNmosString_Clear(&connection->Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- kind_of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the kind of media, as the format of IS-04 names it, or null for another one.
//
static const char* kind_of(DtNmosMedia media)
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
static staged_resource receiver_resource(const DtNmosReceiverInfo* receiver)
{
    const staged_resource resource = {"receiver", "receivers", receiver->Id.Text,
                                      receiver->DeviceId.Text,
                                      DtNmosString_Get(&receiver->Label)};
    return resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sender_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static staged_resource sender_resource(const DtNmosSenderInfo* sender)
{
    const staged_resource resource = {"sender", "senders", sender->Id.Text,
                                      sender->DeviceId.Text,
                                      DtNmosString_Get(&sender->Label)};
    return resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- staged_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes into url the URL of the staged parameters of resource, from the Connection API
// that the control of its device names.
//
static DtNmosResult staged_url(DtNmosQuery* query, const staged_resource* resource,
                               dtnmos_buffer* url)
{
    const char* label = resource->label;
    if (resource->device_id[0] == '\0')
    {
        return dtnmos_fail(DTNMOS_E_NOT_FOUND,
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
        return dtnmos_fail_memory();
    }
    dtnmos_json* device = NULL;
    DtNmosResult result = dtnmos_query_get_json(query, device_url.data, &device);
    dtnmos_buffer_free(&device_url);
    if (result == DTNMOS_E_NOT_FOUND)
    {
        return dtnmos_fail(DTNMOS_E_NOT_FOUND,
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
        result = dtnmos_fail(DTNMOS_E_NOT_FOUND,
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
            result = dtnmos_fail_memory();
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
static DtNmosResult patch(DtNmosQuery* query, const staged_resource* resource,
                          const dtnmos_buffer* body)
{
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    DtNmosResult result = staged_url(query, resource, &url);
    if (result != DTNMOS_OK)
    {
        dtnmos_buffer_free(&url);
        return result;
    }
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    if (response == NULL)
    {
        dtnmos_buffer_free(&url);
        return dtnmos_fail_memory();
    }
    result = dtnmos_query_request(query, "PATCH", url.data, "application/json",
                                  body->data, body->length, response);
    const int status = DtNmosHttpResponse_Status(response);
    if (result == DTNMOS_OK && status != 200)
    {
        // The Connection API answers an error with {"code", "error", "debug"}.
        size_t length = 0;
        const char* text = DtNmosHttpResponse_Body(response, &length);
        dtnmos_json* answer = NULL;
        const char* reason = NULL;
        if (dtnmos_json_parse(text, length, &answer) == DTNMOS_OK)
        {
            reason = dtnmos_json_member_text(answer, "error");
        }
        result =
            dtnmos_fail(DTNMOS_E_HTTP,
                        "The node of %s %s ('%s') answered the PATCH of %s with %d: "
                        "%s",
                        resource->noun, resource->id, resource->label, url.data, status,
                        reason != NULL ? reason : (length > 0 ? text : "no body"));
        dtnmos_json_free(answer);
    }
    DtNmosHttpResponse_Free(response);
    dtnmos_buffer_free(&url);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Connect(DtNmosQuery* query, const char* receiver,
                                 const char* sender, DtNmosConnection* connection)
{
    if (query == NULL || receiver == NULL || receiver[0] == '\0' || sender == NULL ||
        sender[0] == '\0')
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Connect() needs a query, a receiver and a sender.");
    }
    DtNmosConnection_Clear(connection);
    DtNmosConnection found;
    memset(&found, 0, sizeof(found));
    DtNmosResult result = DtNmosQuery_FindReceiver(query, receiver, &found.Receiver);
    if (result == DTNMOS_OK)
    {
        result = DtNmosQuery_FindSender(query, sender, &found.Sender);
    }
    if (result == DTNMOS_OK)
    {
        // A kind that is not known on either side is left to the node.
        const char* takes = kind_of(found.Receiver.Media);
        const char* gives = kind_of(found.Sender.Media);
        if (takes != NULL && gives != NULL && strcmp(takes, gives) != 0)
        {
            result = dtnmos_fail(
                DTNMOS_E_INVALID_ARGUMENT,
                "Sender %s ('%s') sends %s, but receiver %s ('%s') "
                "takes %s.",
                found.Sender.Id.Text, DtNmosString_Get(&found.Sender.Label), gives,
                found.Receiver.Id.Text, DtNmosString_Get(&found.Receiver.Label), takes);
        }
    }
    if (result == DTNMOS_OK)
    {
        result = DtNmosQuery_SenderManifest(query, &found.Sender, &found.Sdp);
    }
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"sender_id\": ");
        dtnmos_json_write_string(&body, found.Sender.Id.Text);
        DTNMOS_APPEND_LITERAL(&body, ", \"master_enable\": true, \"activation\": "
                                     "{\"mode\": \"activate_immediate\"}, "
                                     "\"transport_file\": {\"type\": "
                                     "\"application/sdp\", \"data\": ");
        dtnmos_json_write_string(&body, DtNmosString_Get(&found.Sdp));
        DTNMOS_APPEND_LITERAL(&body, "}}");
        const staged_resource resource = receiver_resource(&found.Receiver);
        result = body.failed ? dtnmos_fail_memory() : patch(query, &resource, &body);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && connection != NULL)
    {
        *connection = found;
        return DTNMOS_OK;
    }
    DtNmosConnection_Clear(&found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Disconnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_Disconnect(DtNmosQuery* query, const char* receiver,
                                    DtNmosReceiverInfo* disconnected)
{
    if (query == NULL || receiver == NULL || receiver[0] == '\0')
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosQuery_Disconnect() needs a query and a receiver.");
    }
    DtNmosReceiverInfo_Clear(disconnected);
    DtNmosReceiverInfo found;
    memset(&found, 0, sizeof(found));
    DtNmosResult result = DtNmosQuery_FindReceiver(query, receiver, &found);
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"sender_id\": null, \"master_enable\": false, "
                                     "\"activation\": {\"mode\": "
                                     "\"activate_immediate\"}}");
        const staged_resource resource = receiver_resource(&found);
        result = body.failed ? dtnmos_fail_memory() : patch(query, &resource, &body);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && disconnected != NULL)
    {
        *disconnected = found;
        return DTNMOS_OK;
    }
    DtNmosReceiverInfo_Clear(&found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_MoveSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_MoveSender(DtNmosQuery* query, const char* sender,
                                    const char* destination_ip, uint16_t destination_port,
                                    DtNmosSenderInfo* moved)
{
    if (query == NULL || sender == NULL || sender[0] == '\0' || destination_ip == NULL ||
        destination_ip[0] == '\0' || destination_port == 0)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosQuery_MoveSender() needs a query, a sender, and an IP "
                           "address and a UDP port to move it to.");
    }
    DtNmosSenderInfo_Clear(moved);
    DtNmosSenderInfo found;
    memset(&found, 0, sizeof(found));
    DtNmosResult result = DtNmosQuery_FindSender(query, sender, &found);
    const char* transport = DtNmosString_Get(&found.Transport);
    if (result == DTNMOS_OK && transport[0] != '\0' &&
        strncmp(transport, "urn:x-nmos:transport:rtp", 24) != 0)
    {
        result = dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                             "Sender %s ('%s') sends over %s, not RTP, so it has no "
                             "destination IP address and port to move.",
                             found.Id.Text, DtNmosString_Get(&found.Label), transport);
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
        result = body.failed ? dtnmos_fail_memory() : patch(query, &resource, &body);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && moved != NULL)
    {
        *moved = found;
        return DTNMOS_OK;
    }
    DtNmosSenderInfo_Clear(&found);
    return result;
}
