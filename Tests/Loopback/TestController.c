// #*#*#*#*#*#*#*#*#*#*#*#*#* TestController.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the receivers of a registry, and of the controller that connects them
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "check.h"
#include "tests.h"

#define BASE "http://registry.test/x-nmos/query/v1.3/"
#define MONITOR_ID "44444444-4444-4444-8444-444444444444"
#define SPEAKER_ID "66666666-6666-4666-8666-666666666666"
#define DEVICE_ID "55555555-5555-4555-8555-555555555555"
#define BARE_DEVICE_ID "77777777-7777-4777-8777-777777777777"
#define CAMERA_ID "11111111-1111-4111-8111-111111111111"
#define DARK_ID "22222222-2222-4222-8222-222222222222"
#define VIDEO_FLOW "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
#define ENCODER_ID "88888888-8888-4888-8888-888888888888"
#define PLAYER_ID "99999999-9999-4999-8999-999999999999"
#define MOVED                                                                            \
    "http://node.test/x-nmos/connection/v1.1/single/senders/" ENCODER_ID "/staged"
#define STAGED                                                                           \
    "http://node.test/x-nmos/connection/v1.1/single/receivers/" MONITOR_ID "/staged"

#define CAMERA_SDP                                                                       \
    "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=camera 1\r\nt=0 0\r\n"                          \
    "m=video 5004 RTP/AVP 96\r\nc=IN IP4 239.0.0.1/64\r\na=rtpmap:96 raw/90000\r\n"

// A video receiver on a device with a Connection API, and an audio receiver, subscribed
// to the camera, on a device without one.
#define MONITOR                                                                          \
    "{\"id\": \"" MONITOR_ID "\", \"label\": \"monitor\", \"device_id\": \"" DEVICE_ID   \
    "\", \"format\": \"urn:x-nmos:format:video\", \"transport\": "                       \
    "\"urn:x-nmos:transport:rtp\", \"caps\": {\"media_types\": [\"video/raw\"]}, "       \
    "\"subscription\": {\"sender_id\": null, \"active\": false}}"
#define SPEAKER                                                                          \
    "{\"id\": \"" SPEAKER_ID "\", \"label\": \"speaker\", \"description\": \"booth\", "  \
    "\"device_id\": \"" BARE_DEVICE_ID "\", \"format\": \"urn:x-nmos:format:audio\", "   \
    "\"caps\": {\"media_types\": [\"audio/L24\"]}, "                                     \
    "\"subscription\": {\"sender_id\": \"" CAMERA_ID "\", \"active\": true}}"
// A video sender with an SDP, and one without.
#define CAMERA                                                                           \
    "{\"id\": \"" CAMERA_ID "\", \"label\": \"camera 1\", \"flow_id\": \"" VIDEO_FLOW    \
    "\", \"manifest_href\": \"http://camera.test/video.sdp\"}"
#define DARK                                                                             \
    "{\"id\": \"" DARK_ID "\", \"label\": \"camera 2\", \"flow_id\": \"" VIDEO_FLOW      \
    "\", \"manifest_href\": null}"

// A registry and a node that the test answers for, which records what it was asked.
typedef struct NmosFakeNetwork
{
    char Requests[16][256]; // "<method> <url>"
    size_t Count;
    char ContentType[64]; // of the last PATCH
    char* Body;           // of the last PATCH
    int PatchStatus;      // what the node answers a PATCH
    const char* PatchAnswer;
    int NodeUnreachable;
} NmosFakeNetwork;

typedef struct NmosFakeRoute
{
    const char* Url;
    const char* Body;
} NmosFakeRoute;

