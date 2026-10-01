// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestNode.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of the node against a registry that an HTTP function of the test records
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_node.h"

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosOs.h"
#include "check.h"
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
    const char* unreachable; // a request to a URL that holds it gets no answer
} fake_registration;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- record_http -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult record_http(void* user, const DtNmosHttpRequest* request,
                                DtNmosHttpResponse* response, DtNmosError* error)
{
    fake_registration* registry = user;
    recorded* r = &registry->requests[registry->count < 64 ? registry->count++ : 63];
    memset(r, 0, sizeof(*r));
    snprintf(r->method, sizeof(r->method), "%s", request->Method);
    snprintf(r->url, sizeof(r->url), "%s", request->Url);
    if (registry->unreachable != NULL &&
        strstr(request->Url, registry->unreachable) != NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_HTTP, "%s did not answer.", request->Url);
    }
    int status = 404;
    if (strstr(request->Url, "/x-nmos/registration/v1.3/resource") != NULL &&
        strcmp(request->Method, "POST") == 0)
    {
        dtnmos_json* json = NULL;
        if (dtnmos_json_parse(request->Body, request->BodyLength, &json, NULL) ==
            DTNMOS_OK)
        {
            const char* type = dtnmos_json_member_text(json, "type");
            snprintf(r->type, sizeof(r->type), "%s", type == NULL ? "?" : type);
            CHECK(dtnmos_json_member(json, "data") != NULL);
            dtnmos_json_free(json);
        }
        status = 201;
    }
    else if (strstr(request->Url, "/health/nodes/") != NULL)
    {
        status = registry->heartbeat_status;
    }
    else if (strcmp(request->Method, "DELETE") == 0)
    {
        status = 204;
    }
    DtNmosHttpResponse_SetStatus(response, status);
    return DTNMOS_OK;
}

#define NODE_ID "aaaaaaaa-0000-4000-8000-000000000001"
#define DEVICE_ID "aaaaaaaa-0000-4000-8000-000000000002"
#define SENDER_ID "aaaaaaaa-0000-4000-8000-000000000003"
#define RECEIVER_ID "aaaaaaaa-0000-4000-8000-000000000004"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_node -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes a node with a device, a video sender and an audio receiver.
//
static DtNmosNode* make_node(fake_registration* registry, const char* host, uint16_t port,
                             DtNmosHttpFunc http)
{
    DtNmosNodeConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Id = (DtNmosId){NODE_ID};
    config.Label = "test node";
    config.Hostname = "test-host";
    config.ApiHost = host;
    config.ApiPort = port;
    config.RegistrationUrl = "http://registry.test/";
    config.Http = http;
    config.HttpUser = registry;
    config.HeartbeatMs = 1;
    DtNmosNode* node = NULL;
    DtNmosError error = {DTNMOS_OK, ""};
    if (DtNmosNode_Create(&config, &node, &error) != DTNMOS_OK)
    {
        printf("  %s\n", error.Message);
        return NULL;
    }
    DtNmosDeviceConfig device = {sizeof(device), {DEVICE_ID}, "a card", "its port 1"};
    CHECK(DtNmosNode_AddDevice(node, &device, &error) == DTNMOS_OK);
    DtNmosFlow flow = {0};
    flow.Size = sizeof(flow);
    flow.Media = DTNMOS_MEDIA_VIDEO;
    DtNmosString_SetText(&flow.DestinationIp, "239.0.0.1");
    flow.DestinationPort = 5004;
    flow.PayloadType = 96;
    flow.ClockRate = 90000;
    flow.Format.Video.Width = 1920;
    flow.Format.Video.Height = 1080;
    flow.Format.Video.RateNumerator = 25;
    flow.Format.Video.RateDenominator = 1;
    flow.Format.Video.Interlaced = 1;
    flow.Format.Video.Depth = 10;
    DtNmosString_SetText(&flow.Format.Video.Sampling, "YCbCr-4:2:2");
    DtNmosSenderConfig sender = {sizeof(sender), {SENDER_ID},  {DEVICE_ID}, "camera", "",
                                 &flow,          "192.168.1.5"};
    CHECK(DtNmosNode_AddSender(node, &sender, NULL, NULL, &error) == DTNMOS_OK);
    DtNmosFlow_Clear(&flow);
    DtNmosReceiverConfig receiver = {
        sizeof(receiver), {RECEIVER_ID}, {DEVICE_ID}, "monitor", "", DTNMOS_MEDIA_AUDIO};
    CHECK(DtNmosNode_AddReceiver(node, &receiver, NULL, NULL, &error) == DTNMOS_OK);
    return node;
}

