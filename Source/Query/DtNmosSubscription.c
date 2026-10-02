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

// A subscription is allocated empty and closed; DtNmosSubscription_Open() fills it in,
// and DtNmosSubscription_Close() empties it again.
struct DtNmosSubscription
{
    bool Open;
    DtNmosWebSocketTransport Websocket;
    void* Connection;
    char* Url; // of the WebSocket
    DtNmosChangeFunc OnChange;
    void* OnChangeUser;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosChangeKind_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosChangeKind_Name(DtNmosChangeKind Kind)
{
    switch (Kind)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosSubscription* DtNmosSubscription_Alloc(void)
{
    return calloc(1, sizeof(DtNmosSubscription));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSubscription_Open(DtNmosSubscription* Subscription, DtNmosQuery* Query,
                                     const DtNmosSubscriptionConfig* Config)
{
    if (Subscription == NULL || Config == NULL || Config->ResourcePath == NULL ||
        Config->ResourcePath[0] != '/' || Config->OnChange == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A subscription needs a resource path such as \"/senders\" "
                              "and a function for the changes.");
    }
    if (Subscription->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "The subscription is open already; close it first.");
    }
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosSubscription_Open");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(Config, DtNmosSubscriptionConfig,
                                                 sizeof(DtNmosSubscriptionConfig));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    if (Config->WebSocket != NULL)
    {
        const DtNmosResult TransportSized =
            DTNMOS_CHECK_SIZE(Config->WebSocket, DtNmosWebSocketTransport,
                              sizeof(DtNmosWebSocketTransport));
        if (TransportSized != DTNMOS_OK)
        {
            return TransportSized;
        }
    }
    const DtNmosWebSocketTransport* Websocket =
        Config->WebSocket != NULL ? Config->WebSocket : DtNmos_CurlWebSocket();
    const char* Base = NmosQuery_Base(Query);