static const NmosFakeRoute FakeRoutes[] = {
    {BASE "receivers?paging.limit=100", "[" MONITOR ", " SPEAKER "]"},
    {BASE "receivers?label=monitor&paging.limit=100", "[" MONITOR "]"},
    {BASE "receivers?label=speaker&paging.limit=100", "[" SPEAKER "]"},
    {BASE "receivers?label=nobody&paging.limit=100", "[]"},
    {BASE "receivers/" MONITOR_ID, MONITOR},
    {BASE "senders?label=camera%201&paging.limit=100", "[" CAMERA "]"},
    {BASE "senders?label=camera%202&paging.limit=100", "[" DARK "]"},
    // A sender of RTP on the device with a Connection API, and one of WebSocket.
    {BASE "senders?label=encoder&paging.limit=100",
     "[{\"id\": \"" ENCODER_ID "\", \"label\": \"encoder\", \"device_id\": \"" DEVICE_ID
     "\", \"transport\": \"urn:x-nmos:transport:rtp.mcast\"}]"},
    {BASE "senders?label=player&paging.limit=100",
     "[{\"id\": \"" PLAYER_ID "\", \"label\": \"player\", \"device_id\": \"" DEVICE_ID
     "\", \"transport\": \"urn:x-nmos:transport:websocket\"}]"},
    {BASE "flows/" VIDEO_FLOW,
     "{\"id\": \"" VIDEO_FLOW "\", \"format\": \"urn:x-nmos:format:video\", "
     "\"media_type\": \"video/raw\"}"},
    // The Connection API of v1.0 comes first, and the one of v1.1 has no final slash.
    {BASE "devices/" DEVICE_ID, "{\"id\": \"" DEVICE_ID "\", \"controls\": [{\"href\": "
                                "\"http://node.test/x-nmos/connection/v1.0/\", \"type\": "
                                "\"urn:x-nmos:control:sr-ctrl/v1.0\"}, {\"href\": "
                                "\"http://node.test/x-nmos/connection/v1.1\", \"type\": "
                                "\"urn:x-nmos:control:sr-ctrl/v1.1\"}]}"},
    {BASE "devices/" BARE_DEVICE_ID,
     "{\"id\": \"" BARE_DEVICE_ID "\", \"controls\": []}"},
    {"http://camera.test/video.sdp", CAMERA_SDP},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeAnswer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult FakeAnswer(void* User, const DtNmosHttpRequest* Request,
                               DtNmosHttpResponse* Response)
{
    NmosFakeNetwork* Network = User;
    CHECK_EQ(Request->TimeoutMs, 2000);
    if (Network->Count < 16)
    {
        snprintf(Network->Requests[Network->Count++], sizeof(Network->Requests[0]),
                 "%s %s", Request->Method, Request->Url);
    }
    if (strcmp(Request->Method, "PATCH") == 0)
    {
        if (Network->NodeUnreachable)
        {
            return DtNmos_SetLastError(DTNMOS_E_HTTP, "connection refused");
        }
        snprintf(Network->ContentType, sizeof(Network->ContentType), "%s",
                 Request->ContentType == NULL ? "" : Request->ContentType);
        free(Network->Body);
        Network->Body = malloc(Request->BodyLength + 1);
        memcpy(Network->Body, Request->Body, Request->BodyLength);
        Network->Body[Request->BodyLength] = '\0';
        DtNmosHttpResponse_SetStatus(Response, Network->PatchStatus);
        const char* Answer = Network->PatchAnswer;
        DtNmosHttpResponse_SetBody(Response, "application/json", Answer, strlen(Answer));
        return DTNMOS_OK;
    }
    CHECK_STR(Request->Method, "GET");
    for (size_t i = 0; i < sizeof(FakeRoutes) / sizeof(FakeRoutes[0]); ++i)
    {
        if (strcmp(FakeRoutes[i].Url, Request->Url) == 0)
        {
            DtNmosHttpResponse_SetStatus(Response, 200);
            DtNmosHttpResponse_SetBody(Response, "application/json", FakeRoutes[i].Body,
                                       strlen(FakeRoutes[i].Body));
            return DTNMOS_OK;
        }
    }
    printf("  no route for %s\n", Request->Url);
    DtNmosHttpResponse_SetStatus(Response, 404);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeNetwork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets up network, whose node accepts a PATCH, and returns a query of its registry.
//
static DtNmosQuery* MakeNetwork(NmosFakeNetwork* Network)
{
    memset(Network, 0, sizeof(*Network));
    Network->PatchStatus = 200;
    Network->PatchAnswer = "{}";
    DtNmosQueryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.RegistryUrl = "http://registry.test";
    Config.Http = FakeAnswer;
    Config.HttpUser = Network;
    Config.TimeoutMs = 2000;
    DtNmosQuery* Query = DtNmosQuery_Alloc();
    if (Query == NULL || DtNmosQuery_Open(Query, &Config) != DTNMOS_OK)
    {
        DtNmosQuery_Freep(&Query);
    }
    return Query;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CountPatches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t CountPatches(const NmosFakeNetwork* Network)
{
    size_t Patches = 0;
    for (size_t i = 0; i < Network->Count; ++i)
    {
        Patches += strncmp(Network->Requests[i], "PATCH ", 6) == 0 ? 1 : 0;
    }
    return Patches;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- query_lists_and_finds_receivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void query_lists_and_finds_receivers(void)
{
    NmosFakeNetwork Network;
    DtNmosQuery* Query = MakeNetwork(&Network);
    REQUIRE(Query != NULL);
    DtNmosReceiverList* List = NULL;
    REQUIRE(DtNmosQuery_Receivers(Query, &List) == DTNMOS_OK);
    REQUIRE(DtNmosReceiverList_Count(List) == 2);
    const DtNmosReceiverInfo* Monitor = DtNmosReceiverList_At(List, 0);
    CHECK_STR(Monitor->Id.Text, MONITOR_ID);
    CHECK_STR(Monitor->DeviceId.Text, DEVICE_ID);
    CHECK_STR(Monitor->Label, "monitor");
    CHECK_EQ(Monitor->Media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(Monitor->Transport, "urn:x-nmos:transport:rtp");
    CHECK_STR(Monitor->SenderId.Text, "");
    CHECK_EQ(Monitor->Active, 0);
    const DtNmosReceiverInfo* Speaker = DtNmosReceiverList_At(List, 1);
    CHECK_EQ(Speaker->Media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(Speaker->Description, "booth");
    CHECK_STR(Speaker->SenderId.Text, CAMERA_ID);
    CHECK_EQ(Speaker->Active, 1);
    CHECK(DtNmosReceiverList_At(List, 2) == NULL);

    // By ID and by label, each in a list of one that owns its strings.
    const char* const Keys[] = {MONITOR_ID, "monitor"};
    for (size_t k = 0; k < 2; ++k)
    {
        DtNmosReceiverList* Found = NULL;
        REQUIRE(DtNmosQuery_FindReceiver(Query, Keys[k], &Found) == DTNMOS_OK);
        REQUIRE(DtNmosReceiverList_Count(Found) == 1);
        const DtNmosReceiverInfo* Receiver = DtNmosReceiverList_At(Found, 0);
        CHECK_STR(Receiver->Id.Text, MONITOR_ID);
        CHECK_EQ(Receiver->Media, DTNMOS_MEDIA_VIDEO);
        CHECK_STR(Receiver->Label, "monitor");
        DtNmosReceiverList_Free(Found);
    }
    DtNmosReceiverList* Missing = NULL;
    CHECK(DtNmosQuery_FindReceiver(Query, "nobody", &Missing) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no receiver labelled 'nobody'") != NULL);
    CHECK(Missing == NULL);
    DtNmosReceiverList_Free(List);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_connects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void controller_connects_a_receiver(void)
{
    NmosFakeNetwork Network;
    DtNmosQuery* Query = MakeNetwork(&Network);
    REQUIRE(Query != NULL);
    DtNmosConnection* Connection = NULL;
    const DtNmosResult Result =
        DtNmosQuery_Connect(Query, "monitor", "camera 1", &Connection);
    if (Result != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(Result == DTNMOS_OK);
    CHECK_STR(DtNmosConnection_Receiver(Connection)->Id.Text, MONITOR_ID);
    CHECK_STR(DtNmosConnection_Receiver(Connection)->Label, "monitor");
    CHECK_STR(DtNmosConnection_Sender(Connection)->Id.Text, CAMERA_ID);
    CHECK_STR(DtNmosConnection_Sdp(Connection), CAMERA_SDP);

    // The receiver and the sender, the flow of the sender, its SDP, the device of the
    // receiver, and the PATCH to the Connection API of v1.1 of that device.
    const char* const Expected[] = {
        "GET " BASE "receivers?label=monitor&paging.limit=100",
        "GET " BASE "senders?label=camera%201&paging.limit=100",
        "GET " BASE "flows/" VIDEO_FLOW,
        "GET http://camera.test/video.sdp",
        "GET " BASE "devices/" DEVICE_ID,
        "PATCH " STAGED,
    };
    REQUIRE(Network.Count == sizeof(Expected) / sizeof(Expected[0]));
    for (size_t i = 0; i < Network.Count; ++i)
    {
        CHECK_STR(Network.Requests[i], Expected[i]);
    }
    CHECK_STR(Network.ContentType, "application/json");
    NmosJson* Body = NULL;
    REQUIRE(NmosJson_Parse(Network.Body, strlen(Network.Body), &Body) == DTNMOS_OK);
    CHECK_STR(NmosJson_MemberText(Body, "sender_id"), CAMERA_ID);
    CHECK_EQ(NmosJson_Member(Body, "master_enable")->Type, DTNMOS_JSON_TRUE);
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(Body, "activation"), "mode"),
              "activate_immediate");
    const NmosJson* File = NmosJson_Member(Body, "transport_file");
    CHECK_STR(NmosJson_MemberText(File, "type"), "application/sdp");
    CHECK_STR(NmosJson_MemberText(File, "data"), CAMERA_SDP);
    NmosJson_Free(Body);
    free(Network.Body);
    DtNmosConnection_Free(Connection);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_disconnects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void controller_disconnects_a_receiver(void)
{
    NmosFakeNetwork Network;
    DtNmosQuery* Query = MakeNetwork(&Network);
    REQUIRE(Query != NULL);
    DtNmosReceiverList* Disconnected = NULL;
    REQUIRE(DtNmosQuery_Disconnect(Query, MONITOR_ID, &Disconnected) == DTNMOS_OK);
    REQUIRE(DtNmosReceiverList_Count(Disconnected) == 1);
    CHECK_STR(DtNmosReceiverList_At(Disconnected, 0)->Label, "monitor");
    REQUIRE(Network.Count == 3);
    CHECK_STR(Network.Requests[0], "GET " BASE "receivers/" MONITOR_ID);
    CHECK_STR(Network.Requests[1], "GET " BASE "devices/" DEVICE_ID);
    CHECK_STR(Network.Requests[2], "PATCH " STAGED);
    NmosJson* Body = NULL;
    REQUIRE(NmosJson_Parse(Network.Body, strlen(Network.Body), &Body) == DTNMOS_OK);
    CHECK_EQ(NmosJson_Member(Body, "sender_id")->Type, DTNMOS_JSON_NULL);
    CHECK_EQ(NmosJson_Member(Body, "master_enable")->Type, DTNMOS_JSON_FALSE);
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(Body, "activation"), "mode"),
              "activate_immediate");
    CHECK(NmosJson_Member(Body, "transport_file") == NULL);
    NmosJson_Free(Body);
    free(Network.Body);
    DtNmosReceiverList_Free(Disconnected);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_names_what_went_wrong -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void controller_names_what_went_wrong(void)
{
    NmosFakeNetwork Network;
    DtNmosQuery* Query = MakeNetwork(&Network);
    REQUIRE(Query != NULL);
    DtNmosConnection* Connection = NULL;

    // A receiver the registry does not have.
    CHECK(DtNmosQuery_Connect(Query, "nobody", "camera 1", &Connection) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no receiver labelled 'nobody'") != NULL);
    // Video to a receiver of audio, refused before the node is asked.
    CHECK(DtNmosQuery_Connect(Query, "speaker", "camera 1", &Connection) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "sends video, but receiver " SPEAKER_ID
                                        " ('speaker') takes audio") != NULL);
    // A sender without SDP.
    CHECK(DtNmosQuery_Connect(Query, "monitor", "camera 2", &Connection) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no manifest_href") != NULL);
    // A device without a Connection API.
    CHECK(DtNmosQuery_Disconnect(Query, "speaker", NULL) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(),
                 "has no control urn:x-nmos:control:sr-ctrl/v1.1") != NULL);
    CHECK(strstr(DtNmos_GetLastError(), BARE_DEVICE_ID) != NULL);
    CHECK_EQ(CountPatches(&Network), 0);

    // A node that refuses the PATCH, with the error of IS-05 and with a page of its own.
    Network.PatchStatus = 400;
    Network.PatchAnswer = "{\"code\": 400, \"error\": \"no such flow\", \"debug\": null}";
    CHECK(DtNmosQuery_Connect(Query, "monitor", "camera 1", &Connection) ==
          DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(),
                 "answered the PATCH of " STAGED " with 400: no such flow") != NULL);
    Network.PatchStatus = 500;
    Network.PatchAnswer = "<html>";
    CHECK(DtNmosQuery_Disconnect(Query, "monitor", NULL) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "with 500: <html>") != NULL);
    // A node that cannot be reached.
    Network.NodeUnreachable = 1;
    CHECK(DtNmosQuery_Disconnect(Query, "monitor", NULL) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "connection refused") != NULL);
    CHECK(Connection == NULL);
    free(Network.Body);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- controller_moves_a_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void controller_moves_a_sender(void)
{
    NmosFakeNetwork Network;
    DtNmosQuery* Query = MakeNetwork(&Network);
    REQUIRE(Query != NULL);
    DtNmosSenderList* Moved = NULL;
    const DtNmosResult Result =
        DtNmosQuery_MoveSender(Query, "encoder", "239.1.2.3", 5010, &Moved);
    if (Result != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(Result == DTNMOS_OK);
    REQUIRE(DtNmosSenderList_Count(Moved) == 1);
    CHECK_STR(DtNmosSenderList_At(Moved, 0)->Id.Text, ENCODER_ID);
    CHECK_STR(DtNmosSenderList_At(Moved, 0)->Label, "encoder");
    // The sender, the device of the sender, and the PATCH of its staged parameters.
    REQUIRE(Network.Count == 3);
    CHECK_STR(Network.Requests[0], "GET " BASE "senders?label=encoder&paging.limit=100");
    CHECK_STR(Network.Requests[1], "GET " BASE "devices/" DEVICE_ID);
    CHECK_STR(Network.Requests[2], "PATCH " MOVED);
    NmosJson* Body = NULL;
    REQUIRE(NmosJson_Parse(Network.Body, strlen(Network.Body), &Body) == DTNMOS_OK);
    const NmosJson* Legs = NmosJson_Member(Body, "transport_params");
    REQUIRE(Legs != NULL && Legs->Type == DTNMOS_JSON_ARRAY && Legs->Count == 1);
    CHECK_STR(NmosJson_MemberText(&Legs->Items[0], "destination_ip"), "239.1.2.3");
    CHECK_EQ(NmosJson_Member(&Legs->Items[0], "destination_port")->Number, 5010);
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(Body, "activation"), "mode"),
              "activate_immediate");
    CHECK(NmosJson_Member(Body, "master_enable") == NULL);
    NmosJson_Free(Body);
    DtNmosSenderList_Free(Moved);

    // A sender that does not send over RTP, one without a device, and no port.
    CHECK(DtNmosQuery_MoveSender(Query, "player", "239.1.2.3", 5010, NULL) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "not RTP") != NULL);
    CHECK(DtNmosQuery_MoveSender(Query, "camera 1", "239.1.2.3", 5010, NULL) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "names no device") != NULL);
    CHECK(DtNmosQuery_MoveSender(Query, "encoder", "239.1.2.3", 0, NULL) ==
          DTNMOS_E_INVALID_ARGUMENT);
    // A node that refuses the destination.
    Network.PatchStatus = 400;
    Network.PatchAnswer =
        "{\"code\": 400, \"error\": \"no such address\", \"debug\": null}";
    CHECK(DtNmosQuery_MoveSender(Query, "encoder", "10.0.0.1", 5010, NULL) ==
          DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "sender " ENCODER_ID " ('encoder') answered") !=
          NULL);
    CHECK(strstr(DtNmos_GetLastError(), "with 400: no such address") != NULL);
    free(Network.Body);
    DtNmosQuery_Free(Query);
}
