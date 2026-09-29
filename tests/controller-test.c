// #*#*#*#*#*#*#*#*#*#*#*#*#* controller-test.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of the receivers of a registry, and of the controller that connects them
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/controller.h"

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "json.h"
#include "tests.h"

#define BASE "http://registry.test/x-nmos/query/v1.3/"
#define MONITOR_ID "44444444-4444-4444-8444-444444444444"
#define SPEAKER_ID "66666666-6666-4666-8666-666666666666"
#define DEVICE_ID "55555555-5555-4555-8555-555555555555"
#define BARE_DEVICE_ID "77777777-7777-4777-8777-777777777777"
#define CAMERA_ID "11111111-1111-4111-8111-111111111111"
#define DARK_ID "22222222-2222-4222-8222-222222222222"
#define VIDEO_FLOW "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
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
typedef struct fake_network
{
    char requests[16][256]; // "<method> <url>"
    size_t count;
    char content_type[64]; // of the last PATCH
    char* body;            // of the last PATCH
    int patch_status;      // what the node answers a PATCH
    const char* patch_answer;
    int node_unreachable;
} fake_network;

typedef struct fake_route
{
    const char* url;
    const char* body;
} fake_route;

static const fake_route fake_routes[] = {
    {BASE "receivers?paging.limit=100", "[" MONITOR ", " SPEAKER "]"},
    {BASE "receivers?label=monitor&paging.limit=100", "[" MONITOR "]"},
    {BASE "receivers?label=speaker&paging.limit=100", "[" SPEAKER "]"},
    {BASE "receivers?label=nobody&paging.limit=100", "[]"},
    {BASE "receivers/" MONITOR_ID, MONITOR},
    {BASE "senders?label=camera%201&paging.limit=100", "[" CAMERA "]"},
    {BASE "senders?label=camera%202&paging.limit=100", "[" DARK "]"},
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
static dtnmos_result fake_answer(void* user, const dtnmos_http_request* request,
                                 dtnmos_http_response* response, dtnmos_error* error)
{
    fake_network* network = user;
    CHECK_EQ(request->timeout_ms, 2000);
    if (network->count < 16)
    {
        snprintf(network->requests[network->count++], sizeof(network->requests[0]),
                 "%s %s", request->method, request->url);
    }
    if (strcmp(request->method, "PATCH") == 0)
    {
        if (network->node_unreachable)
        {
            snprintf(error->message, sizeof(error->message), "connection refused");
            error->code = DTNMOS_E_HTTP;
            return DTNMOS_E_HTTP;
        }
        snprintf(network->content_type, sizeof(network->content_type), "%s",
                 request->content_type == NULL ? "" : request->content_type);
        free(network->body);
        network->body = malloc(request->body_length + 1);
        memcpy(network->body, request->body, request->body_length);
        network->body[request->body_length] = '\0';
        dtnmos_http_response_set_status(response, network->patch_status);
        const char* answer = network->patch_answer;
        dtnmos_http_response_set_body(response, "application/json", answer,
                                      strlen(answer));
        return DTNMOS_OK;
    }
    CHECK_STR(request->method, "GET");
    for (size_t i = 0; i < sizeof(fake_routes) / sizeof(fake_routes[0]); ++i)
    {
        if (strcmp(fake_routes[i].url, request->url) == 0)
        {
            dtnmos_http_response_set_status(response, 200);
            dtnmos_http_response_set_body(response, "application/json",
                                          fake_routes[i].body,
                                          strlen(fake_routes[i].body));
            return DTNMOS_OK;
        }
    }
    printf("  no route for %s\n", request->url);
    dtnmos_http_response_set_status(response, 404);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_network -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up network, whose node accepts a PATCH, and returns a query of its registry.
//
static dtnmos_query* make_network(fake_network* network)
{
    memset(network, 0, sizeof(*network));
    network->patch_status = 200;
    network->patch_answer = "{}";
    dtnmos_query_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.registry_url = "http://registry.test";
    config.http = fake_answer;
    config.http_user = network;
    config.timeout_ms = 2000;
    dtnmos_query* query = NULL;
    dtnmos_query_create(&config, &query, NULL);
    return query;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- count_patches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static size_t count_patches(const fake_network* network)
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
    fake_network network;
    dtnmos_query* query = make_network(&network);
    REQUIRE(query != NULL);
    dtnmos_receiver_list* list = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    REQUIRE(dtnmos_query_receivers(query, &list, &error) == DTNMOS_OK);
    REQUIRE(dtnmos_receiver_list_count(list) == 2);
    const dtnmos_receiver_info* monitor = dtnmos_receiver_list_at(list, 0);
    CHECK_STR(monitor->id.text, MONITOR_ID);
    CHECK_STR(monitor->device_id.text, DEVICE_ID);
    CHECK_STR(dtnmos_string_get(&monitor->label), "monitor");
    CHECK_EQ(monitor->media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(dtnmos_string_get(&monitor->transport), "urn:x-nmos:transport:rtp");
    CHECK_STR(monitor->sender_id.text, "");
    CHECK_EQ(monitor->active, 0);
    const dtnmos_receiver_info* speaker = dtnmos_receiver_list_at(list, 1);
    CHECK_EQ(speaker->media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(dtnmos_string_get(&speaker->description), "booth");
    CHECK_STR(speaker->sender_id.text, CAMERA_ID);
    CHECK_EQ(speaker->active, 1);
    CHECK(dtnmos_receiver_list_at(list, 2) == NULL);

    // By ID and by label, and a copy that owns its strings.
    const char* const keys[] = {MONITOR_ID, "monitor"};
    for (size_t k = 0; k < 2; ++k)
    {
        dtnmos_receiver_info found = {0};
        REQUIRE(dtnmos_query_find_receiver(query, keys[k], &found, &error) == DTNMOS_OK);
        CHECK_STR(found.id.text, MONITOR_ID);
        CHECK_EQ(found.media, DTNMOS_MEDIA_VIDEO);
        dtnmos_receiver_info copy = {0};
        REQUIRE(dtnmos_receiver_info_copy(&copy, &found) == DTNMOS_OK);
        dtnmos_receiver_info_clear(&found);
        CHECK_STR(dtnmos_string_get(&copy.label), "monitor");
        dtnmos_receiver_info_clear(&copy);
    }
    dtnmos_receiver_info missing = {0};
    CHECK(dtnmos_query_find_receiver(query, "nobody", &missing, &error) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "no receiver labelled 'nobody'") != NULL);
    dtnmos_receiver_list_free(list);
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_connects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void controller_connects_a_receiver(void)
{
    fake_network network;
    dtnmos_query* query = make_network(&network);
    REQUIRE(query != NULL);
    dtnmos_connection connection = {0};
    dtnmos_error error = {DTNMOS_OK, ""};
    const dtnmos_result result =
        dtnmos_connect(query, "monitor", "camera 1", &connection, &error);
    if (result != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    REQUIRE(result == DTNMOS_OK);
    CHECK_STR(connection.receiver.id.text, MONITOR_ID);
    CHECK_STR(connection.sender.id.text, CAMERA_ID);
    CHECK_STR(dtnmos_string_get(&connection.sdp), CAMERA_SDP);

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
    dtnmos_json* body = NULL;
    REQUIRE(dtnmos_json_parse(network.body, strlen(network.body), &body, NULL) ==
            DTNMOS_OK);
    CHECK_STR(dtnmos_json_member_text(body, "sender_id"), CAMERA_ID);
    CHECK_EQ(dtnmos_json_member(body, "master_enable")->type, DTNMOS_JSON_TRUE);
    CHECK_STR(dtnmos_json_member_text(dtnmos_json_member(body, "activation"), "mode"),
              "activate_immediate");
    const dtnmos_json* file = dtnmos_json_member(body, "transport_file");
    CHECK_STR(dtnmos_json_member_text(file, "type"), "application/sdp");
    CHECK_STR(dtnmos_json_member_text(file, "data"), CAMERA_SDP);
    dtnmos_json_free(body);
    free(network.body);
    dtnmos_connection_clear(&connection);
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_disconnects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void controller_disconnects_a_receiver(void)
{
    fake_network network;
    dtnmos_query* query = make_network(&network);
    REQUIRE(query != NULL);
    dtnmos_receiver_info disconnected = {0};
    dtnmos_error error = {DTNMOS_OK, ""};
    REQUIRE(dtnmos_disconnect(query, MONITOR_ID, &disconnected, &error) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&disconnected.label), "monitor");
    REQUIRE(network.count == 3);
    CHECK_STR(network.requests[0], "GET " BASE "receivers/" MONITOR_ID);
    CHECK_STR(network.requests[1], "GET " BASE "devices/" DEVICE_ID);
    CHECK_STR(network.requests[2], "PATCH " STAGED);
    dtnmos_json* body = NULL;
    REQUIRE(dtnmos_json_parse(network.body, strlen(network.body), &body, NULL) ==
            DTNMOS_OK);
    CHECK_EQ(dtnmos_json_member(body, "sender_id")->type, DTNMOS_JSON_NULL);
    CHECK_EQ(dtnmos_json_member(body, "master_enable")->type, DTNMOS_JSON_FALSE);
    CHECK_STR(dtnmos_json_member_text(dtnmos_json_member(body, "activation"), "mode"),
              "activate_immediate");
    CHECK(dtnmos_json_member(body, "transport_file") == NULL);
    dtnmos_json_free(body);
    free(network.body);
    dtnmos_receiver_info_clear(&disconnected);
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- controller_names_what_went_wrong -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void controller_names_what_went_wrong(void)
{
    fake_network network;
    dtnmos_query* query = make_network(&network);
    REQUIRE(query != NULL);
    dtnmos_connection connection = {0};
    dtnmos_error error = {DTNMOS_OK, ""};

    // A receiver the registry does not have.
    CHECK(dtnmos_connect(query, "nobody", "camera 1", &connection, &error) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "no receiver labelled 'nobody'") != NULL);
    // Video to a receiver of audio, refused before the node is asked.
    CHECK(dtnmos_connect(query, "speaker", "camera 1", &connection, &error) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(error.message, "sends video, but receiver " SPEAKER_ID
                                " ('speaker') takes audio") != NULL);
    // A sender without SDP.
    CHECK(dtnmos_connect(query, "monitor", "camera 2", &connection, &error) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "no manifest_href") != NULL);
    // A device without a Connection API.
    CHECK(dtnmos_disconnect(query, "speaker", NULL, &error) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "has no control urn:x-nmos:control:sr-ctrl/v1.1") !=
          NULL);
    CHECK(strstr(error.message, BARE_DEVICE_ID) != NULL);
    CHECK_EQ(count_patches(&network), 0);

    // A node that refuses the PATCH, with the error of IS-05 and with a page of its own.
    network.patch_status = 400;
    network.patch_answer =
        "{\"code\": 400, \"error\": \"no such flow\", \"debug\": null}";
    CHECK(dtnmos_connect(query, "monitor", "camera 1", &connection, &error) ==
          DTNMOS_E_HTTP);
    CHECK(strstr(error.message,
                 "answered the PATCH of " STAGED " with 400: no such flow") != NULL);
    network.patch_status = 500;
    network.patch_answer = "<html>";
    CHECK(dtnmos_disconnect(query, "monitor", NULL, &error) == DTNMOS_E_HTTP);
    CHECK(strstr(error.message, "with 500: <html>") != NULL);
    // A node that cannot be reached.
    network.node_unreachable = 1;
    CHECK(dtnmos_disconnect(query, "monitor", NULL, &error) == DTNMOS_E_HTTP);
    CHECK(strstr(error.message, "connection refused") != NULL);
    CHECK(connection.receiver.id.text[0] == '\0');
    free(network.body);
    dtnmos_query_destroy(query);
}