// .-.-.-.-.-.-.-.-.-.-.- node_registers_parents_before_children -.-.-.-.-.-.-.-.-.-.-.-.-
//
void node_registers_parents_before_children(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    DtNmosNode* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    CHECK(!DtNmosNode_IsRegistered(node));
    DtNmosError error = {DTNMOS_OK, ""};
    uint32_t next_ms = 0;
    REQUIRE(DtNmosNode_Poll(node, &next_ms, &error) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(node));
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
    DtNmosDeviceConfig twice = {sizeof(twice), {DEVICE_ID}, "again", ""};
    CHECK(DtNmosNode_AddDevice(node, &twice, &error) == DTNMOS_E_INVALID_ARGUMENT);
    DtNmosReceiverConfig orphan = {sizeof(orphan),
                                   {"aaaaaaaa-0000-4000-8000-00000000000f"},
                                   {"aaaaaaaa-0000-4000-8000-00000000000e"},
                                   "x",
                                   "",
                                   DTNMOS_MEDIA_VIDEO};
    CHECK(DtNmosNode_AddReceiver(node, &orphan, NULL, NULL, &error) ==
          DTNMOS_E_INVALID_ARGUMENT);
    DtNmosNode_Destroy(node);
}

// .-.-.-.-.-.-.-.-.- node_registers_again_when_the_registry_lost_it -.-.-.-.-.-.-.-.-.-.-
//
void node_registers_again_when_the_registry_lost_it(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    DtNmosNode* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    DtNmosError error = {DTNMOS_OK, ""};
    REQUIRE(DtNmosNode_Poll(node, NULL, &error) == DTNMOS_OK);
    const int registered = registry.count;
    // A heartbeat the registry answers with 404 means it lost the node. The wait is
    // longer than a tick of the clock of Windows, about 16 ms, so that the heartbeat is
    // due.
    dtnmos_sleep_ms(40);
    registry.heartbeat_status = 404;
    REQUIRE(DtNmosNode_Poll(node, NULL, &error) == DTNMOS_OK);
    CHECK(strstr(registry.requests[registered].url, "/health/nodes/" NODE_ID) != NULL);
    CHECK(!DtNmosNode_IsRegistered(node));
    registry.heartbeat_status = 200;
    REQUIRE(DtNmosNode_Poll(node, NULL, &error) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(node));
    CHECK_STR(registry.requests[registered + 1].type, "node");
    CHECK_STR(registry.requests[registered + 6].type, "receiver");
    DtNmosNode_Destroy(node);
}

