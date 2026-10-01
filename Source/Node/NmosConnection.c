// #*#*#*#*#*#*#*#*#*#*#*#*#* NmosConnection.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The Connection API of the node (AMWA IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "NmosNode.h"

// The transport parameters of the one leg of RTP. An address is "auto", an address, or
// "" for null; a port is -1 for "auto".
typedef struct NmosLeg
{
    char source_ip[DTNMOS_MAX_ADDRESS_SIZE];
    char destination_ip[DTNMOS_MAX_ADDRESS_SIZE]; // of a sender
    char multicast_ip[DTNMOS_MAX_ADDRESS_SIZE];   // of a receiver
    char interface_ip[DTNMOS_MAX_ADDRESS_SIZE];   // of a receiver
    int source_port;                              // of a sender
    int destination_port;
    int rtp_enabled;
} NmosLeg;

// The staged or active parameters of a sender or receiver.
typedef struct NmosParameters
{
    int master_enable;
    char peer_id[37]; // receiver_id of a sender, sender_id of a receiver; "" for null
    NmosLeg transport;
    char* transport_file;     // of a receiver: the SDP it was given, or null
    char activation_time[32]; // of the last activation; "" for null
} NmosParameters;

typedef struct NmosConnection
{
    NmosParameters staged;
    NmosParameters active;
} NmosConnection;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static char* copy_text(const char* text)
{
    if (text == NULL)
    {
        return NULL;
    }
    const size_t length = strlen(text);
    char* copy = malloc(length + 1);
    if (copy != NULL)
    {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_parameters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes target a copy of source; returns 0 when out of memory, leaving target as it was.
//
static int copy_parameters(NmosParameters* target, const NmosParameters* source)
{
    char* file = copy_text(source->transport_file);
    if (source->transport_file != NULL && file == NULL)
    {
        return 0;
    }
    free(target->transport_file);
    *target = *source;
    target->transport_file = file;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_connection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void free_connection(NmosConnection* c)
{
    if (c != NULL)
    {
        free(c->staged.transport_file);
        free(c->active.transport_file);
        free(c);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_InitSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosConnection_InitSender(NmosNodeSender* sender)
{
    NmosConnection* c = calloc(1, sizeof(*c));
    if (c == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    // A sender sends where its element was told to until a controller moves it.
    NmosLeg* t = &c->active.transport;
    snprintf(t->source_ip, sizeof(t->source_ip), "%s",
             sender->source_ip != NULL && sender->source_ip[0] != '\0' ? sender->source_ip
                                                                       : "auto");
    snprintf(t->destination_ip, sizeof(t->destination_ip), "%s",
             sender->flow.DestinationIp);
    t->source_port = sender->flow.DestinationPort;
    t->destination_port = sender->flow.DestinationPort;
    t->rtp_enabled = 1;
    c->active.master_enable = 1;
    c->staged = c->active;
    sender->connection = c;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_ClearSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosConnection_ClearSender(NmosNodeSender* sender)
{
    free_connection(sender->connection);
    sender->connection = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_InitReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosConnection_InitReceiver(NmosNodeReceiver* receiver)
{
    NmosConnection* c = calloc(1, sizeof(*c));
    if (c == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    // A receiver receives what its element was given until a controller connects it.
    NmosLeg* t = &c->active.transport;
    snprintf(t->source_ip, sizeof(t->source_ip), "auto");
    snprintf(t->interface_ip, sizeof(t->interface_ip), "auto");
    t->destination_port = -1;
    t->rtp_enabled = 1;
    c->active.master_enable = 1;
    c->staged = c->active;
    receiver->connection = c;
    receiver->master_enable = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_ClearReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosConnection_ClearReceiver(NmosNodeReceiver* receiver)
{
    free_connection(receiver->connection);
    receiver->connection = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_address -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes an address, or null for "".
//
static void write_address(NmosBuffer* b, const char* address)
{
    if (address[0] == '\0')
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    else
    {
        NmosJson_WriteString(b, address);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_port -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_port(NmosBuffer* b, int port)
{
    if (port < 0)
    {
        DTNMOS_APPEND_LITERAL(b, "\"auto\"");
    }
    else
    {
        NmosBuffer_Printf(b, "%d", port);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_parameters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the parameters p of a sender or receiver; with_activation writes the immediate
// activation of p, which staged parameters show only in the answer to their PATCH.
//
static void write_parameters(NmosBuffer* b, const NmosParameters* p, int sender,
                             int with_activation)
{
    NmosBuffer_Printf(b, "{\"%s\": ", sender ? "receiver_id" : "sender_id");
    write_address(b, p->peer_id);
    NmosBuffer_Printf(b, ", \"master_enable\": %s, \"activation\": ",
                      p->master_enable ? "true" : "false");
    if (with_activation && p->activation_time[0] != '\0')
    {
        NmosBuffer_Printf(b,
                          "{\"mode\": \"activate_immediate\", \"requested_time\": null, "
                          "\"activation_time\": \"%s\"}",
                          p->activation_time);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "{\"mode\": null, \"requested_time\": null, "
                                 "\"activation_time\": null}");
    }
    if (!sender)
    {
        DTNMOS_APPEND_LITERAL(b, ", \"transport_file\": {\"data\": ");
        if (p->transport_file != NULL)
        {
            NmosJson_WriteString(b, p->transport_file);
            DTNMOS_APPEND_LITERAL(b, ", \"type\": \"application/sdp\"}");
        }
        else
        {
            DTNMOS_APPEND_LITERAL(b, "null, \"type\": null}");
        }
    }
    DTNMOS_APPEND_LITERAL(b, ", \"transport_params\": [{\"source_ip\": ");
    write_address(b, p->transport.source_ip);
    if (sender)
    {
        DTNMOS_APPEND_LITERAL(b, ", \"destination_ip\": ");
        write_address(b, p->transport.destination_ip);
        DTNMOS_APPEND_LITERAL(b, ", \"source_port\": ");
        write_port(b, p->transport.source_port);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, ", \"multicast_ip\": ");
        write_address(b, p->transport.multicast_ip);
        DTNMOS_APPEND_LITERAL(b, ", \"interface_ip\": ");
        write_address(b, p->transport.interface_ip);
    }
    DTNMOS_APPEND_LITERAL(b, ", \"destination_port\": ");
    write_port(b, p->transport.destination_port);
    NmosBuffer_Printf(b, ", \"rtp_enabled\": %s}]}",
                      p->transport.rtp_enabled ? "true" : "false");
}

// The constraints of the one leg: each parameter of the leg, unconstrained.
static const char sender_constraints[] =
    "[{\"source_ip\": {}, \"destination_ip\": {}, \"source_port\": {}, "
    "\"destination_port\": {}, \"rtp_enabled\": {}}]";
static const char receiver_constraints[] =
    "[{\"source_ip\": {}, \"multicast_ip\": {}, \"interface_ip\": {}, "
    "\"destination_port\": {}, \"rtp_enabled\": {}}]";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void answer_text(DtNmosHttpResponse* response, const char* text)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosBuffer_Append(&b, text, strlen(text));
    NmosNode_AnswerJson(response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_address -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads an address: a string, or null into "". Returns 0 for another type.
//
static int read_address(const NmosJson* value, char* target, size_t size)
{
    if (value->type == DTNMOS_JSON_NULL)
    {
        target[0] = '\0';
        return 1;
    }
    if (value->type != DTNMOS_JSON_STRING || value->string_length >= size)
    {
        return 0;
    }
    memcpy(target, value->string, value->string_length + 1);
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_port -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads a port: a whole number of 0 to 65535, or "auto" into -1.
//
static int read_port(const NmosJson* value, int* port)
{
    if (value->type == DTNMOS_JSON_STRING && strcmp(value->string, "auto") == 0)
    {
        *port = -1;
        return 1;
    }
    if (value->type != DTNMOS_JSON_NUMBER || value->number < 0 || value->number > 65535 ||
        (double)(int)value->number != value->number)
    {
        return 0;
    }
    *port = (int)value->number;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_bool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int read_bool(const NmosJson* value, int* target)
{
    if (value->type != DTNMOS_JSON_TRUE && value->type != DTNMOS_JSON_FALSE)
    {
        return 0;
    }
    *target = value->type == DTNMOS_JSON_TRUE;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- merge_leg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Merges the transport parameters of the one leg into t; returns a message on failure.
//
static const char* merge_leg(const NmosJson* value, int sender, NmosLeg* t)
{
    if (value->type != DTNMOS_JSON_ARRAY || value->count != 1 ||
        value->items[0].type != DTNMOS_JSON_OBJECT)
    {
        return "transport_params holds the parameters of one leg.";
    }
    const NmosJson* params = &value->items[0];
    for (size_t i = 0; i < params->count; ++i)
    {
        const char* name = params->keys[i];
        const NmosJson* member = &params->items[i];
        int valid = 0;
        if (strcmp(name, "source_ip") == 0)
        {
            valid = read_address(member, t->source_ip, sizeof(t->source_ip));
        }
        else if (sender && strcmp(name, "destination_ip") == 0)
        {
            valid = read_address(member, t->destination_ip, sizeof(t->destination_ip));
        }
        else if (!sender && strcmp(name, "multicast_ip") == 0)
        {
            valid = read_address(member, t->multicast_ip, sizeof(t->multicast_ip));
        }
        else if (!sender && strcmp(name, "interface_ip") == 0)
        {
            valid = read_address(member, t->interface_ip, sizeof(t->interface_ip));
        }
        else if (sender && strcmp(name, "source_port") == 0)
        {
            valid = read_port(member, &t->source_port);
        }
        else if (strcmp(name, "destination_port") == 0)
        {
            valid = read_port(member, &t->destination_port);
        }
        else if (strcmp(name, "rtp_enabled") == 0)
        {
            valid = read_bool(member, &t->rtp_enabled);
        }
        else
        {
            return "transport_params holds a parameter that RTP does not have.";
        }
        if (!valid)
        {
            return "A transport parameter has a value of the wrong type.";
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- merge_patch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Merges the body of a PATCH into staged, and sets activate when it asks for an immediate
// activation. Returns a message on failure, with its status in status.
//
static const char* merge_patch(const NmosJson* body, int sender, NmosParameters* staged,
                               int* activate, int* status)
{
    *status = 400;
    if (body->type != DTNMOS_JSON_OBJECT)
    {
        return "The body of a PATCH is a JSON object.";
    }
    for (size_t i = 0; i < body->count; ++i)
    {
        const char* key = body->keys[i];
        const NmosJson* value = &body->items[i];
        const char* failure = NULL;
        if (strcmp(key, "master_enable") == 0)
        {
            if (!read_bool(value, &staged->master_enable))
            {
                failure = "master_enable is true or false.";
            }
        }
        else if (strcmp(key, sender ? "receiver_id" : "sender_id") == 0)
        {
            if (!read_address(value, staged->peer_id, sizeof(staged->peer_id)))
            {
                failure = "The ID of the peer is a UUID or null.";
            }
        }
        else if (!sender && strcmp(key, "transport_file") == 0)
        {
            const NmosJson* data = NmosJson_Member(value, "data");
            if (data == NULL ||
                (data->type != DTNMOS_JSON_STRING && data->type != DTNMOS_JSON_NULL))
            {
                return "transport_file holds data: an SDP, or null.";
            }
            char* file =
                data->type == DTNMOS_JSON_STRING ? copy_text(data->string) : NULL;
            if (data->type == DTNMOS_JSON_STRING && file == NULL)
            {
                *status = 500;
                return "Out of memory.";
            }
            free(staged->transport_file);
            staged->transport_file = file;
        }
        else if (strcmp(key, "transport_params") == 0)
        {
            failure = merge_leg(value, sender, &staged->transport);
        }
        else if (strcmp(key, "activation") == 0)
        {
            const NmosJson* mode = NmosJson_Member(value, "mode");
            const char* name = NmosJson_Text(mode);
            if (mode == NULL || (mode->type != DTNMOS_JSON_NULL && name == NULL))
            {
                return "activation holds a mode.";
            }
            if (name != NULL && strcmp(name, "activate_immediate") == 0)
            {
                *activate = 1;
            }
            else if (name != NULL)
            {
                *status = 501;
                return "The node activates immediately only.";
            }
        }
        else
        {
            return "The staged parameters have no such member.";
        }
        if (failure != NULL)
        {
            return failure;
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- find_connection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds the sender or receiver id; returns its connection, or null.
//
static NmosConnection* find_connection(DtNmosNode* node, const char* id, int sender,
                                       NmosNodeSender** s, NmosNodeReceiver** r)
{
    *s = NULL;
    *r = NULL;
    DtNmosId wanted;
    memset(&wanted, 0, sizeof(wanted));
    if (strlen(id) >= sizeof(wanted.Text))
    {
        return NULL;
    }
    strcpy(wanted.Text, id);
    if (sender)
    {
        *s = NmosNode_FindSender(node, &wanted);
        return *s != NULL ? (*s)->connection : NULL;
    }
    *r = NmosNode_FindReceiver(node, &wanted);
    return *r != NULL ? (*r)->connection : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- receiver_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills flow with what a receiver of media receives by staged: the flow of that media in
// its transport file, with the transport parameters that are set over it. Returns 0 when
// the transport file describes none.
//
static int receiver_flow(DtNmosMedia media, const NmosParameters* staged,
                         NmosStore* store, DtNmosFlow* flow)
{
    DtNmosSdp* sdp = NULL;
    if (DtNmosSdp_Parse(staged->transport_file, strlen(staged->transport_file), &sdp) !=
        DTNMOS_OK)
    {
        return 0;
    }
    int found = 0;
    for (size_t i = 0; !found && i < DtNmosSdp_FlowCount(sdp); ++i)
    {
        const DtNmosFlow* candidate = DtNmosSdp_Flow(sdp, i);
        if (candidate->Media == media && candidate->Leg == 0)
        {
            found = NmosFlow_Copy(flow, store, candidate) == DTNMOS_OK;
        }
    }
    DtNmosSdp_Free(sdp);
    if (!found)
    {
        return 0;
    }
    const NmosLeg* t = &staged->transport;
    if (t->multicast_ip[0] != '\0' && strcmp(t->multicast_ip, "auto") != 0)
    {
        snprintf(flow->DestinationIp, sizeof(flow->DestinationIp), "%s", t->multicast_ip);
    }
    if (t->source_ip[0] != '\0' && strcmp(t->source_ip, "auto") != 0)
    {
        snprintf(flow->SourceIp, sizeof(flow->SourceIp), "%s", t->source_ip);
    }
    if (t->destination_port >= 0)
    {
        flow->DestinationPort = (uint16_t)t->destination_port;
    }
    return 1;
}

// What an activation hands to the callback of a sender or receiver, gathered under the
// lock of the node and applied without it.
typedef struct NmosActivation
{
    DtNmosSenderActivateFunc sender_callback;
    DtNmosReceiverActivateFunc receiver_callback;
    void* user;
    DtNmosId resource;
    DtNmosSenderActivation sender;
    DtNmosReceiverActivation receiver;
    NmosStore store; // the strings and arrays of the flow of receiver
} NmosActivation;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clear_activation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void clear_activation(NmosActivation* a)
{
    NmosStore_Free(&a->store);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- gather_activation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gathers the activation of staged on the sender s or the receiver r. Returns a message
// when staged cannot be activated.
//
static const char* gather_activation(const NmosNodeSender* s, const NmosNodeReceiver* r,
                                     const NmosParameters* staged, NmosActivation* a)
{
    const NmosLeg* t = &staged->transport;
    const int enabled = staged->master_enable && t->rtp_enabled;
    if (s != NULL)
    {
        a->sender_callback = s->activate;
        a->user = s->user;
        a->resource = s->id;
        a->sender.MasterEnable = enabled;
        const int automatic =
            t->destination_ip[0] == '\0' || strcmp(t->destination_ip, "auto") == 0;
        snprintf(a->sender.DestinationIp, sizeof(a->sender.DestinationIp), "%s",
                 automatic ? s->flow.DestinationIp : t->destination_ip);
        snprintf(a->sender.SourceIp, sizeof(a->sender.SourceIp), "%s",
                 strcmp(t->source_ip, "auto") == 0 ? "" : t->source_ip);
        a->sender.DestinationPort = t->destination_port >= 0
                                        ? (uint16_t)t->destination_port
                                        : s->flow.DestinationPort;
        return NULL;
    }
    a->receiver_callback = r->activate;
    a->user = r->user;
    a->resource = r->id;
    a->receiver.MasterEnable = enabled;
    snprintf(a->receiver.SenderId.Text, sizeof(a->receiver.SenderId.Text), "%s",
             staged->peer_id);
    if (staged->transport_file != NULL)
    {
        a->receiver.HasFlow =
            receiver_flow(r->media, staged, &a->store, &a->receiver.Flow);
        if (!a->receiver.HasFlow)
        {
            return "The transport file describes no flow that the receiver receives.";
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_active -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes staged, activated, the active parameters of the sender s or receiver r, with the
// "auto" of a sender resolved, and registers the new state of the sender or receiver.
//
static void make_active(DtNmosNode* node, NmosConnection* c, NmosNodeSender* s,
                        NmosNodeReceiver* r, NmosParameters* staged)
{
    NmosOs_VersionNow(&node->last_version, staged->activation_time,
                      sizeof(staged->activation_time));
    if (!copy_parameters(&c->active, staged))
    {
        return;
    }
    if (s != NULL)
    {
        NmosLeg* t = &c->active.transport;
        if (t->destination_ip[0] != '\0' && strcmp(t->destination_ip, "auto") != 0)
        {
            snprintf(s->flow.DestinationIp, sizeof(s->flow.DestinationIp), "%s",
                     t->destination_ip);
        }
        if (t->destination_port >= 0)
        {
            s->flow.DestinationPort = (uint16_t)t->destination_port;
        }
        snprintf(t->destination_ip, sizeof(t->destination_ip), "%s",
                 s->flow.DestinationIp);
        t->destination_port = s->flow.DestinationPort;
        if (t->source_port < 0)
        {
            t->source_port = s->flow.DestinationPort;
        }
        s->master_enable = c->active.master_enable && t->rtp_enabled;
        snprintf(s->receiver_id.Text, sizeof(s->receiver_id.Text), "%s", staged->peer_id);
        ++s->session_version;
        NmosOs_VersionNow(&node->last_version, s->version, sizeof(s->version));
        s->registered = 0;
    }
    else
    {
        r->master_enable = c->active.master_enable && c->active.transport.rtp_enabled;
        snprintf(r->sender_id.Text, sizeof(r->sender_id.Text), "%s", staged->peer_id);
        NmosOs_VersionNow(&node->last_version, r->version, sizeof(r->version));
        r->registered = 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- patch_staged -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers a PATCH of the staged parameters of the sender or receiver id.
//
static void patch_staged(DtNmosNode* node, const char* id, int sender,
                         const DtNmosHttpRequest* request, DtNmosHttpResponse* response)
{
    NmosJson* body = NULL;
    if (request->Body == NULL ||
        NmosJson_Parse(request->Body, request->BodyLength, &body) != DTNMOS_OK)
    {
        NmosNode_AnswerError(response, 400, "The body of the PATCH is no JSON.");
        return;
    }
    NmosParameters staged;
    memset(&staged, 0, sizeof(staged));
    NmosActivation a;
    memset(&a, 0, sizeof(a));
    int activate = 0;
    int status = 404;
    const char* failure = "The node has no such sender or receiver.";

    NmosNode_Lock(node);
    NmosNodeSender* s = NULL;
    NmosNodeReceiver* r = NULL;
    NmosConnection* c = find_connection(node, id, sender, &s, &r);
    if (c != NULL)
    {
        status = 500;
        failure = "Out of memory.";
        if (copy_parameters(&staged, &c->staged))
        {
            failure = merge_patch(body, sender, &staged, &activate, &status);
        }
    }
    if (failure == NULL && activate)
    {
        status = 400;
        failure = gather_activation(s, r, &staged, &a);
    }
    if (failure == NULL && !copy_parameters(&c->staged, &staged))
    {
        status = 500;
        failure = "Out of memory.";
    }
    NmosNode_Unlock(node);
    NmosJson_Free(body);

    // The callback applies the activation without the lock, for as long as that takes. A
    // callback that fails leaves its message with DtNmos_SetLastError(), on this thread.
    NmosError_Clear();
    DtNmosResult result = DTNMOS_OK;
    if (failure == NULL && a.sender_callback != NULL)
    {
        result = a.sender_callback(a.user, &a.resource, &a.sender);
    }
    else if (failure == NULL && a.receiver_callback != NULL)
    {
        result = a.receiver_callback(a.user, &a.resource, &a.receiver);
    }
    clear_activation(&a);
    if (result != DTNMOS_OK)
    {
        status = 500;
        failure = DtNmos_GetLastError()[0] != '\0' ? DtNmos_GetLastError()
                                                   : "The activation failed.";
    }

    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    if (failure == NULL)
    {
        NmosNode_Lock(node);
        c = find_connection(node, id, sender, &s, &r);
        if (c == NULL)
        {
            status = 404;
            failure = "The sender or receiver was removed during its activation.";
        }
        else
        {
            if (activate)
            {
                make_active(node, c, s, r, &staged);
            }
            // The answer shows the activation that took place.
            write_parameters(&b, &staged, sender, activate);
        }
        NmosNode_Unlock(node);
    }
    free(staged.transport_file);
    if (failure != NULL)
    {
        NmosNode_AnswerError(response, status, failure);
    }
    else
    {
        NmosNode_AnswerJson(response, &b);
    }
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_transport_file -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers the transport file of the sender s.
//
static void answer_transport_file(const NmosNodeSender* s, DtNmosHttpResponse* response)
{
    NmosBuffer text;
    memset(&text, 0, sizeof(text));
    if (NmosNode_WriteTransportFile(s, &text) != DTNMOS_OK)
    {
        NmosBuffer_Free(&text);
        NmosNode_AnswerError(response, 500, DtNmos_GetLastError());
        return;
    }
    DtNmosHttpResponse_SetStatus(response, 200);
    DtNmosHttpResponse_SetBody(response, "application/sdp", text.data, text.length);
    NmosBuffer_Free(&text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_ids -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers the IDs of the senders or the receivers, each followed by a slash.
//
static void answer_ids(const DtNmosNode* node, int sender, DtNmosHttpResponse* response)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    DTNMOS_APPEND_LITERAL(&b, "[");
    const size_t count = sender ? node->sender_count : node->receiver_count;
    for (size_t i = 0; i < count; ++i)
    {
        NmosBuffer_Printf(&b, "%s\"%s/\"", i == 0 ? "" : ", ",
                          sender ? node->senders[i].id.Text : node->receivers[i].id.Text);
    }
    DTNMOS_APPEND_LITERAL(&b, "]");
    NmosNode_AnswerJson(response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_single -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers a GET of the single interface, whose further segments are segments; the caller
// holds the lock.
//
static void answer_single(DtNmosNode* node, char** segments, size_t count,
                          DtNmosHttpResponse* response)
{
    const int sender = count >= 1 && strcmp(segments[0], "senders") == 0;
    const int receiver = count >= 1 && strcmp(segments[0], "receivers") == 0;
    if (count == 0)
    {
        answer_text(response, "[\"senders/\", \"receivers/\"]");
        return;
    }
    if ((!sender && !receiver) || count > 3)
    {
        NmosNode_AnswerError(response, 404, "The Connection API has no such resource.");
        return;
    }
    if (count == 1)
    {
        answer_ids(node, sender, response);
        return;
    }
    NmosNodeSender* s = NULL;
    NmosNodeReceiver* r = NULL;
    NmosConnection* c = find_connection(node, segments[1], sender, &s, &r);
    if (c == NULL)
    {
        NmosNode_AnswerError(response, 404, "The node has no such sender or receiver.");
        return;
    }
    const char* leaf = count == 3 ? segments[2] : "";
    if (count == 2)
    {
        answer_text(response, sender ? "[\"constraints/\", \"staged/\", \"active/\", "
                                       "\"transportfile/\", \"transporttype/\"]"
                                     : "[\"constraints/\", \"staged/\", \"active/\", "
                                       "\"transporttype/\"]");
    }
    else if (strcmp(leaf, "constraints") == 0)
    {
        answer_text(response, sender ? sender_constraints : receiver_constraints);
    }
    else if (strcmp(leaf, "transporttype") == 0)
    {
        answer_text(response, "\"urn:x-nmos:transport:rtp\"");
    }
    else if (strcmp(leaf, "staged") == 0 || strcmp(leaf, "active") == 0)
    {
        const int active = strcmp(leaf, "active") == 0;
        NmosBuffer b;
        memset(&b, 0, sizeof(b));
        write_parameters(&b, active ? &c->active : &c->staged, sender, active);
        NmosNode_AnswerJson(response, &b);
        NmosBuffer_Free(&b);
    }
    else if (sender && strcmp(leaf, "transportfile") == 0)
    {
        answer_transport_file(s, response);
    }
    else
    {
        NmosNode_AnswerError(response, 404, "The Connection API has no such resource.");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosConnection_Handle(DtNmosNode* node, const DtNmosHttpRequest* request,
                                   char** segments, size_t count,
                                   DtNmosHttpResponse* response)
{
    const int single = count >= 1 && strcmp(segments[0], "single") == 0;
    const int bulk = count >= 1 && strcmp(segments[0], "bulk") == 0;
    if (strcmp(request->Method, "PATCH") == 0)
    {
        if (single && count == 4 && strcmp(segments[3], "staged") == 0 &&
            (strcmp(segments[1], "senders") == 0 ||
             strcmp(segments[1], "receivers") == 0))
        {
            patch_staged(node, segments[2], strcmp(segments[1], "senders") == 0, request,
                         response);
        }
        else
        {
            NmosNode_AnswerError(response, 405, "Only staged parameters take a PATCH.");
        }
        return DTNMOS_OK;
    }
    if (bulk && count == 2 && strcmp(request->Method, "POST") == 0)
    {
        NmosNode_AnswerError(response, 501,
                             "The node does not implement bulk activation.");
        return DTNMOS_OK;
    }
    if (strcmp(request->Method, "GET") != 0 && strcmp(request->Method, "HEAD") != 0)
    {
        NmosNode_AnswerError(response, 405, "The Connection API answers GET and PATCH.");
        return DTNMOS_OK;
    }
    NmosNode_Lock(node);
    if (count == 0)
    {
        answer_text(response, "[\"bulk/\", \"single/\"]");
    }
    else if (single)
    {
        answer_single(node, segments + 1, count - 1, response);
    }
    else if (bulk && count == 1)
    {
        answer_text(response, "[\"senders/\", \"receivers/\"]");
    }
    else if (bulk && count == 2)
    {
        NmosNode_AnswerError(response, 405, "The bulk interface takes a POST.");
    }
    else
    {
        NmosNode_AnswerError(response, 404, "The Connection API has no such resource.");
    }
    NmosNode_Unlock(node);
    return DTNMOS_OK;
}