    // Ask for the subscription: not persistent, so that it goes with its WebSocket, and
    // secure when the registry is asked over https.
    NmosBuffer Body;
    memset(&Body, 0, sizeof(Body));
    NmosBuffer_Printf(
        &Body, "{\"max_update_rate_ms\": %u, \"resource_path\": ",
        (unsigned)(Config->MaxUpdateRateMs == 0 ? 100 : Config->MaxUpdateRateMs));
    NmosJson_WriteString(&Body, Config->ResourcePath);
    NmosBuffer_Printf(&Body, ", \"params\": {}, \"persist\": false, \"secure\": %s}",
                      strncmp(Base, "https:", 6) == 0 ? "true" : "false");
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    NmosBuffer_Printf(&Url, "%ssubscriptions", Base);
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    DtNmosResult Result = DTNMOS_OK;
    if (Body.Failed || Url.Failed || Response == NULL)
    {
        Result = NmosError_FailMemory();
    }
    else
    {
        Result = NmosQuery_Request(Query, "POST", Url.Data, "application/json", Body.Data,
                                   Body.Length, Response);
    }
    NmosJson* Answer = NULL;
    if (Result == DTNMOS_OK)
    {
        const int Status = DtNmosHttpResponse_Status(Response);
        size_t Length = 0;
        const char* Text = DtNmosHttpResponse_Body(Response, &Length);
        if (Status != 200 && Status != 201)
        {
            Result = NmosError_Fail(
                DTNMOS_E_HTTP, "The registry answered the subscription to %s with %d.",
                Config->ResourcePath, Status);
        }
        else if (NmosJson_Parse(Text, Length, &Answer) != DTNMOS_OK ||
                 NmosJson_MemberText(Answer, "ws_href") == NULL)
        {
            Result =
                NmosError_Fail(DTNMOS_E_PARSE,
                               "The registry answered the subscription to %s without "
                               "the ws_href of its WebSocket.",
                               Config->ResourcePath);
        }
    }
    DtNmosSubscription* Made = NULL;
    if (Result == DTNMOS_OK)
    {
        const char* Href = NmosJson_MemberText(Answer, "ws_href");
        Made = Subscription;
        const size_t Length = strlen(Href);
        if ((Made->Url = malloc(Length + 1)) == NULL)
        {
            Result = NmosError_FailMemory();
        }
        else
        {
            memcpy(Made->Url, Href, Length + 1);
            Made->Websocket = *Websocket;
            Made->OnChange = Config->OnChange;
            Made->OnChangeUser = Config->OnChangeUser;
            Result = Websocket->Connect(Websocket->User, Made->Url,
                                        NmosQuery_Timeout(Query), &Made->Connection);
        }
    }
    NmosJson_Free(Answer);
    DtNmosHttpResponse_Free(Response);
    NmosBuffer_Free(&Url);
    NmosBuffer_Free(&Body);
    if (Result != DTNMOS_OK)
    {
        if (Made != NULL)
        {
            free(Made->Url);
            memset(Made, 0, sizeof(*Made));
        }
        return Result;
    }
    Made->Open = true;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReportChange -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reports one item of the data of a grain, {"path", "pre", "post"}; an item with neither
// is skipped.
//
static DtNmosResult ReportChange(DtNmosSubscription* Subscription, const NmosJson* Item)
{
    const NmosJson* Pre = NmosJson_Member(Item, "pre");
    const NmosJson* Post = NmosJson_Member(Item, "post");
    const bool HasPre = Pre != NULL && Pre->Type == DTNMOS_JSON_OBJECT;
    const bool HasPost = Post != NULL && Post->Type == DTNMOS_JSON_OBJECT;
    if (!HasPre && !HasPost)
    {
        return DTNMOS_OK;
    }
    NmosBuffer Before;
    NmosBuffer After;
    memset(&Before, 0, sizeof(Before));
    memset(&After, 0, sizeof(After));
    if (HasPre)
    {
        NmosJson_Write(&Before, Pre);
    }
    if (HasPost)
    {
        NmosJson_Write(&After, Post);
    }
    if (Before.Failed || After.Failed)
    {
        NmosBuffer_Free(&Before);
        NmosBuffer_Free(&After);
        return NmosError_FailMemory();
    }
    DtNmosChange Change;
    memset(&Change, 0, sizeof(Change));
    const char* Path = NmosJson_MemberText(Item, "path");
    Change.Id = Path != NULL ? Path : "";
    Change.Pre = HasPre ? Before.Data : NULL;
    Change.PreLength = HasPre ? Before.Length : 0;
    Change.Post = HasPost ? After.Data : NULL;
    Change.PostLength = HasPost ? After.Length : 0;
    if (HasPre && HasPost)
    {
        // The first message gives each resource as it is, before and after the same.
        Change.Kind = Before.Length == After.Length &&
                              memcmp(Before.Data, After.Data, Before.Length) == 0
                          ? DTNMOS_CHANGE_PRESENT
                          : DTNMOS_CHANGE_MODIFIED;
    }
    else
    {
        Change.Kind = HasPost ? DTNMOS_CHANGE_ADDED : DTNMOS_CHANGE_REMOVED;
    }
    Subscription->OnChange(Subscription->OnChangeUser, &Change);
    NmosBuffer_Free(&Before);
    NmosBuffer_Free(&After);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSubscription_Poll(DtNmosSubscription* Subscription, uint32_t TimeoutMs)
{
    if (Subscription == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosSubscription_Poll() needs a subscription.");
    }
    if (!Subscription->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "DtNmosSubscription_Poll() needs an open subscription.");
    }
    const char* Message = NULL;
    size_t Length = 0;
    DtNmosResult Result = Subscription->Websocket.Receive(Subscription->Websocket.User,
                                                          Subscription->Connection,
                                                          TimeoutMs, &Message, &Length);
    if (Result != DTNMOS_OK)
    {
        return Result;
    }
    // A grain: {"grain_type": "event", ..., "grain": {"topic": "/senders/",
    // "data": [{"path": ..., "pre": ..., "post": ...}]}}.
    NmosJson* Json = NULL;
    const NmosJson* Data = NULL;
    if (NmosJson_Parse(Message, Length, &Json) == DTNMOS_OK)
    {
        Data = NmosJson_Member(NmosJson_Member(Json, "grain"), "data");
    }
    if (Data == NULL || Data->Type != DTNMOS_JSON_ARRAY)
    {
        NmosJson_Free(Json);
        return NmosError_Fail(DTNMOS_E_PARSE,
                              "A message of the WebSocket %s is no grain with data.",
                              Subscription->Url);
    }
    for (size_t i = 0; Result == DTNMOS_OK && i < Data->Count; ++i)
    {
        Result = ReportChange(Subscription, &Data->Items[i]);
    }
    NmosJson_Free(Json);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosSubscription_Url(const DtNmosSubscription* Subscription)
{
    return Subscription == NULL || !Subscription->Open ? "" : Subscription->Url;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosSubscription_Close(DtNmosSubscription* Subscription)
{
    if (Subscription == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosSubscription_Close() needs a subscription.");
    }
    if (!Subscription->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "DtNmosSubscription_Close() needs an open subscription.");
    }
    Subscription->Websocket.Close(Subscription->Websocket.User, Subscription->Connection);
    free(Subscription->Url);
    memset(Subscription, 0, sizeof(*Subscription));
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosSubscription_Free(DtNmosSubscription* Subscription)
{
    if (Subscription == NULL)
    {
        return;
    }
    if (Subscription->Open)
    {
        DtNmosSubscription_Close(Subscription);
    }
    free(Subscription);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSubscription_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosSubscription_Freep(DtNmosSubscription** Subscription)
{
    if (Subscription != NULL)
    {
        DtNmosSubscription_Free(*Subscription);
        *Subscription = NULL;
    }
}