// .-.-.-.-.-.-.-.-.-.- node_deletes_what_is_removed_and_what_it_had -.-.-.-.-.-.-.-.-.-.-
//
// The registry a callback moves the node to, and what it was asked.
typedef struct next_registry
{
    int calls;
    uint32_t failures;
    const char* url; // null keeps the node where it is
} next_registry;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- give_next_registry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int give_next_registry(void* user, uint32_t failures, DtNmosString* next_url)
{
    next_registry* next = user;
    ++next->calls;
    next->failures = failures;
    if (next->url == NULL)
    {
        return 0;
    }
    DtNmosString_SetText(next_url, next->url);
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- node_moves_to_the_next_registry -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void node_moves_to_the_next_registry(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    registry.unreachable = "registry-a.test";
    next_registry next = {0, 0, "http://registry-b.test"};
    DtNmosNodeConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Id = (DtNmosId){NODE_ID};
    config.Label = "moving node";
    config.ApiHost = "192.168.1.5";
    config.ApiPort = 8080;
    config.RegistrationUrl = "http://registry-a.test";
    config.Http = record_http;
    config.HttpUser = &registry;
    config.HeartbeatMs = 1;
    config.RegistryFailed = give_next_registry;
    config.RegistryFailedUser = &next;
    config.FailuresBeforeSwitch = 2;
    DtNmosNode* node = NULL;
    REQUIRE(DtNmosNode_Create(&config, &node, NULL) == DTNMOS_OK);

    // The first registry does not answer; after two failed polls the node moves on, and
    // registers with the next one from the start.
    CHECK(DtNmosNode_Poll(node, NULL, NULL) != DTNMOS_OK);
    CHECK_EQ(next.calls, 0);
    CHECK(DtNmosNode_Poll(node, NULL, NULL) != DTNMOS_OK);
    CHECK_EQ(next.calls, 1);
    CHECK_EQ(next.failures, 2);
    const int before = registry.count;
    REQUIRE(DtNmosNode_Poll(node, NULL, NULL) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(node));
    REQUIRE(registry.count > before);
    CHECK(strstr(registry.requests[before].url,
                 "http://registry-b.test/x-nmos/registration/v1.3/resource") != NULL);
    CHECK_STR(registry.requests[before].type, "node");

    // A heartbeat answered with 404 is no failure: the node registers again with the
    // same registry.
    registry.heartbeat_status = 404;
    dtnmos_sleep_ms(40);
    CHECK(DtNmosNode_Poll(node, NULL, NULL) == DTNMOS_OK);
    registry.heartbeat_status = 200;
    CHECK(DtNmosNode_Poll(node, NULL, NULL) == DTNMOS_OK);
    CHECK_EQ(next.calls, 1);
    CHECK(strstr(registry.requests[registry.count - 1].url, "registry-b.test") != NULL);

    // A callback without another registry keeps the node where it is.
    registry.unreachable = "registry-b.test";
    next.url = NULL;
    for (int poll = 0; poll < 2; ++poll)
    {
        dtnmos_sleep_ms(40);
        CHECK(DtNmosNode_Poll(node, NULL, NULL) != DTNMOS_OK);
    }
    CHECK_EQ(next.calls, 2);
    CHECK(strstr(registry.requests[registry.count - 1].url, "registry-b.test") != NULL);
    registry.unreachable = NULL;
    DtNmosNode_Destroy(node);
}

void node_deletes_what_is_removed_and_what_it_had(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    DtNmosNode* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    DtNmosError error = {DTNMOS_OK, ""};
    REQUIRE(DtNmosNode_Poll(node, NULL, &error) == DTNMOS_OK);
    const int before = registry.count;
    const DtNmosId sender = {SENDER_ID};
    REQUIRE(DtNmosNode_Remove(node, &sender, &error) == DTNMOS_OK);
    CHECK(DtNmosNode_Remove(node, &sender, &error) == DTNMOS_E_NOT_FOUND);
    REQUIRE(DtNmosNode_Poll(node, NULL, &error) == DTNMOS_OK);
    // The sender, its flow and its source go, and the device that listed it registers
    // anew.
    CHECK_STR(registry.requests[before].method, "DELETE");
    CHECK(strstr(registry.requests[before].url, "resource/senders/" SENDER_ID) != NULL);
    CHECK(strstr(registry.requests[before + 1].url, "resource/flows/") != NULL);
    CHECK(strstr(registry.requests[before + 2].url, "resource/sources/") != NULL);
    CHECK_STR(registry.requests[before + 3].type, "device");
    const int kept = registry.count;
    DtNmosNode_Destroy(node);
    // The end of the node deletes the receiver, the device and the node.
    REQUIRE(registry.count == kept + 3);
    CHECK(strstr(registry.requests[kept].url, "resource/receivers/" RECEIVER_ID) != NULL);
    CHECK(strstr(registry.requests[kept + 1].url, "resource/devices/" DEVICE_ID) != NULL);
    CHECK(strstr(registry.requests[kept + 2].url, "resource/nodes/" NODE_ID) != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Asks the node for path and returns the response, which the caller frees.
//
static DtNmosHttpResponse* ask(DtNmosNode* node, const char* method, const char* path)
{
    DtNmosHttpRequest request;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Method = method;
    request.Url = path;
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    DtNmosError error = {DTNMOS_OK, ""};
    CHECK(DtNmosNode_Handle(node, &request, response, &error) == DTNMOS_OK);
    return response;
}

// .-.-.-.-.-.-.-.-.-.- node_answers_its_node_api_and_transport_files -.-.-.-.-.-.-.-.-.-.
//
void node_answers_its_node_api_and_transport_files(void)
{
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    DtNmosNode* node = make_node(&registry, "192.168.1.5", 8080, record_http);
    REQUIRE(node != NULL);
    DtNmosHttpResponse* response = ask(node, "GET", "/x-nmos/node/v1.3/self");
    CHECK_EQ(DtNmosHttpResponse_Status(response), 200);
    dtnmos_json* json = NULL;
    size_t length = 0;
    const char* body = DtNmosHttpResponse_Body(response, &length);
    REQUIRE(dtnmos_json_parse(body, length, &json, NULL) == DTNMOS_OK);
    CHECK_STR(dtnmos_json_member_text(json, "id"), NODE_ID);
    CHECK_STR(dtnmos_json_member_text(json, "href"), "http://192.168.1.5:8080/");
    dtnmos_json_free(json);
    DtNmosHttpResponse_Free(response);

    response = ask(node, "GET", "/x-nmos/node/v1.3/senders/?paging.limit=10");
    body = DtNmosHttpResponse_Body(response, &length);
    REQUIRE(dtnmos_json_parse(body, length, &json, NULL) == DTNMOS_OK);
    REQUIRE(json->type == DTNMOS_JSON_ARRAY && json->count == 1);
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "manifest_href"),
              "http://192.168.1.5:8080/x-nmos/connection/v1.1/single/senders/" SENDER_ID
              "/transportfile");
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "transport"),
              "urn:x-nmos:transport:rtp.mcast");
    dtnmos_json_free(json);
    DtNmosHttpResponse_Free(response);

    response = ask(node, "GET", "/x-nmos/node/v1.3/flows");
    body = DtNmosHttpResponse_Body(response, &length);
    REQUIRE(dtnmos_json_parse(body, length, &json, NULL) == DTNMOS_OK);
    REQUIRE(json->count == 1);
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "interlace_mode"),
              "interlaced_tff");
    CHECK_STR(dtnmos_json_member_text(&json->items[0], "media_type"), "video/raw");
    dtnmos_json_free(json);
    DtNmosHttpResponse_Free(response);

    // The transport file is the SDP of the flow of the sender.
    response = ask(node, "GET",
                   "/x-nmos/connection/v1.1/single/senders/" SENDER_ID "/transportfile");
    CHECK_EQ(DtNmosHttpResponse_Status(response), 200);
    CHECK_STR(DtNmosHttpResponse_ContentType(response), "application/sdp");
    body = DtNmosHttpResponse_Body(response, &length);
    DtNmosSdp* sdp = NULL;
    REQUIRE(DtNmosSdp_Parse(body, length, &sdp, NULL) == DTNMOS_OK);
    CHECK_EQ(DtNmosSdp_Flow(sdp, 0)->Format.Video.Height, 1080);
    CHECK(DtNmosSdp_Flow(sdp, 0)->Format.Video.Interlaced);
    CHECK_STR(DtNmosString_Get(&DtNmosSdp_Session(sdp)->OriginIp), "192.168.1.5");
    DtNmosSdp_Free(sdp);
    DtNmosHttpResponse_Free(response);

    const char* const missing[] = {"/x-nmos/node/v1.3/senders/nobody", "/x-nmos/nothing",
                                   "/x-nmos/node/v1.3/clocks"};
    for (size_t i = 0; i < 3; ++i)
    {
        response = ask(node, "GET", missing[i]);
        CHECK_EQ(DtNmosHttpResponse_Status(response), 404);
        DtNmosHttpResponse_Free(response);
    }
    response = ask(node, "POST", "/x-nmos/node/v1.3/self");
    CHECK_EQ(DtNmosHttpResponse_Status(response), 405);
    DtNmosHttpResponse_Free(response);
    DtNmosNode_Destroy(node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- node_serves_itself_over_http -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void node_serves_itself_over_http(void)
{
    if (!DtNmos_HasServer() || !DtNmos_HasCurl())
    {
        printf("  skipped: the library has no server or no libcurl\n");
        return;
    }
    fake_registration registry;
    memset(&registry, 0, sizeof(registry));
    registry.heartbeat_status = 200;
    // The registry is fake, the node's own server is real.
    DtNmosNode* node = make_node(&registry, "127.0.0.1", 0, record_http);
    REQUIRE(node != NULL);
    DtNmosError error = {DTNMOS_OK, ""};
    const DtNmosResult served = DtNmosNode_Serve(node, &error);
    if (served != DTNMOS_OK)
    {
        printf("  %s\n", error.Message);
    }
    REQUIRE(served == DTNMOS_OK);
    CHECK(DtNmosNode_ApiPort(node) != 0);
    DtNmosString base = {0};
    REQUIRE(DtNmosNode_ApiUrl(node, &base) == DTNMOS_OK);
    char url[256];
    snprintf(url, sizeof(url), "%s/x-nmos/node/v1.3/self", DtNmosString_Get(&base));
    DtNmosHttpRequest request = {sizeof(request), "GET", url, NULL, NULL, 0, 3000};
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    REQUIRE(DtNmos_CurlHttp(NULL, &request, response, &error) == DTNMOS_OK);
    CHECK_EQ(DtNmosHttpResponse_Status(response), 200);
    CHECK(strstr(DtNmosHttpResponse_Body(response, NULL), NODE_ID) != NULL);
    DtNmosHttpResponse_Free(response);
    // The thread of the server polls the node, which registers.
    for (int wait = 0; wait < 100 && !DtNmosNode_IsRegistered(node); ++wait)
    {
        dtnmos_sleep_ms(20);
    }
    CHECK(DtNmosNode_IsRegistered(node));
    DtNmosString_Clear(&base);
    DtNmosNode_Destroy(node);
}
