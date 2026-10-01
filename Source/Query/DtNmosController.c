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
    DtNmosReceiverList* Receiver;
    DtNmosSenderList* Sender;
    char* Sdp;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosConnection_Free(DtNmosConnection* Connection)
{
    if (Connection == NULL)
    {
        return;
    }
    DtNmosReceiverList_Free(Connection->Receiver);
    DtNmosSenderList_Free(Connection->Sender);
    free(Connection->Sdp);
    free(Connection);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosReceiverInfo* DtNmosConnection_Receiver(const DtNmosConnection* Connection)
{
    return Connection == NULL ? NULL : DtNmosReceiverList_At(Connection->Receiver, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Sdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosConnection_Sdp(const DtNmosConnection* Connection)
{
    return Connection == NULL ? NULL : Connection->Sdp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosConnection_Sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosSenderInfo* DtNmosConnection_Sender(const DtNmosConnection* Connection)
{
    return Connection == NULL ? NULL : DtNmosSenderList_At(Connection->Sender, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- KindOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the kind of media, as the format of IS-04 names it, or null for another one.
//
static const char* KindOf(DtNmosMedia Media)
{
    switch (Media)
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
typedef struct NmosStagedResource
{
    const char* Noun; // "receiver" or "sender", as a message names it
    const char* Path; // "receivers" or "senders", in the URL of the Connection API
    const char* Id;
    const char* DeviceId;
    const char* Label;
} NmosStagedResource;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiverResource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static NmosStagedResource ReceiverResource(const DtNmosReceiverInfo* Receiver)
{
    const NmosStagedResource Resource = {"receiver", "receivers", Receiver->Id.Text,
                                         Receiver->DeviceId.Text, Receiver->Label};
    return Resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SenderResource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static NmosStagedResource SenderResource(const DtNmosSenderInfo* Sender)
{
    const NmosStagedResource Resource = {"sender", "senders", Sender->Id.Text,
                                         Sender->DeviceId.Text, Sender->Label};
    return Resource;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StagedUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes into url the URL of the staged parameters of resource, from the Connection API
// that the control of its device names.
//
static DtNmosResult StagedUrl(DtNmosQuery* Query, const NmosStagedResource* Resource,
                              NmosBuffer* Url)
{
    const char* Label = Resource->Label;
    if (Resource->DeviceId[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_NOT_FOUND,
                              "The %s %s ('%s') names no device, so it has no Connection "
                              "API to control it through.",
                              Resource->Noun, Resource->Id, Label);
    }
    NmosBuffer DeviceUrl;
    memset(&DeviceUrl, 0, sizeof(DeviceUrl));
    NmosBuffer_Printf(&DeviceUrl, "%sdevices/%s", NmosQuery_Base(Query),
                      Resource->DeviceId);
    if (DeviceUrl.Failed)
    {
        NmosBuffer_Free(&DeviceUrl);
        return NmosError_FailMemory();
    }
    NmosJson* Device = NULL;
    DtNmosResult Result = NmosQuery_GetJson(Query, DeviceUrl.Data, &Device);
    NmosBuffer_Free(&DeviceUrl);
    if (Result == DTNMOS_E_NOT_FOUND)
    {
        return NmosError_Fail(DTNMOS_E_NOT_FOUND,
                              "The registry has no device %s of %s %s ('%s').",
                              Resource->DeviceId, Resource->Noun, Resource->Id, Label);
    }
    if (Result != DTNMOS_OK)
    {
        return Result;
    }
    const char* Href = NULL;
    const NmosJson* Controls = NmosJson_Member(Device, "controls");
    for (size_t i = 0; Href == NULL && Controls != NULL &&
                       Controls->Type == DTNMOS_JSON_ARRAY && i < Controls->Count;
         ++i)
    {
        const char* Type = NmosJson_MemberText(&Controls->Items[i], "type");
        const char* Candidate = NmosJson_MemberText(&Controls->Items[i], "href");
        if (Type != NULL && strcmp(Type, DTNMOS_CONNECTION_CONTROL) == 0 &&
            Candidate != NULL && Candidate[0] != '\0')
        {
            Href = Candidate;
        }
    }
    if (Href == NULL)
    {
        Result =
            NmosError_Fail(DTNMOS_E_NOT_FOUND,
                           "Device %s of %s %s ('%s') has no control %s, so it has no "
                           "Connection API to control it through.",
                           Resource->DeviceId, Resource->Noun, Resource->Id, Label,
                           DTNMOS_CONNECTION_CONTROL);
    }
    else
    {
        const size_t Length = strlen(Href);
        NmosBuffer_Printf(Url, "%s%ssingle/%s/%s/staged", Href,
                          Href[Length - 1] == '/' ? "" : "/", Resource->Path,
                          Resource->Id);
        if (Url->Failed)
        {
            Result = NmosError_FailMemory();
        }
    }
    NmosJson_Free(Device);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Patch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends body as a PATCH of the staged parameters of resource; fails unless the node
// answers with 200, naming the error the node gave.
//
static DtNmosResult Patch(DtNmosQuery* Query, const NmosStagedResource* Resource,
                          const NmosBuffer* Body)
{
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    DtNmosResult Result = StagedUrl(Query, Resource, &Url);
    if (Result != DTNMOS_OK)
    {
        NmosBuffer_Free(&Url);
        return Result;
    }
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    if (Response == NULL)
    {
        NmosBuffer_Free(&Url);
        return NmosError_FailMemory();
    }
    Result = NmosQuery_Request(Query, "PATCH", Url.Data, "application/json", Body->Data,
                               Body->Length, Response);
    const int Status = DtNmosHttpResponse_Status(Response);
    if (Result == DTNMOS_OK && Status != 200)
    {
        // The Connection API answers an error with {"code", "error", "debug"}.
        size_t Length = 0;
        const char* Text = DtNmosHttpResponse_Body(Response, &Length);
        NmosJson* Answer = NULL;
        const char* Reason = NULL;
        if (NmosJson_Parse(Text, Length, &Answer) == DTNMOS_OK)
        {
            Reason = NmosJson_MemberText(Answer, "error");
        }
        Result = NmosError_Fail(
            DTNMOS_E_HTTP,
            "The node of %s %s ('%s') answered the PATCH of %s with %d: "
            "%s",
            Resource->Noun, Resource->Id, Resource->Label, Url.Data, Status,
            Reason != NULL ? Reason : (Length > 0 ? Text : "no body"));
        NmosJson_Free(Answer);
    }
    DtNmosHttpResponse_Free(Response);
    NmosBuffer_Free(&Url);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Connect(DtNmosQuery* Query, const char* Receiver,
                                 const char* Sender, DtNmosConnection** Connection)
{
    if (Connection != NULL)
    {
        *Connection = NULL;
    }
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_Connect");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || Receiver == NULL || Receiver[0] == '\0' || Sender == NULL ||
        Sender[0] == '\0')
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Connect() needs a query, a receiver and a sender.");
    }
    DtNmosConnection* Found = calloc(1, sizeof(*Found));
    if (Found == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult Result = DtNmosQuery_FindReceiver(Query, Receiver, &Found->Receiver);
    if (Result == DTNMOS_OK)
    {
        Result = DtNmosQuery_FindSender(Query, Sender, &Found->Sender);
    }
    const DtNmosReceiverInfo* TakesInfo = DtNmosConnection_Receiver(Found);
    const DtNmosSenderInfo* GivesInfo = DtNmosConnection_Sender(Found);
    if (Result == DTNMOS_OK)
    {
        // A kind that is not known on either side is left to the node.
        const char* Takes = KindOf(TakesInfo->Media);
        const char* Gives = KindOf(GivesInfo->Media);
        if (Takes != NULL && Gives != NULL && strcmp(Takes, Gives) != 0)
        {
            Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                    "Sender %s ('%s') sends %s, but receiver %s ('%s') "
                                    "takes %s.",
                                    GivesInfo->Id.Text, GivesInfo->Label, Gives,
                                    TakesInfo->Id.Text, TakesInfo->Label, Takes);
        }
    }
    if (Result == DTNMOS_OK)
    {
        // The SDP the receiver is given is kept, for the caller to see what it got.
        DtNmosHttpResponse* Response = NULL;
        Result = NmosQuery_Manifest(Query, GivesInfo, &Response);
        if (Result == DTNMOS_OK)
        {
            size_t Length = 0;
            const char* Body = DtNmosHttpResponse_Body(Response, &Length);
            Found->Sdp = malloc(Length + 1);
            if (Found->Sdp == NULL)
            {
                Result = NmosError_FailMemory();
            }
            else
            {
                memcpy(Found->Sdp, Body, Length);
                Found->Sdp[Length] = '\0';
            }
        }
        DtNmosHttpResponse_Free(Response);
    }
    if (Result == DTNMOS_OK)
    {
        NmosBuffer Body;
        memset(&Body, 0, sizeof(Body));
        DTNMOS_APPEND_LITERAL(&Body, "{\"sender_id\": ");
        NmosJson_WriteString(&Body, GivesInfo->Id.Text);
        DTNMOS_APPEND_LITERAL(&Body, ", \"master_enable\": true, \"activation\": "
                                     "{\"mode\": \"activate_immediate\"}, "
                                     "\"transport_file\": {\"type\": "
                                     "\"application/sdp\", \"data\": ");
        NmosJson_WriteString(&Body, Found->Sdp);
        DTNMOS_APPEND_LITERAL(&Body, "}}");
        const NmosStagedResource Resource = ReceiverResource(TakesInfo);
        Result = Body.Failed ? NmosError_FailMemory() : Patch(Query, &Resource, &Body);
        NmosBuffer_Free(&Body);
    }
    if (Result == DTNMOS_OK && Connection != NULL)
    {
        *Connection = Found;
        return DTNMOS_OK;
    }
    DtNmosConnection_Free(Found);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Disconnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_Disconnect(DtNmosQuery* Query, const char* Receiver,
                                    DtNmosReceiverList** Disconnected)
{
    if (Disconnected != NULL)
    {
        *Disconnected = NULL;
    }
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_Disconnect");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || Receiver == NULL || Receiver[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosQuery_Disconnect() needs a query and a receiver.");
    }
    DtNmosReceiverList* Found = NULL;
    DtNmosResult Result = DtNmosQuery_FindReceiver(Query, Receiver, &Found);
    if (Result == DTNMOS_OK)
    {
        NmosBuffer Body;
        memset(&Body, 0, sizeof(Body));
        DTNMOS_APPEND_LITERAL(&Body, "{\"sender_id\": null, \"master_enable\": false, "
                                     "\"activation\": {\"mode\": "
                                     "\"activate_immediate\"}}");
        const NmosStagedResource Resource =
            ReceiverResource(DtNmosReceiverList_At(Found, 0));
        Result = Body.Failed ? NmosError_FailMemory() : Patch(Query, &Resource, &Body);
        NmosBuffer_Free(&Body);
    }
    if (Result == DTNMOS_OK && Disconnected != NULL)
    {
        *Disconnected = Found;
        return DTNMOS_OK;
    }
    DtNmosReceiverList_Free(Found);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_MoveSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_MoveSender(DtNmosQuery* Query, const char* Sender,
                                    const char* DestinationIp, uint16_t DestinationPort,
                                    DtNmosSenderList** Moved)
{
    if (Moved != NULL)
    {
        *Moved = NULL;
    }
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_MoveSender");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || Sender == NULL || Sender[0] == '\0' || DestinationIp == NULL ||
        DestinationIp[0] == '\0' || DestinationPort == 0)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_MoveSender() needs a query, a sender, and an IP "
            "address and a UDP port to move it to.");
    }
    DtNmosSenderList* Found = NULL;
    DtNmosResult Result = DtNmosQuery_FindSender(Query, Sender, &Found);
    const DtNmosSenderInfo* Info = DtNmosSenderList_At(Found, 0);
    if (Result == DTNMOS_OK && Info->Transport[0] != '\0' &&
        strncmp(Info->Transport, "urn:x-nmos:transport:rtp", 24) != 0)
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                "Sender %s ('%s') sends over %s, not RTP, so it has no "
                                "destination IP address and port to move.",
                                Info->Id.Text, Info->Label, Info->Transport);
    }
    if (Result == DTNMOS_OK)
    {
        NmosBuffer Body;
        memset(&Body, 0, sizeof(Body));
        DTNMOS_APPEND_LITERAL(&Body, "{\"transport_params\": [{\"destination_ip\": ");
        NmosJson_WriteString(&Body, DestinationIp);
        NmosBuffer_Printf(&Body,
                          ", \"destination_port\": %u}], \"activation\": "
                          "{\"mode\": \"activate_immediate\"}}",
                          (unsigned)DestinationPort);
        const NmosStagedResource Resource = SenderResource(Info);
        Result = Body.Failed ? NmosError_FailMemory() : Patch(Query, &Resource, &Body);
        NmosBuffer_Free(&Body);
    }
    if (Result == DTNMOS_OK && Moved != NULL)
    {
        *Moved = Found;
        return DTNMOS_OK;
    }
    DtNmosSenderList_Free(Found);
    return Result;
}
