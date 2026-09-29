// SPDX-License-Identifier: BSD-3-Clause
//
// Tests of the node against a registry that an HTTP function of the test records: its
// resources registered parents first, the heartbeat that registers them again when the
// registry has lost them, what a removal and the end of the node delete, the answers of
// its Node API and transport files, and, when the library has its server and libcurl, the
// node serving itself over HTTP.

#include "dtnmos/node.h"

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "json.h"
#include "platform.h"
#include "tests.h"

// A request the fake registry received.
typedef struct recorded
{
    char method[8];
    char url[256];
    char type[16]; // of a registration: the type of its resource
} recorded;

typedef struct fake_registration
{
    recorded requests[64];
    int count;
    int heartbeat_status;
} fake_registration;

static dtnmos_result record_http(void* user, const dtnmos_http_request* request,
                                 dtnmos_http_response* response, dtnmos_error* error)
{
    (void)error;
    fake_registration* registry = user;
    recorded* r = &registry->requests[registry->count < 64 ? registry->count++ : 63];
    memset(r, 0, sizeof(*r));
    snprintf(r->method, sizeof(r->method), "%s", request->method);
    snprintf(r->url, sizeof(r->url), "%s", request->url);
    int status = 404;
    if (strstr(request->url, "/x-nmos/registration/v1.3/resource") != NULL &&
        strcmp(request->method, "POST") == 0)
    {
        dtnmos_json* json = NULL;
        if (dtnmos_json_parse(request->body, request->body_length, &json, NULL) ==
            DTNMOS_OK)
        {
            const char* type = dtnmos_json_member_text(json, "type");
            snprintf(r->type, sizeof(r->type), "%s", type == NULL ? "?" : type);
            CHECK(dtnmos_json_member(json, "data") != NULL);
            dtnmos_json_free(json);
        }
        status = 201;
    }
    else if (strstr(request->url, "/health/nodes/") != NULL)
    {
        status = registry->heartbeat_status;
    }
    else if (strcmp(request->method, "DELETE") == 0)
    {
        status = 204;
    }
    dtnmos_http_response_set_status(response, status);
    return DTNMOS_OK;
}

#define NODE_ID "aaaaaaaa-0000-4000-8000-000000000001"
#define DEVICE_ID "aaaaaaaa-0000-4000-8000-000000000002"
#define SENDER_ID "aaaaaaaa-0000-4000-8000-000000000003"
#define RECEIVER_ID "aaaaaaaa-0000-4000-8000-000000000004"

// Makes a node with a device, a video sender and an audio receiver.
static dtnmos_node* make_node(fake_registration* registry, const char* host,
                              uint16_t port, dtnmos_http_fn http)
{
    dtnmos_node_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.id = (dtnmos_id){NODE_ID};
    config.label = "test node";
    config.hostname = "test-host";
    config.api_host = host;
    config.api_port = port;
    config.registration_url = "http://registry.test/";
    config.http = http;
    config.http_user = registry;
    config.heartbeat_ms = 1;
    dtnmos_node* node = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    if (dtnmos_node_create(&config, &node, &error) != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
        return NULL;
    }
    dtnmos_device_config device = {sizeof(device), {DEVICE_ID}, "a card", "its port 1"};
    CHECK(dtnmos_node_add_device(node, &device, &error) == DTNMOS_OK);
    dtnmos_flow flow = {0};
    flow.size = sizeof(flow);
    flow.media = DTNMOS_MEDIA_VIDEO;
    dtnmos_string_set_text(&flow.destination_ip, "239.0.0.1");
    flow.destination_port = 5004;
    flow.payload_type = 96;
    flow.clock_rate = 90000;
    flow.format.video.width = 1920;
    flow.format.video.height = 1080;
    flow.format.video.rate_numerator = 25;
    flow.format.video.rate_denominator = 1;
    flow.format.video.interlaced = 1;
    flow.format.video.depth = 10;
    dtnmos_string_set_text(&flow.format.video.sampling, "YCbCr-4:2:2");
    dtnmos_sender_config sender = {
        sizeof(sender), {SENDER_ID}, {DEVICE_ID}, "camera", "", &flow, "192.168.1.5"};
    CHECK(dtnmos_node_add_sender(node, &sender, NULL, NULL, &error) == DTNMOS_OK);
    dtnmos_flow_clear(&flow);
    dtnmos_receiver_config receiver = {
        sizeof(receiver), {RECEIVER_ID}, {DEVICE_ID}, "monitor", "", DTNMOS_MEDIA_AUDIO};
    CHECK(dtnmos_node_add_receiver(node, &receiver, NULL, NULL, &error) == DTNMOS_OK);
    return node;
}

