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
    char requests[16][256]; // "<method> <url>"
    size_t count;
    char content_type[64]; // of the last PATCH
    char* body;            // of the last PATCH
    int patch_status;      // what the node answers a PATCH
    const char* patch_answer;
    int node_unreachable;
} NmosFakeNetwork;

typedef struct NmosFakeRoute
{
    const char* url;
    const char* body;
} NmosFakeRoute;

static const NmosFakeRoute fake_routes[] = {
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_answer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult fake_answer(void* user, const DtNmosHttpRequest* request,
                                DtNmosHttpResponse* response)
{
    NmosFakeNetwork* network = user;
    CHECK_EQ(request->TimeoutMs, 2000);
    if (network->count < 16)
    {
        snprintf(network->requests[network->count++], sizeof(network->requests[0]),
                 "%s %s", request->Method, request->Url);
    }
    if (strcmp(request->Method, "PATCH") == 0)
    {
        if (network->node_unreachable)
        {
            return DtNmos_SetLastError(DTNMOS_E_HTTP, "connection refused");
        }
        snprintf(network->content_type, sizeof(network->content_type), "%s",
                 request->ContentType == NULL ? "" : request->ContentType);
        free(network->body);
        network->body = malloc(request->BodyLength + 1);
        memcpy(network->body, request->Body, request->BodyLength);
        network->body[request->BodyLength] = '\0';
        DtNmosHttpResponse_SetStatus(response, network->patch_status);
        const char* answer = network->patch_answer;
        DtNmosHttpResponse_SetBody(response, "application/json", answer, strlen(answer));
        return DTNMOS_OK;
    }
    CHECK_STR(request->Method, "GET");
    for (size_t i = 0; i < sizeof(fake_routes) / sizeof(fake_routes[0]); ++i)
    {
        if (strcmp(fake_routes[i].url, request->Url) == 0)
        {
            DtNmosHttpResponse_SetStatus(response, 200);
            DtNmosHttpResponse_SetBody(response, "application/json", fake_routes[i].body,
                                       strlen(fake_routes[i].body));
            return DTNMOS_OK;
        }
    }
    printf("  no route for %s\n", request->Url);
    DtNmosHttpResponse_SetStatus(response, 404);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_network -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up network, whose node accepts a PATCH, and returns a query of its registry.
//
static DtNmosQuery* make_network(NmosFakeNetwork* network)
{
    memset(network, 0, sizeof(*network));
    network->patch_status = 200;
    network->patch_answer = "{}";
    DtNmosQueryConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.RegistryUrl = "http://registry.test";
    config.Http = fake_answer;
    config.HttpUser = network;
    config.TimeoutMs = 2000;
    DtNmosQuery* query = DtNmosQuery_Alloc();
    if (query == NULL || DtNmosQuery_Open(query, &config) != DTNMOS_OK)
    {
        DtNmosQuery_Freep(&query);
    }
    return query;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- count_patches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static size_t count_patches(const NmosFakeNetwork* network)
{
    size_t patches = 0;
    for (size_t i = 0; i < network->count; ++i)
    {
        patches += strncmp(network->requests[i], "PATCH ", 6) == 0 ? 1 : 0;
    }
    return patches;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- query_lists_and_finds_receivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void query_lists_and_finds_receivers(void)
{
    NmosFakeNetwork network;
    DtNmosQuery* query = make_network(&network);
    REQUIRE(query != NULL);
    DtNmosReceiverList* list = NULL;
    REQUIRE(DtNmosQuery_Receivers(query, &list) == DTNMOS_OK);
    REQUIRE(DtNmosReceiverList_Count(list) == 2);
    const DtNmosReceiverInfo* monitor = DtNmosReceiverList_At(list, 0);
    CHECK_STR(monitor->Id.Text, MONITOR_ID);
    CHECK_STR(monitor->DeviceId.Text, DEVICE_ID);
    CHECK_STR(monitor->Label, "monitor");
    CHECK_EQ(monitor->Media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(monitor->Transport, "urn:x-nmos:transport:rtp");
    CHECK_STR(monitor->SenderId.Text, "");
    CHECK_EQ(monitor->Active, 0);
    const DtNmosReceiverInfo* speaker = DtNmosReceiverList_At(list, 1);
    CHECK_EQ(speaker->Media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(speaker->Description, "booth");
    CHECK_STR(speaker->SenderId.Text, CAMERA_ID);
    CHECK_EQ(speaker->Active, 1);
    CHECK(DtNmosReceiverList_At(list, 2) == NULL);

    // By ID and by label, each in a list of one that owns its strings.
    const char* const keys[] = {MONITOR_ID, "monitor"};
    for (size_t k = 0; k < 2; ++k)
    {
        DtNmosReceiverList* found = NULL;
        REQUIRE(DtNmosQuery_FindReceiver(query, keys[k], &found) == DTNMOS_OK);
        REQUIRE(DtNmosReceiverList_Count(found) == 1);
        const DtNmosReceiverInfo* receiver = DtNmosReceiverList_At(found, 0);
        CHECK_STR(receiver->Id.Text, MONITOR_ID);
        CHECK_EQ(receiver->Media, DTNMOS_MEDIA_VIDEO);
        CHECK_STR(receiver->Label, "monitor");
        DtNmosReceiverList_Free(found);
    }
    DtNmosReceiverList* missing = NULL;
    CHECK(DtNmosQuery_FindReceiver(query, "nobody", &missing) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no receiver labelled 'nobody'") != NULL);
    CHECK(missing == NULL);
    DtNmosReceiverList_Free(list);
    DtNmosQuery_Free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_connects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void controller_connects_a_receiver(void)
{
    NmosFakeNetwork network;
    DtNmosQuery* query = make_network(&network);
    REQUIRE(query != NULL);
    DtNmosConnection* connection = NULL;
    const DtNmosResult result =
        DtNmosQuery_Connect(query, "monitor", "camera 1", &connection);
    if (result != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(result == DTNMOS_OK);
    CHECK_STR(DtNmosConnection_Receiver(connection)->Id.Text, MONITOR_ID);
    CHECK_STR(DtNmosConnection_Receiver(connection)->Label, "monitor");
    CHECK_STR(DtNmosConnection_Sender(connection)->Id.Text, CAMERA_ID);
    CHECK_STR(DtNmosConnection_Sdp(connection), CAMERA_SDP);

    // The receiver and the sender, the flow of the sender, its SDP, the device of the
    // receiver, and the PATCH to the Connection API of v1.1 of that device.
    const char* const expected[] = {
        "GET " BASE "receivers?label=monitor&paging.limit=100",
        "GET " BASE "senders?label=camera%201&paging.limit=100",
        "GET " BASE "flows/" VIDEO_FLOW,
        "GET http://camera.test/video.sdp",
        "GET " BASE "devices/" DEVICE_ID,
        "PATCH " STAGED,
    };
    REQUIRE(network.count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < network.count; ++i)
    {
        CHECK_STR(network.requests[i], expected[i]);
    }
    CHECK_STR(network.content_type, "application/json");
    NmosJson* body = NULL;
    REQUIRE(NmosJson_Parse(network.body, strlen(network.body), &body) == DTNMOS_OK);
    CHECK_STR(NmosJson_MemberText(body, "sender_id"), CAMERA_ID);
    CHECK_EQ(NmosJson_Member(body, "master_enable")->type, DTNMOS_JSON_TRUE);
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(body, "activation"), "mode"),
              "activate_immediate");
    const NmosJson* file = NmosJson_Member(body, "transport_file");
    CHECK_STR(NmosJson_MemberText(file, "type"), "application/sdp");
    CHECK_STR(NmosJson_MemberText(file, "data"), CAMERA_SDP);
    NmosJson_Free(body);
    free(network.body);
    DtNmosConnection_Free(connection);
    DtNmosQuery_Free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_disconnects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void controller_disconnects_a_receiver(void)
{
    NmosFakeNetwork network;
    DtNmosQuery* query = make_network(&network);
    REQUIRE(query != NULL);
    DtNmosReceiverList* disconnected = NULL;
    REQUIRE(DtNmosQuery_Disconnect(query, MONITOR_ID, &disconnected) == DTNMOS_OK);
    REQUIRE(DtNmosReceiverList_Count(disconnected) == 1);
    CHECK_STR(DtNmosReceiverList_At(disconnected, 0)->Label, "monitor");
    REQUIRE(network.count == 3);
    CHECK_STR(network.requests[0], "GET " BASE "receivers/" MONITOR_ID);
    CHECK_STR(network.requests[1], "GET " BASE "devices/" DEVICE_ID);
    CHECK_STR(network.requests[2], "PATCH " STAGED);
    NmosJson* body = NULL;
    REQUIRE(NmosJson_Parse(network.body, strlen(network.body), &body) == DTNMOS_OK);
    CHECK_EQ(NmosJson_Member(body, "sender_id")->type, DTNMOS_JSON_NULL);
    CHECK_EQ(NmosJson_Member(body, "master_enable")->type, DTNMOS_JSON_FALSE);
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(body, "activation"), "mode"),
              "activate_immediate");
    CHECK(NmosJson_Member(body, "transport_file") == NULL);
    NmosJson_Free(body);
    free(network.body);
    DtNmosReceiverList_Free(disconnected);
    DtNmosQuery_Free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_names_what_went_wrong -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void controller_names_what_went_wrong(void)
{
    NmosFakeNetwork network;
    DtNmosQuery* query = make_network(&network);
    REQUIRE(query != NULL);
    DtNmosConnection* connection = NULL;

    // A receiver the registry does not have.
    CHECK(DtNmosQuery_Connect(query, "nobody", "camera 1", &connection) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no receiver labelled 'nobody'") != NULL);
    // Video to a receiver of audio, refused before the node is asked.
    CHECK(DtNmosQuery_Connect(query, "speaker", "camera 1", &connection) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "sends video, but receiver " SPEAKER_ID
                                        " ('speaker') takes audio") != NULL);
    // A sender without SDP.
    CHECK(DtNmosQuery_Connect(query, "monitor", "camera 2", &connection) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no manifest_href") != NULL);
    // A device without a Connection API.
    CHECK(DtNmosQuery_Disconnect(query, "speaker", NULL) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(),
                 "has no control urn:x-nmos:control:sr-ctrl/v1.1") != NULL);
    CHECK(strstr(DtNmos_GetLastError(), BARE_DEVICE_ID) != NULL);
    CHECK_EQ(count_patches(&network), 0);

    // A node that refuses the PATCH, with the error of IS-05 and with a page of its own.
    network.patch_status = 400;
    network.patch_answer =
        "{\"code\": 400, \"error\": \"no such flow\", \"debug\": null}";
    CHECK(DtNmosQuery_Connect(query, "monitor", "camera 1", &connection) ==
          DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(),
                 "answered the PATCH of " STAGED " with 400: no such flow") != NULL);
    network.patch_status = 500;
    network.patch_answer = "<html>";
    CHECK(DtNmosQuery_Disconnect(query, "monitor", NULL) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "with 500: <html>") != NULL);
    // A node that cannot be reached.
    network.node_unreachable = 1;
    CHECK(DtNmosQuery_Disconnect(query, "monitor", NULL) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "connection refused") != NULL);
    CHECK(connection == NULL);
    free(network.body);
    DtNmosQuery_Free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- controller_moves_a_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void controller_moves_a_sender(void)
{
    NmosFakeNetwork network;
    DtNmosQuery* query = make_network(&network);
    REQUIRE(query != NULL);
    DtNmosSenderList* moved = NULL;
    const DtNmosResult result =
        DtNmosQuery_MoveSender(query, "encoder", "239.1.2.3", 5010, &moved);
    if (result != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(result == DTNMOS_OK);
    REQUIRE(DtNmosSenderList_Count(moved) == 1);
    CHECK_STR(DtNmosSenderList_At(moved, 0)->Id.Text, ENCODER_ID);
    CHECK_STR(DtNmosSenderList_At(moved, 0)->Label, "encoder");
    // The sender, the device of the sender, and the PATCH of its staged parameters.
    REQUIRE(network.count == 3);
    CHECK_STR(network.requests[0], "GET " BASE "senders?label=encoder&paging.limit=100");
    CHECK_STR(network.requests[1], "GET " BASE "devices/" DEVICE_ID);
    CHECK_STR(network.requests[2], "PATCH " MOVED);
    NmosJson* body = NULL;
    REQUIRE(NmosJson_Parse(network.body, strlen(network.body), &body) == DTNMOS_OK);
    const NmosJson* legs = NmosJson_Member(body, "transport_params");
    REQUIRE(legs != NULL && legs->type == DTNMOS_JSON_ARRAY && legs->count == 1);
    CHECK_STR(NmosJson_MemberText(&legs->items[0], "destination_ip"), "239.1.2.3");
    CHECK_EQ(NmosJson_Member(&legs->items[0], "destination_port")->number, 5010);
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(body, "activation"), "mode"),
              "activate_immediate");
    CHECK(NmosJson_Member(body, "master_enable") == NULL);
    NmosJson_Free(body);
    DtNmosSenderList_Free(moved);

    // A sender that does not send over RTP, one without a device, and no port.
    CHECK(DtNmosQuery_MoveSender(query, "player", "239.1.2.3", 5010, NULL) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "not RTP") != NULL);
    CHECK(DtNmosQuery_MoveSender(query, "camera 1", "239.1.2.3", 5010, NULL) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "names no device") != NULL);
    CHECK(DtNmosQuery_MoveSender(query, "encoder", "239.1.2.3", 0, NULL) ==
          DTNMOS_E_INVALID_ARGUMENT);
    // A node that refuses the destination.
    network.patch_status = 400;
    network.patch_answer =
        "{\"code\": 400, \"error\": \"no such address\", \"debug\": null}";
    CHECK(DtNmosQuery_MoveSender(query, "encoder", "10.0.0.1", 5010, NULL) ==
          DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "sender " ENCODER_ID " ('encoder') answered") !=
          NULL);
    CHECK(strstr(DtNmos_GetLastError(), "with 400: no such address") != NULL);
    free(network.body);
    DtNmosQuery_Free(query);
}
