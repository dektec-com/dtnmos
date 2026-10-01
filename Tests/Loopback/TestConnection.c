// #*#*#*#*#*#*#*#*#*#*#*#*#* TestConnection.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the Connection API of the node (IS-05)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "check.h"
#include "dtnmos_node.h"
#include "tests.h"

#define NODE_ID "bbbbbbbb-0000-4000-8000-000000000001"
#define DEVICE_ID "bbbbbbbb-0000-4000-8000-000000000002"
#define SENDER_ID "bbbbbbbb-0000-4000-8000-000000000003"
#define RECEIVER_ID "bbbbbbbb-0000-4000-8000-000000000004"
#define PEER_ID "bbbbbbbb-0000-4000-8000-000000000005"

#define CONNECTION "/x-nmos/connection/v1.1/single/"

// What the callbacks of the node received, and what they answer.
typedef struct activations
{
    int sender_calls;
    int sender_enabled;
    char destination[64];
    int destination_port;
    char source[64];
    int receiver_calls;
    int receiver_enabled;
    int has_flow;
    DtNmosMedia media;
    char receives[64];
    int receives_port;
    char sender_id[37];
    DtNmosResult answer;
    int registrations; // of the registry: POSTs of a resource
    char last_registered[16];
} activations;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- activate_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult activate_sender(void* user, const DtNmosId* sender,
                                    const DtNmosSenderActivation* activation,
                                    DtNmosError* error)
{
    activations* seen = user;
    CHECK_STR(sender->Text, SENDER_ID);
    ++seen->sender_calls;
    seen->sender_enabled = activation->MasterEnable;
    snprintf(seen->destination, sizeof(seen->destination), "%s",
             DtNmosString_Get(&activation->DestinationIp));
    seen->destination_port = activation->DestinationPort;
    snprintf(seen->source, sizeof(seen->source), "%s",
             DtNmosString_Get(&activation->SourceIp));
    if (seen->answer != DTNMOS_OK)
    {
        snprintf(error->Message, sizeof(error->Message), "the card refused it");
    }
    return seen->answer;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- activate_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult activate_receiver(void* user, const DtNmosId* receiver,
                                      const DtNmosReceiverActivation* activation,
                                      DtNmosError* error)
{
    (void)error;
    activations* seen = user;
    CHECK_STR(receiver->Text, RECEIVER_ID);
    ++seen->receiver_calls;
    seen->receiver_enabled = activation->MasterEnable;
    seen->has_flow = activation->HasFlow;
    seen->media = activation->Flow.Media;
    snprintf(seen->receives, sizeof(seen->receives), "%s",
             DtNmosString_Get(&activation->Flow.DestinationIp));
    seen->receives_port = activation->Flow.DestinationPort;
    snprintf(seen->sender_id, sizeof(seen->sender_id), "%s", activation->SenderId.Text);
    return seen->answer;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- registry_http -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The registry records the type of what is registered, and accepts it all.
//
static DtNmosResult registry_http(void* user, const DtNmosHttpRequest* request,
                                  DtNmosHttpResponse* response, DtNmosError* error)
{
    (void)error;
    activations* seen = user;
    int status = 200;
    if (strcmp(request->Method, "POST") == 0 && strstr(request->Url, "/resource") != NULL)
    {
        dtnmos_json* json = NULL;
        if (dtnmos_json_parse(request->Body, request->BodyLength, &json, NULL) ==
            DTNMOS_OK)
        {
            const char* type = dtnmos_json_member_text(json, "type");
            snprintf(seen->last_registered, sizeof(seen->last_registered), "%s",
                     type == NULL ? "?" : type);
            dtnmos_json_free(json);
        }
        ++seen->registrations;
        status = 201;
    }
    else if (strcmp(request->Method, "DELETE") == 0)
    {
        status = 204;
    }
    DtNmosHttpResponse_SetStatus(response, status);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_node -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes a node with a video sender to 239.0.0.1:5004 and an audio receiver, whose
// callbacks record into seen, and registers them.
//
static DtNmosNode* make_node(activations* seen)
{
    DtNmosNodeConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Id = (DtNmosId){NODE_ID};
    config.Label = "connection node";
    config.ApiHost = "192.168.1.5";
    config.ApiPort = 8080;
    config.RegistrationUrl = "http://registry.test";
    config.Http = registry_http;
    config.HttpUser = seen;
    DtNmosNode* node = NULL;
    DtNmosError error = {DTNMOS_OK, ""};
    if (DtNmosNode_Create(&config, &node, &error) != DTNMOS_OK)
    {
        printf("  %s\n", error.Message);
        return NULL;
    }
    DtNmosDeviceConfig device = {sizeof(device), {DEVICE_ID}, "a card", ""};
    CHECK(DtNmosNode_AddDevice(node, &device, &error) == DTNMOS_OK);
    DtNmosFlow flow = {0};
    flow.Size = sizeof(flow);
    flow.Media = DTNMOS_MEDIA_VIDEO;
    DtNmosString_SetText(&flow.DestinationIp, "239.0.0.1");
    flow.DestinationPort = 5004;
    flow.PayloadType = 96;
    flow.ClockRate = 90000;
    flow.Format.Video.Width = 1280;
    flow.Format.Video.Height = 720;
    flow.Format.Video.RateNumerator = 50;
    flow.Format.Video.RateDenominator = 1;
    flow.Format.Video.Depth = 10;
    DtNmosString_SetText(&flow.Format.Video.Sampling, "YCbCr-4:2:2");
    DtNmosSenderConfig sender = {sizeof(sender), {SENDER_ID},  {DEVICE_ID}, "camera", "",
                                 &flow,          "192.168.1.5"};
    CHECK(DtNmosNode_AddSender(node, &sender, activate_sender, seen, &error) ==
          DTNMOS_OK);
    DtNmosFlow_Clear(&flow);
    DtNmosReceiverConfig receiver = {
        sizeof(receiver), {RECEIVER_ID}, {DEVICE_ID}, "monitor", "", DTNMOS_MEDIA_AUDIO};
    CHECK(DtNmosNode_AddReceiver(node, &receiver, activate_receiver, seen, &error) ==
          DTNMOS_OK);
    CHECK(DtNmosNode_Poll(node, NULL, &error) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(node));
    return node;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends method to path with body, and returns the status; json, when not null, gets the
// JSON of the answer, which the caller frees.
//
static int ask(DtNmosNode* node, const char* method, const char* path, const char* body,
               dtnmos_json** json)
{
    DtNmosHttpRequest request;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Method = method;
    request.Url = path;
    if (body != NULL)
    {
        request.ContentType = "application/json";
        request.Body = body;
        request.BodyLength = strlen(body);
    }
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    DtNmosError error = {DTNMOS_OK, ""};
    CHECK(DtNmosNode_Handle(node, &request, response, &error) == DTNMOS_OK);
    const int status = DtNmosHttpResponse_Status(response);
    if (json != NULL)
    {
        size_t length = 0;
        const char* text = DtNmosHttpResponse_Body(response, &length);
        *json = NULL;
        if (dtnmos_json_parse(text, length, json, NULL) != DTNMOS_OK)
        {
            printf("  no JSON from %s %s: %.*s\n", method, path, (int)length, text);
        }
    }
    DtNmosHttpResponse_Free(response);
    return status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- leg_member -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the member name of the one leg of the transport parameters of json.
//
static const dtnmos_json* leg_member(const dtnmos_json* json, const char* name)
{
    const dtnmos_json* legs = dtnmos_json_member(json, "transport_params");
    if (legs == NULL || legs->type != DTNMOS_JSON_ARRAY || legs->count != 1)
    {
        return NULL;
    }
    return dtnmos_json_member(&legs->items[0], name);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- connection_answers_its_parameters -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void connection_answers_its_parameters(void)
{
    activations seen;
    memset(&seen, 0, sizeof(seen));
    DtNmosNode* node = make_node(&seen);
    REQUIRE(node != NULL);
    dtnmos_json* json = NULL;
    CHECK_EQ(ask(node, "GET", "/x-nmos/connection/v1.1/", NULL, &json), 200);
    REQUIRE(json != NULL && json->type == DTNMOS_JSON_ARRAY && json->count == 2);
    dtnmos_json_free(json);

    CHECK_EQ(ask(node, "GET", CONNECTION "senders/", NULL, &json), 200);
    REQUIRE(json != NULL && json->count == 1);
    CHECK_STR(dtnmos_json_text(&json->items[0]), SENDER_ID "/");
    dtnmos_json_free(json);
    CHECK_EQ(ask(node, "GET", CONNECTION "receivers", NULL, &json), 200);
    REQUIRE(json != NULL && json->count == 1);
    CHECK_STR(dtnmos_json_text(&json->items[0]), RECEIVER_ID "/");
    dtnmos_json_free(json);

    CHECK_EQ(ask(node, "GET", CONNECTION "senders/" SENDER_ID, NULL, &json), 200);
    REQUIRE(json != NULL);
    CHECK_EQ(json->count, 5);
    dtnmos_json_free(json);
    CHECK_EQ(ask(node, "GET", CONNECTION "receivers/" RECEIVER_ID "/transporttype", NULL,
                 &json),
             200);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_text(json), "urn:x-nmos:transport:rtp");
    dtnmos_json_free(json);
    CHECK_EQ(
        ask(node, "GET", CONNECTION "senders/" SENDER_ID "/constraints", NULL, &json),
        200);
    REQUIRE(json != NULL && json->type == DTNMOS_JSON_ARRAY && json->count == 1);
    CHECK(dtnmos_json_member(&json->items[0], "destination_ip") != NULL);
    dtnmos_json_free(json);

    // The active parameters of the sender are where its element sends.
    CHECK_EQ(ask(node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &json),
             200);
    REQUIRE(json != NULL);
    CHECK(dtnmos_json_member(json, "master_enable")->type == DTNMOS_JSON_TRUE);
    CHECK(dtnmos_json_member(json, "receiver_id")->type == DTNMOS_JSON_NULL);
    CHECK_STR(dtnmos_json_text(leg_member(json, "destination_ip")), "239.0.0.1");
    CHECK_EQ(leg_member(json, "destination_port")->number, 5004);
    CHECK_STR(dtnmos_json_text(leg_member(json, "source_ip")), "192.168.1.5");
    dtnmos_json_free(json);
    CHECK_EQ(ask(node, "GET", CONNECTION "receivers/" RECEIVER_ID "/staged", NULL, &json),
             200);
    REQUIRE(json != NULL);
    const dtnmos_json* file = dtnmos_json_member(json, "transport_file");
    REQUIRE(file != NULL);
    CHECK(dtnmos_json_member(file, "data")->type == DTNMOS_JSON_NULL);
    CHECK_STR(dtnmos_json_text(leg_member(json, "destination_port")), "auto");
    dtnmos_json_free(json);

    // A receiver has no transport file, and unknown resources are not found.
    CHECK_EQ(ask(node, "GET", CONNECTION "receivers/" RECEIVER_ID "/transportfile", NULL,
                 NULL),
             404);
    CHECK_EQ(ask(node, "GET", CONNECTION "senders/" PEER_ID "/active", NULL, NULL), 404);
    CHECK_EQ(ask(node, "GET", CONNECTION "flows/", NULL, NULL), 404);
    CHECK_EQ(ask(node, "PUT", CONNECTION "senders/" SENDER_ID "/staged", "{}", NULL),
             405);
    CHECK_EQ(ask(node, "PATCH", CONNECTION "senders/" SENDER_ID "/active", "{}", NULL),
             405);
    CHECK_EQ(ask(node, "POST", "/x-nmos/connection/v1.1/bulk/senders", "[]", NULL), 501);
    CHECK_EQ(seen.sender_calls + seen.receiver_calls, 0);
    DtNmosNode_Destroy(node);
}

static const char* const connect_receiver =
    "{\"sender_id\": \"" PEER_ID "\", \"master_enable\": true, "
    "\"activation\": {\"mode\": \"activate_immediate\"}, "
    "\"transport_file\": {\"type\": \"application/sdp\", \"data\": "
    "\"v=0\\no=- 1 1 IN IP4 192.168.1.7\\ns=peer\\nt=0 0\\n"
    "m=video 5000 RTP/AVP 96\\nc=IN IP4 239.1.1.1/64\\na=rtpmap:96 raw/90000\\n"
    "m=audio 5006 RTP/AVP 97\\nc=IN IP4 239.1.1.2/64\\na=rtpmap:97 L24/48000/2\\n"
    "a=ptime:1\\n\"}, "
    "\"transport_params\": [{\"destination_port\": 5008}]}";

// .-.-.-.-.-.-.-.-.-.-.-.-.- connection_connects_a_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void connection_connects_a_receiver(void)
{
    activations seen;
    memset(&seen, 0, sizeof(seen));
    DtNmosNode* node = make_node(&seen);
    REQUIRE(node != NULL);
    const int registered = seen.registrations;
    dtnmos_json* json = NULL;
    CHECK_EQ(ask(node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                 connect_receiver, &json),
             200);
    REQUIRE(json != NULL);
    const dtnmos_json* activation = dtnmos_json_member(json, "activation");
    CHECK_STR(dtnmos_json_member_text(activation, "mode"), "activate_immediate");
    CHECK(dtnmos_json_member_text(activation, "activation_time") != NULL);
    dtnmos_json_free(json);

    // The callback gets the audio flow of the transport file, on the port of the PATCH.
    CHECK_EQ(seen.receiver_calls, 1);
    CHECK(seen.receiver_enabled);
    CHECK(seen.has_flow);
    CHECK_EQ(seen.media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(seen.receives, "239.1.1.2");
    CHECK_EQ(seen.receives_port, 5008);
    CHECK_STR(seen.sender_id, PEER_ID);

    CHECK_EQ(ask(node, "GET", CONNECTION "receivers/" RECEIVER_ID "/active", NULL, &json),
             200);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_member_text(json, "sender_id"), PEER_ID);
    CHECK(dtnmos_json_member_text(dtnmos_json_member(json, "transport_file"), "data") !=
          NULL);
    dtnmos_json_free(json);

    // The receiver registers its subscription anew.
    CHECK_EQ(ask(node, "GET", "/x-nmos/node/v1.3/receivers/" RECEIVER_ID, NULL, &json),
             200);
    REQUIRE(json != NULL);
    const dtnmos_json* subscription = dtnmos_json_member(json, "subscription");
    CHECK_STR(dtnmos_json_member_text(subscription, "sender_id"), PEER_ID);
    CHECK(dtnmos_json_member(subscription, "active")->type == DTNMOS_JSON_TRUE);
    dtnmos_json_free(json);
    CHECK(!DtNmosNode_IsRegistered(node));
    CHECK(DtNmosNode_Poll(node, NULL, NULL) == DTNMOS_OK);
    CHECK_EQ(seen.registrations, registered + 1);
    CHECK_STR(seen.last_registered, "receiver");

    // Disabling hands no flow over, and keeps what it received from.
    CHECK_EQ(ask(node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                 "{\"master_enable\": false, \"transport_file\": {\"data\": null}, "
                 "\"activation\": {\"mode\": \"activate_immediate\"}}",
                 NULL),
             200);
    CHECK_EQ(seen.receiver_calls, 2);
    CHECK(!seen.receiver_enabled);
    CHECK(!seen.has_flow);
    DtNmosNode_Destroy(node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- connection_moves_a_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void connection_moves_a_sender(void)
{
    activations seen;
    memset(&seen, 0, sizeof(seen));
    DtNmosNode* node = make_node(&seen);
    REQUIRE(node != NULL);

    // A PATCH without activation stages only.
    dtnmos_json* json = NULL;
    CHECK_EQ(ask(node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                 "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\", "
                 "\"destination_port\": 6000}]}",
                 &json),
             200);
    REQUIRE(json != NULL);
    CHECK(dtnmos_json_member(dtnmos_json_member(json, "activation"), "mode")->type ==
          DTNMOS_JSON_NULL);
    dtnmos_json_free(json);
    CHECK_EQ(seen.sender_calls, 0);
    CHECK_EQ(ask(node, "GET", CONNECTION "senders/" SENDER_ID "/staged", NULL, &json),
             200);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_text(leg_member(json, "destination_ip")), "192.168.1.9");
    dtnmos_json_free(json);
    CHECK_EQ(ask(node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &json),
             200);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_text(leg_member(json, "destination_ip")), "239.0.0.1");
    dtnmos_json_free(json);

    // The activation moves the sender, and its transport file with it.
    CHECK_EQ(ask(node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                 "{\"receiver_id\": \"" PEER_ID "\", "
                 "\"activation\": {\"mode\": \"activate_immediate\", \"requested_time\": "
                 "null}}",
                 NULL),
             200);
    CHECK_EQ(seen.sender_calls, 1);
    CHECK(seen.sender_enabled);
    CHECK_STR(seen.destination, "192.168.1.9");
    CHECK_EQ(seen.destination_port, 6000);
    CHECK_STR(seen.source, "192.168.1.5");
    CHECK_EQ(
        ask(node, "GET", CONNECTION "senders/" SENDER_ID "/transportfile", NULL, NULL),
        200);
    DtNmosHttpRequest request;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Method = "GET";
    request.Url = CONNECTION "senders/" SENDER_ID "/transportfile";
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    CHECK(DtNmosNode_Handle(node, &request, response, NULL) == DTNMOS_OK);
    size_t length = 0;
    const char* text = DtNmosHttpResponse_Body(response, &length);
    DtNmosSdp* sdp = NULL;
    REQUIRE(DtNmosSdp_Parse(text, length, &sdp, NULL) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&DtNmosSdp_Flow(sdp, 0)->DestinationIp), "192.168.1.9");
    CHECK_EQ(DtNmosSdp_Flow(sdp, 0)->DestinationPort, 6000);
    CHECK_EQ(DtNmosSdp_Session(sdp)->SessionVersion, 2);
    DtNmosSdp_Free(sdp);
    DtNmosHttpResponse_Free(response);

    CHECK_EQ(ask(node, "GET", "/x-nmos/node/v1.3/senders/" SENDER_ID, NULL, &json), 200);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_member_text(json, "transport"),
              "urn:x-nmos:transport:rtp.ucast");
    CHECK_STR(
        dtnmos_json_member_text(dtnmos_json_member(json, "subscription"), "receiver_id"),
        PEER_ID);
    dtnmos_json_free(json);

    // Disabling it keeps where it sends.
    CHECK_EQ(ask(node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                 "{\"master_enable\": false, \"activation\": {\"mode\": "
                 "\"activate_immediate\"}}",
                 NULL),
             200);
    CHECK_EQ(seen.sender_calls, 2);
    CHECK(!seen.sender_enabled);
    CHECK_STR(seen.destination, "192.168.1.9");
    CHECK_EQ(ask(node, "GET", "/x-nmos/node/v1.3/senders/" SENDER_ID, NULL, &json), 200);
    REQUIRE(json != NULL);
    CHECK(dtnmos_json_member(dtnmos_json_member(json, "subscription"), "active")->type ==
          DTNMOS_JSON_FALSE);
    dtnmos_json_free(json);
    DtNmosNode_Destroy(node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- connection_refuses_bad_patches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void connection_refuses_bad_patches(void)
{
    activations seen;
    memset(&seen, 0, sizeof(seen));
    DtNmosNode* node = make_node(&seen);
    REQUIRE(node != NULL);
    const char* const staged = CONNECTION "senders/" SENDER_ID "/staged";
    CHECK_EQ(ask(node, "PATCH", staged, "{\"master_enable\": ", NULL), 400);
    CHECK_EQ(ask(node, "PATCH", staged, "[]", NULL), 400);
    CHECK_EQ(ask(node, "PATCH", staged, "{\"colour\": \"red\"}", NULL), 400);
    CHECK_EQ(ask(node, "PATCH", staged, "{\"master_enable\": 1}", NULL), 400);
    CHECK_EQ(ask(node, "PATCH", staged,
                 "{\"transport_params\": [{\"multicast_ip\": null}]}", NULL),
             400);
    CHECK_EQ(ask(node, "PATCH", staged,
                 "{\"transport_params\": [{\"destination_port\": 70000}]}", NULL),
             400);
    CHECK_EQ(ask(node, "PATCH", staged, "{\"transport_params\": [{}, {}]}", NULL), 400);
    CHECK_EQ(ask(node, "PATCH", staged,
                 "{\"activation\": {\"mode\": \"activate_scheduled_relative\", "
                 "\"requested_time\": \"0:0\"}}",
                 NULL),
             501);
    CHECK_EQ(ask(node, "PATCH", CONNECTION "senders/" PEER_ID "/staged", "{}", NULL),
             404);
    // A transport file without audio does not connect the audio receiver.
    CHECK_EQ(
        ask(node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
            "{\"transport_file\": {\"data\": \"v=0\\no=- 1 1 IN IP4 10.0.0.1\\ns=x\\n"
            "t=0 0\\nm=video 5000 RTP/AVP 96\\nc=IN IP4 239.1.1.1\\n"
            "a=rtpmap:96 raw/90000\\n\"}, "
            "\"activation\": {\"mode\": \"activate_immediate\"}}",
            NULL),
        400);
    CHECK_EQ(seen.receiver_calls, 0);

    // An activation the callback fails leaves the active parameters as they were.
    seen.answer = DTNMOS_E_STATE;
    dtnmos_json* json = NULL;
    CHECK_EQ(ask(node, "PATCH", staged,
                 "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\"}], "
                 "\"activation\": {\"mode\": \"activate_immediate\"}}",
                 &json),
             500);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_member_text(json, "error"), "the card refused it");
    dtnmos_json_free(json);
    CHECK_EQ(seen.sender_calls, 1);
    CHECK_EQ(ask(node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &json),
             200);
    REQUIRE(json != NULL);
    CHECK_STR(dtnmos_json_text(leg_member(json, "destination_ip")), "239.0.0.1");
    dtnmos_json_free(json);
    CHECK(DtNmosNode_IsRegistered(node));
    DtNmosNode_Destroy(node);
}