void node_registers_parents_before_children(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    dtnmos_node* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    CHECK(!dtnmos_node_registered(node));
    dtnmos_error error = {DTNMOS_OK, ""};
    uint32_t next_ms = 0;
    REQUIRE(dtnmos_node_poll(node, &next_ms, &error) == DTNMOS_OK);
    CHECK(dtnmos_node_registered(node));
    const char* const order[] = {"node", "device", "source",
                                 "flow", "sender", "receiver"};
    REQUIRE(registry.count >= 6);
    for (int i = 0; i < 6; ++i)
    {
        CHECK_STR(registry.requests[i].type, order[i]);
        CHECK_STR(registry.requests[i].url,
                  "http://registry.test/x-nmos/registration/v1.3/resource");
    }
    // Adding an ID twice, or a sender to a device the node lacks, fails.
    dtnmos_device_config twice = {sizeof(twice), {DEVICE_ID}, "again", ""};
    CHECK(dtnmos_node_add_device(node, &twice, &error) == DTNMOS_E_INVALID_ARGUMENT);
    dtnmos_receiver_config orphan = {sizeof(orphan),
                                     {"aaaaaaaa-0000-4000-8000-00000000000f"},
                                     {"aaaaaaaa-0000-4000-8000-00000000000e"},
                                     "x",
                                     "",
                                     DTNMOS_MEDIA_VIDEO};
    CHECK(dtnmos_node_add_receiver(node, &orphan, NULL, NULL, &error) ==
          DTNMOS_E_INVALID_ARGUMENT);
    dtnmos_node_destroy(node);
}

void node_registers_again_when_the_registry_lost_it(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    dtnmos_node* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    dtnmos_error error = {DTNMOS_OK, ""};
    REQUIRE(dtnmos_node_poll(node, NULL, &error) == DTNMOS_OK);
    const int registered = registry.count;
    // A heartbeat the registry answers with 404 means it lost the node. The wait is
    // longer than a tick of the clock of Windows, about 16 ms, so that the heartbeat is
    // due.
    dtnmos_sleep_ms(40);
    registry.heartbeat_status = 404;
    REQUIRE(dtnmos_node_poll(node, NULL, &error) == DTNMOS_OK);
    CHECK(strstr(registry.requests[registered].url, "/health/nodes/" NODE_ID) != NULL);
    CHECK(!dtnmos_node_registered(node));
    registry.heartbeat_status = 200;
    REQUIRE(dtnmos_node_poll(node, NULL, &error) == DTNMOS_OK);
    CHECK(dtnmos_node_registered(node));
    CHECK_STR(registry.requests[registered + 1].type, "node");
    CHECK_STR(registry.requests[registered + 6].type, "receiver");
    dtnmos_node_destroy(node);
}

void node_deletes_what_is_removed_and_what_it_had(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    dtnmos_node* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    dtnmos_error error = {DTNMOS_OK, ""};
    REQUIRE(dtnmos_node_poll(node, NULL, &error) == DTNMOS_OK);
    const int before = registry.count;
    const dtnmos_id sender = {SENDER_ID};
    REQUIRE(dtnmos_node_remove(node, &sender, &error) == DTNMOS_OK);
    CHECK(dtnmos_node_remove(node, &sender, &error) == DTNMOS_E_NOT_FOUND);
    REQUIRE(dtnmos_node_poll(node, NULL, &error) == DTNMOS_OK);
    // The sender, its flow and its source go, and the device that listed it registers
    // anew.
    CHECK_STR(registry.requests[before].method, "DELETE");
    CHECK(strstr(registry.requests[before].url, "resource/senders/" SENDER_ID) != NULL);
    CHECK(strstr(registry.requests[before + 1].url, "resource/flows/") != NULL);
    CHECK(strstr(registry.requests[before + 2].url, "resource/sources/") != NULL);
    CHECK_STR(registry.requests[before + 3].type, "device");
    const int kept = registry.count;
    dtnmos_node_destroy(node);
    // The end of the node deletes the receiver, the device and the node.
    REQUIRE(registry.count == kept + 3);
    CHECK(strstr(registry.requests[kept].url, "resource/receivers/" RECEIVER_ID) != NULL);
    CHECK(strstr(registry.requests[kept + 1].url, "resource/devices/" DEVICE_ID) != NULL);
    CHECK(strstr(registry.requests[kept + 2].url, "resource/nodes/" NODE_ID) != NULL);
}

// Asks the node for path and returns the response, which the caller frees.
static dtnmos_http_response* ask(dtnmos_node* node, const char* method, const char* path)
{
    dtnmos_http_request request;
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.method = method;
    request.url = path;
    dtnmos_http_response* response = dtnmos_http_response_create();
    dtnmos_error error = {DTNMOS_OK, ""};
    CHECK(dtnmos_node_handle(node, &request, response, &error) == DTNMOS_OK);
    return response;
}

