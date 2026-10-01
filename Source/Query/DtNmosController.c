// #*#*#*#*#*#*#*#*#*#*#*#*# DtNmosController.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A controller that connects and disconnects receivers (IS-05 v1.1)
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

// The type of the control of a device that is the base of its Connection API.
#define DTNMOS_CONNECTION_CONTROL "urn:x-nmos:control:sr-ctrl/v1.1"

// The receiver and the sender each in a list of one, as finding them returned them, and
// the SDP the receiver was given.
struct DtNmosConnection
{
    DtNmosReceiverList* receiver;
    DtNmosSenderList* sender;
    char* sdp;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosConnection_Free(DtNmosConnection* connection)
{
    if (connection == NULL)
    {
        return;
    }
    DtNmosReceiverList_Free(connection->receiver);
    DtNmosSenderList_Free(connection->sender);
    free(connection->sdp);
    free(connection);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosReceiverInfo* DtNmosConnection_Receiver(const DtNmosConnection* connection)
{
    return connection == NULL ? NULL : DtNmosReceiverList_At(connection->receiver, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Sdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosConnection_Sdp(const DtNmosConnection* connection)
{
    return connection == NULL ? NULL : connection->sdp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosSenderInfo* DtNmosConnection_Sender(const DtNmosConnection* connection)
{
    return connection == NULL ? NULL : DtNmosSenderList_At(connection->sender, 0);
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
                                      receiver->DeviceId.Text, receiver->Label};
    return resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sender_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static staged_resource sender_resource(const DtNmosSenderInfo* sender)
{
    const staged_resource resource = {"sender", "senders", sender->Id.Text,
                                      sender->DeviceId.Text, sender->Label};
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
    DtNmosHttpResponse* response = DtNmosHttpResponse_Alloc();
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
                                 const char* sender, DtNmosConnection** connection)
{
    if (connection != NULL)
    {
        *connection = NULL;
    }
    const DtNmosResult open = dtnmos_query_check_open(query, "DtNmosQuery_Connect");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || receiver == NULL || receiver[0] == '\0' || sender == NULL ||
        sender[0] == '\0')
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Connect() needs a query, a receiver and a sender.");
    }
    DtNmosConnection* found = calloc(1, sizeof(*found));
    if (found == NULL)
    {
        return dtnmos_fail_memory();
    }
    DtNmosResult result = DtNmosQuery_FindReceiver(query, receiver, &found->receiver);
    if (result == DTNMOS_OK)
    {
        result = DtNmosQuery_FindSender(query, sender, &found->sender);
    }
    const DtNmosReceiverInfo* takes_info = DtNmosConnection_Receiver(found);
    const DtNmosSenderInfo* gives_info = DtNmosConnection_Sender(found);
    if (result == DTNMOS_OK)
    {
        // A kind that is not known on either side is left to the node.
        const char* takes = kind_of(takes_info->Media);
        const char* gives = kind_of(gives_info->Media);
        if (takes != NULL && gives != NULL && strcmp(takes, gives) != 0)
        {
            result = dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                                 "Sender %s ('%s') sends %s, but receiver %s ('%s') "
                                 "takes %s.",
                                 gives_info->Id.Text, gives_info->Label, gives,
                                 takes_info->Id.Text, takes_info->Label, takes);
        }
    }
    if (result == DTNMOS_OK)
    {
        // The SDP the receiver is given is kept, for the caller to see what it got.
        DtNmosHttpResponse* response = NULL;
        result = dtnmos_query_manifest(query, gives_info, &response);
        if (result == DTNMOS_OK)
        {
            size_t length = 0;
            const char* body = DtNmosHttpResponse_Body(response, &length);
            found->sdp = malloc(length + 1);
            if (found->sdp == NULL)
            {
                result = dtnmos_fail_memory();
            }
            else
            {
                memcpy(found->sdp, body, length);
                found->sdp[length] = '\0';
            }
        }
        DtNmosHttpResponse_Free(response);
    }
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"sender_id\": ");
        dtnmos_json_write_string(&body, gives_info->Id.Text);
        DTNMOS_APPEND_LITERAL(&body, ", \"master_enable\": true, \"activation\": "
                                     "{\"mode\": \"activate_immediate\"}, "
                                     "\"transport_file\": {\"type\": "
                                     "\"application/sdp\", \"data\": ");
        dtnmos_json_write_string(&body, found->sdp);
        DTNMOS_APPEND_LITERAL(&body, "}}");
        const staged_resource resource = receiver_resource(takes_info);
        result = body.failed ? dtnmos_fail_memory() : patch(query, &resource, &body);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && connection != NULL)
    {
        *connection = found;
        return DTNMOS_OK;
    }
    DtNmosConnection_Free(found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Disconnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_Disconnect(DtNmosQuery* query, const char* receiver,
                                    DtNmosReceiverList** disconnected)
{
    if (disconnected != NULL)
    {
        *disconnected = NULL;
    }
    const DtNmosResult open = dtnmos_query_check_open(query, "DtNmosQuery_Disconnect");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || receiver == NULL || receiver[0] == '\0')
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosQuery_Disconnect() needs a query and a receiver.");
    }
    DtNmosReceiverList* found = NULL;
    DtNmosResult result = DtNmosQuery_FindReceiver(query, receiver, &found);
    if (result == DTNMOS_OK)
    {
        dtnmos_buffer body;
        memset(&body, 0, sizeof(body));
        DTNMOS_APPEND_LITERAL(&body, "{\"sender_id\": null, \"master_enable\": false, "
                                     "\"activation\": {\"mode\": "
                                     "\"activate_immediate\"}}");
        const staged_resource resource =
            receiver_resource(DtNmosReceiverList_At(found, 0));
        result = body.failed ? dtnmos_fail_memory() : patch(query, &resource, &body);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && disconnected != NULL)
    {
        *disconnected = found;
        return DTNMOS_OK;
    }
    DtNmosReceiverList_Free(found);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_MoveSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_MoveSender(DtNmosQuery* query, const char* sender,
                                    const char* destination_ip, uint16_t destination_port,
                                    DtNmosSenderList** moved)
{
    if (moved != NULL)
    {
        *moved = NULL;
    }
    const DtNmosResult open = dtnmos_query_check_open(query, "DtNmosQuery_MoveSender");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (query == NULL || sender == NULL || sender[0] == '\0' || destination_ip == NULL ||
        destination_ip[0] == '\0' || destination_port == 0)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosQuery_MoveSender() needs a query, a sender, and an IP "
                           "address and a UDP port to move it to.");
    }
    DtNmosSenderList* found = NULL;
    DtNmosResult result = DtNmosQuery_FindSender(query, sender, &found);
    const DtNmosSenderInfo* info = DtNmosSenderList_At(found, 0);
    if (result == DTNMOS_OK && info->Transport[0] != '\0' &&
        strncmp(info->Transport, "urn:x-nmos:transport:rtp", 24) != 0)
    {
        result = dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                             "Sender %s ('%s') sends over %s, not RTP, so it has no "
                             "destination IP address and port to move.",
                             info->Id.Text, info->Label, info->Transport);
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
        const staged_resource resource = sender_resource(info);
        result = body.failed ? dtnmos_fail_memory() : patch(query, &resource, &body);
        dtnmos_buffer_free(&body);
    }
    if (result == DTNMOS_OK && moved != NULL)
    {
        *moved = found;
        return DTNMOS_OK;
    }
    DtNmosSenderList_Free(found);
    return result;
}