void node_answers_its_node_api_and_transport_files(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    dtnmos_node* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    dtnmos_http_response* response = ask(node, "GET", "/x-nmos/node/v1.3/self");
    CHECK_EQ(dtnmos_http_response_status(response), 200);
    dtnmos_json* json = NULL;
    size_t length = 0;
    const char* body = dtnmos_http_response_body(response, &length);
    REQUIRE(dtnmos_json_parse(body, length, &json, NULL) == DTNMOS_OK);
    CHECK_STR(dtnmos_json_member_text(json, "id"), NODE_ID);
    CHECK_STR(dtnmos_json_member_text(json, "href"), "http://192.168.1.5:8080/");
    dtnmos_json_free(json);
    dtnmos_http_response_free(response);

    response = ask(node, "GET", "/x-nmos/node/v1.3/senders/?paging.limit=10");
    body = dtnmos_http_response_body(response, &length);
    REQUIRE(dtnmos_json_parse(body, length, &json, NULL) == DTNMOS_OK);
    REQUIRE(json->type == DTNMOS_JSON_ARRAY && json->count == 1);
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "manifest_href"),
              "http://192.168.1.5:8080/x-nmos/connection/v1.1/single/senders/" SENDER_ID
              "/transportfile");
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "transport"),
              "urn:x-nmos:transport:rtp.mcast");
    dtnmos_json_free(json);
    dtnmos_http_response_free(response);

    response = ask(node, "GET", "/x-nmos/node/v1.3/flows");
    body = dtnmos_http_response_body(response, &length);
    REQUIRE(dtnmos_json_parse(body, length, &json, NULL) == DTNMOS_OK);
    REQUIRE(json->count == 1);
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "interlace_mode"),
              "interlaced_tff");
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "media_type"), "video/raw");
    dtnmos_json_free(json);
    dtnmos_http_response_free(response);

    // The transport file is the SDP of the flow of the sender.
    response = ask(node, "GET",
                   "/x-nmos/connection/v1.1/single/senders/" SENDER_ID "/transportfile");
    CHECK_EQ(dtnmos_http_response_status(response), 200);
    CHECK_STR(dtnmos_http_response_content_type(response), "application/sdp");
    body = dtnmos_http_response_body(response, &length);
    dtnmos_sdp* sdp = NULL;
    REQUIRE(dtnmos_sdp_parse(body, length, &sdp, NULL) == DTNMOS_OK);
    CHECK_EQ(dtnmos_sdp_flow(sdp, 0)->format.video.height, 1080);
    CHECK(dtnmos_sdp_flow(sdp, 0)->format.video.interlaced);
    CHECK_STR(dtnmos_string_get(&dtnmos_sdp_session(sdp)->origin_ip), "192.168.1.5");
    dtnmos_sdp_free(sdp);
    dtnmos_http_response_free(response);

    const char* const missing[] = {"/x-nmos/node/v1.3/senders/nobody", "/x-nmos/nothing",
                                   "/x-nmos/node/v1.3/clocks"};
    for (size_t i = 0; i < 3; ++i)
    {
        response = ask(node, "GET", missing[i]);
        CHECK_EQ(dtnmos_http_response_status(response), 404);
        dtnmos_http_response_free(response);
    }
    response = ask(node, "POST", "/x-nmos/node/v1.3/self");
    CHECK_EQ(dtnmos_http_response_status(response), 405);
    dtnmos_http_response_free(response);
    dtnmos_node_destroy(node);
}

void node_serves_itself_over_http(void)
{
    if (!dtnmos_has_server() || !dtnmos_has_curl())
    {
        printf("  skipped: the library has no server or no libcurl\n");
        return;
    }
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    // The registry is fake, the node's own server is real.
    dtnmos_node* node = make_node(&registry, "127.0.0.1", 0, record_http);
    REQUIRE(node != NULL);
    dtnmos_error error = {DTNMOS_OK, ""};
    const dtnmos_result served = dtnmos_node_serve(node, &error);
    if (served != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    REQUIRE(served == DTNMOS_OK);
    CHECK(dtnmos_node_api_port(node) != 0);
    dtnmos_string base = {0};
    REQUIRE(dtnmos_node_api_url(node, &base) == DTNMOS_OK);
    char url[256];
    snprintf(url, sizeof(url), "%s/x-nmos/node/v1.3/self", dtnmos_string_get(&base));
    dtnmos_http_request request = {sizeof(request), "GET", url, NULL, NULL, 0, 3000};
    dtnmos_http_response* response = dtnmos_http_response_create();
    REQUIRE(dtnmos_curl_http(NULL, &request, response, &error) == DTNMOS_OK);
    CHECK_EQ(dtnmos_http_response_status(response), 200);
    CHECK(strstr(dtnmos_http_response_body(response, NULL), NODE_ID) != NULL);
    dtnmos_http_response_free(response);
    // The thread of the server polls the node, which registers.
    for (int wait = 0; wait < 100 && !dtnmos_node_registered(node); ++wait)
    {
        dtnmos_sleep_ms(20);
    }
    CHECK(dtnmos_node_registered(node));
    dtnmos_string_clear(&base);
    dtnmos_node_destroy(node);
}
