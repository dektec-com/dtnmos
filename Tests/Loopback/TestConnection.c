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
typedef struct NmosActivations
{
    int SenderCalls;
    int SenderEnabled;
    char Destination[DTNMOS_MAX_ADDRESS_SIZE];
    int DestinationPort;
    char Source[DTNMOS_MAX_ADDRESS_SIZE];
    int ReceiverCalls;
    int ReceiverEnabled;
    int HasFlow;
    DtNmosMedia Media;
    char Receives[DTNMOS_MAX_ADDRESS_SIZE];
    int ReceivesPort;
    char SenderId[37];
    DtNmosResult Answer;
    int Registrations; // of the registry: POSTs of a resource
    char LastRegistered[16];
} NmosActivations;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ActivateSender(void* User, const DtNmosId* Sender,
                                   const DtNmosSenderActivation* Activation)
{
    NmosActivations* Seen = User;
    CHECK_STR(Sender->Text, SENDER_ID);
    ++Seen->SenderCalls;
    Seen->SenderEnabled = Activation->MasterEnable;
    snprintf(Seen->Destination, sizeof(Seen->Destination), "%s",
             Activation->DestinationIp);
    Seen->DestinationPort = Activation->DestinationPort;
    snprintf(Seen->Source, sizeof(Seen->Source), "%s", Activation->SourceIp);
    if (Seen->Answer != DTNMOS_OK)
    {
        return DtNmos_SetLastError(Seen->Answer, "the card refused it");
    }
    return Seen->Answer;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ActivateReceiver(void* User, const DtNmosId* Receiver,
                                     const DtNmosReceiverActivation* Activation)
{
    NmosActivations* Seen = User;
    CHECK_STR(Receiver->Text, RECEIVER_ID);
    ++Seen->ReceiverCalls;
    Seen->ReceiverEnabled = Activation->MasterEnable;
    Seen->HasFlow = Activation->HasFlow;
    Seen->Media = Activation->Flow.Media;
    snprintf(Seen->Receives, sizeof(Seen->Receives), "%s",
             Activation->Flow.DestinationIp);
    Seen->ReceivesPort = Activation->Flow.DestinationPort;
    snprintf(Seen->SenderId, sizeof(Seen->SenderId), "%s", Activation->SenderId.Text);
    return Seen->Answer;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistryHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The registry records the type of what is registered, and accepts it all.
//
static DtNmosResult RegistryHttp(void* User, const DtNmosHttpRequest* Request,
                                 DtNmosHttpResponse* Response)
{
    NmosActivations* Seen = User;
    int Status = 200;
    if (strcmp(Request->Method, "POST") == 0 && strstr(Request->Url, "/resource") != NULL)
    {
        NmosJson* Json = NULL;
        if (NmosJson_Parse(Request->Body, Request->BodyLength, &Json) == DTNMOS_OK)
        {
            const char* Type = NmosJson_MemberText(Json, "type");
            snprintf(Seen->LastRegistered, sizeof(Seen->LastRegistered), "%s",
                     Type == NULL ? "?" : Type);
            NmosJson_Free(Json);
        }
        ++Seen->Registrations;
        Status = 201;
    }
    else if (strcmp(Request->Method, "DELETE") == 0)
    {
        Status = 204;
    }
    DtNmosHttpResponse_SetStatus(Response, Status);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes a node with a video sender to 239.0.0.1:5004 and an audio receiver, whose
// callbacks record into seen, and registers them.
//
static DtNmosNode* MakeNode(NmosActivations* Seen)
{
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Id = (DtNmosId){NODE_ID};
    Config.Label = "connection node";
    Config.ApiHost = "192.168.1.5";
    Config.ApiPort = 8080;
    Config.RegistrationUrl = "http://registry.test";
    Config.Http = RegistryHttp;
    Config.HttpUser = Seen;
    DtNmosNode* Node = DtNmosNode_Alloc();
    if (Node == NULL || DtNmosNode_Open(Node, &Config) != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
        DtNmosNode_Free(Node);
        return NULL;
    }
    DtNmosDeviceConfig Device = {sizeof(Device), {DEVICE_ID}, "a card", ""};
    CHECK(DtNmosNode_AddDevice(Node, &Device) == DTNMOS_OK);
    DtNmosFlow Flow = {0};
    Flow.Size = sizeof(Flow);
    Flow.Media = DTNMOS_MEDIA_VIDEO;
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "%s", "239.0.0.1");
    Flow.DestinationPort = 5004;
    Flow.PayloadType = 96;
    Flow.ClockRate = 90000;
    Flow.Format.Video.Width = 1280;
    Flow.Format.Video.Height = 720;
    Flow.Format.Video.RateNumerator = 50;
    Flow.Format.Video.RateDenominator = 1;
    Flow.Format.Video.Depth = 10;
    snprintf(Flow.Format.Video.Sampling, sizeof(Flow.Format.Video.Sampling), "%s",
             "YCbCr-4:2:2");
    DtNmosSenderConfig Sender = {sizeof(Sender), {SENDER_ID},  {DEVICE_ID}, "camera", "",
                                 &Flow,          "192.168.1.5"};
    CHECK(DtNmosNode_AddSender(Node, &Sender, ActivateSender, Seen) == DTNMOS_OK);
    DtNmosReceiverConfig Receiver = {
        sizeof(Receiver), {RECEIVER_ID}, {DEVICE_ID}, "monitor", "", DTNMOS_MEDIA_AUDIO};
    CHECK(DtNmosNode_AddReceiver(Node, &Receiver, ActivateReceiver, Seen) == DTNMOS_OK);
    CHECK(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(Node));
    return Node;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends method to path with body, and returns the status; json, when not null, gets the
// JSON of the answer, which the caller frees.
//
static int Ask(DtNmosNode* Node, const char* Method, const char* Path, const char* Body,
               NmosJson** Json)
{
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = Method;
    Request.Url = Path;
    if (Body != NULL)
    {
        Request.ContentType = "application/json";
        Request.Body = Body;
        Request.BodyLength = strlen(Body);
    }
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    CHECK(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
    const int Status = DtNmosHttpResponse_Status(Response);
    if (Json != NULL)
    {
        size_t Length = 0;
        const char* Text = DtNmosHttpResponse_Body(Response, &Length);
        *Json = NULL;
        if (NmosJson_Parse(Text, Length, Json) != DTNMOS_OK)
        {
            printf("  no JSON from %s %s: %.*s\n", Method, Path, (int)Length, Text);
        }
    }
    DtNmosHttpResponse_Free(Response);
    return Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LegMember -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the member name of the one leg of the transport parameters of json.
//
static const NmosJson* LegMember(const NmosJson* Json, const char* name)
{
    const NmosJson* Legs = NmosJson_Member(Json, "transport_params");
    if (Legs == NULL || Legs->Type != DTNMOS_JSON_ARRAY || Legs->Count != 1)
    {
        return NULL;
    }
    return NmosJson_Member(&Legs->Items[0], name);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- connection_answers_its_parameters -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void connection_answers_its_parameters(void)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    REQUIRE(Node != NULL);
    NmosJson* Json = NULL;
    CHECK_EQ(Ask(Node, "GET", "/x-nmos/connection/v1.1/", NULL, &Json), 200);
    REQUIRE(Json != NULL && Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 2);
    NmosJson_Free(Json);

    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/", NULL, &Json), 200);
    REQUIRE(Json != NULL && Json->Count == 1);
    CHECK_STR(NmosJson_Text(&Json->Items[0]), SENDER_ID "/");
    NmosJson_Free(Json);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "receivers", NULL, &Json), 200);
    REQUIRE(Json != NULL && Json->Count == 1);
    CHECK_STR(NmosJson_Text(&Json->Items[0]), RECEIVER_ID "/");
    NmosJson_Free(Json);

    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/" SENDER_ID, NULL, &Json), 200);
    REQUIRE(Json != NULL);
    CHECK_EQ(Json->Count, 5);
    NmosJson_Free(Json);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/transporttype", NULL,
                 &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_Text(Json), "urn:x-nmos:transport:rtp");
    NmosJson_Free(Json);
    CHECK_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/constraints", NULL, &Json),
        200);
    REQUIRE(Json != NULL && Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 1);
    CHECK(NmosJson_Member(&Json->Items[0], "destination_ip") != NULL);
    NmosJson_Free(Json);

    // The active parameters of the sender are where its element sends.
    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK(NmosJson_Member(Json, "master_enable")->Type == DTNMOS_JSON_TRUE);
    CHECK(NmosJson_Member(Json, "receiver_id")->Type == DTNMOS_JSON_NULL);
    CHECK_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "239.0.0.1");
    CHECK_EQ(LegMember(Json, "destination_port")->Number, 5004);
    CHECK_STR(NmosJson_Text(LegMember(Json, "source_ip")), "192.168.1.5");
    NmosJson_Free(Json);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/staged", NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    const NmosJson* File = NmosJson_Member(Json, "transport_file");
    REQUIRE(File != NULL);
    CHECK(NmosJson_Member(File, "data")->Type == DTNMOS_JSON_NULL);
    CHECK_STR(NmosJson_Text(LegMember(Json, "destination_port")), "auto");
    NmosJson_Free(Json);

    // A receiver has no transport file, and unknown resources are not found.
    CHECK_EQ(Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/transportfile", NULL,
                 NULL),
             404);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/" PEER_ID "/active", NULL, NULL), 404);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "flows/", NULL, NULL), 404);
    CHECK_EQ(Ask(Node, "PUT", CONNECTION "senders/" SENDER_ID "/staged", "{}", NULL),
             405);
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/active", "{}", NULL),
             405);
    CHECK_EQ(Ask(Node, "POST", "/x-nmos/connection/v1.1/bulk/senders", "[]", NULL), 501);
    CHECK_EQ(Seen.SenderCalls + Seen.ReceiverCalls, 0);
    DtNmosNode_Free(Node);
}

static const char* const ConnectReceiver =
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
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    REQUIRE(Node != NULL);
    const int Registered = Seen.Registrations;
    NmosJson* Json = NULL;
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                 ConnectReceiver, &Json),
             200);
    REQUIRE(Json != NULL);
    const NmosJson* Activation = NmosJson_Member(Json, "activation");
    CHECK_STR(NmosJson_MemberText(Activation, "mode"), "activate_immediate");
    CHECK(NmosJson_MemberText(Activation, "activation_time") != NULL);
    NmosJson_Free(Json);

    // The callback gets the audio flow of the transport file, on the port of the PATCH.
    CHECK_EQ(Seen.ReceiverCalls, 1);
    CHECK(Seen.ReceiverEnabled);
    CHECK(Seen.HasFlow);
    CHECK_EQ(Seen.Media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(Seen.Receives, "239.1.1.2");
    CHECK_EQ(Seen.ReceivesPort, 5008);
    CHECK_STR(Seen.SenderId, PEER_ID);

    CHECK_EQ(Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/active", NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_MemberText(Json, "sender_id"), PEER_ID);
    CHECK(NmosJson_MemberText(NmosJson_Member(Json, "transport_file"), "data") != NULL);
    NmosJson_Free(Json);

    // The receiver registers its subscription anew.
    CHECK_EQ(Ask(Node, "GET", "/x-nmos/node/v1.3/receivers/" RECEIVER_ID, NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    const NmosJson* Subscription = NmosJson_Member(Json, "subscription");
    CHECK_STR(NmosJson_MemberText(Subscription, "sender_id"), PEER_ID);
    CHECK(NmosJson_Member(Subscription, "active")->Type == DTNMOS_JSON_TRUE);
    NmosJson_Free(Json);
    CHECK(!DtNmosNode_IsRegistered(Node));
    CHECK(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK_EQ(Seen.Registrations, Registered + 1);
    CHECK_STR(Seen.LastRegistered, "receiver");

    // Disabling hands no flow over, and keeps what it received from.
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                 "{\"master_enable\": false, \"transport_file\": {\"data\": null}, "
                 "\"activation\": {\"mode\": \"activate_immediate\"}}",
                 NULL),
             200);
    CHECK_EQ(Seen.ReceiverCalls, 2);
    CHECK(!Seen.ReceiverEnabled);
    CHECK(!Seen.HasFlow);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- connection_moves_a_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void connection_moves_a_sender(void)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    REQUIRE(Node != NULL);

    // A PATCH without activation stages only.
    NmosJson* Json = NULL;
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                 "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\", "
                 "\"destination_port\": 6000}]}",
                 &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK(NmosJson_Member(NmosJson_Member(Json, "activation"), "mode")->Type ==
          DTNMOS_JSON_NULL);
    NmosJson_Free(Json);
    CHECK_EQ(Seen.SenderCalls, 0);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/staged", NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "192.168.1.9");
    NmosJson_Free(Json);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "239.0.0.1");
    NmosJson_Free(Json);

    // The activation moves the sender, and its transport file with it.
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                 "{\"receiver_id\": \"" PEER_ID "\", "
                 "\"activation\": {\"mode\": \"activate_immediate\", \"requested_time\": "
                 "null}}",
                 NULL),
             200);
    CHECK_EQ(Seen.SenderCalls, 1);
    CHECK(Seen.SenderEnabled);
    CHECK_STR(Seen.Destination, "192.168.1.9");
    CHECK_EQ(Seen.DestinationPort, 6000);
    CHECK_STR(Seen.Source, "192.168.1.5");
    CHECK_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/transportfile", NULL, NULL),
        200);
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = "GET";
    Request.Url = CONNECTION "senders/" SENDER_ID "/transportfile";
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    CHECK(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
    size_t Length = 0;
    const char* Text = DtNmosHttpResponse_Body(Response, &Length);
    DtNmosSdp* Sdp = NULL;
    REQUIRE(DtNmosSdp_Parse(Text, Length, &Sdp) == DTNMOS_OK);
    CHECK_STR(DtNmosSdp_Flow(Sdp, 0)->DestinationIp, "192.168.1.9");
    CHECK_EQ(DtNmosSdp_Flow(Sdp, 0)->DestinationPort, 6000);
    CHECK_EQ(DtNmosSdp_Session(Sdp)->SessionVersion, 2);
    DtNmosSdp_Free(Sdp);
    DtNmosHttpResponse_Free(Response);

    CHECK_EQ(Ask(Node, "GET", "/x-nmos/node/v1.3/senders/" SENDER_ID, NULL, &Json), 200);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_MemberText(Json, "transport"), "urn:x-nmos:transport:rtp.ucast");
    CHECK_STR(NmosJson_MemberText(NmosJson_Member(Json, "subscription"), "receiver_id"),
              PEER_ID);
    NmosJson_Free(Json);

    // Disabling it keeps where it sends.
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                 "{\"master_enable\": false, \"activation\": {\"mode\": "
                 "\"activate_immediate\"}}",
                 NULL),
             200);
    CHECK_EQ(Seen.SenderCalls, 2);
    CHECK(!Seen.SenderEnabled);
    CHECK_STR(Seen.Destination, "192.168.1.9");
    CHECK_EQ(Ask(Node, "GET", "/x-nmos/node/v1.3/senders/" SENDER_ID, NULL, &Json), 200);
    REQUIRE(Json != NULL);
    CHECK(NmosJson_Member(NmosJson_Member(Json, "subscription"), "active")->Type ==
          DTNMOS_JSON_FALSE);
    NmosJson_Free(Json);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- connection_refuses_bad_patches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void connection_refuses_bad_patches(void)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    REQUIRE(Node != NULL);
    const char* const Staged = CONNECTION "senders/" SENDER_ID "/staged";
    CHECK_EQ(Ask(Node, "PATCH", Staged, "{\"master_enable\": ", NULL), 400);
    CHECK_EQ(Ask(Node, "PATCH", Staged, "[]", NULL), 400);
    CHECK_EQ(Ask(Node, "PATCH", Staged, "{\"colour\": \"red\"}", NULL), 400);
    CHECK_EQ(Ask(Node, "PATCH", Staged, "{\"master_enable\": 1}", NULL), 400);
    CHECK_EQ(Ask(Node, "PATCH", Staged,
                 "{\"transport_params\": [{\"multicast_ip\": null}]}", NULL),
             400);
    CHECK_EQ(Ask(Node, "PATCH", Staged,
                 "{\"transport_params\": [{\"destination_port\": 70000}]}", NULL),
             400);
    CHECK_EQ(Ask(Node, "PATCH", Staged, "{\"transport_params\": [{}, {}]}", NULL), 400);
    CHECK_EQ(Ask(Node, "PATCH", Staged,
                 "{\"activation\": {\"mode\": \"activate_scheduled_relative\", "
                 "\"requested_time\": \"0:0\"}}",
                 NULL),
             501);
    CHECK_EQ(Ask(Node, "PATCH", CONNECTION "senders/" PEER_ID "/staged", "{}", NULL),
             404);
    // A transport file without audio does not connect the audio receiver.
    CHECK_EQ(
        Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
            "{\"transport_file\": {\"data\": \"v=0\\no=- 1 1 IN IP4 10.0.0.1\\ns=x\\n"
            "t=0 0\\nm=video 5000 RTP/AVP 96\\nc=IN IP4 239.1.1.1\\n"
            "a=rtpmap:96 raw/90000\\n\"}, "
            "\"activation\": {\"mode\": \"activate_immediate\"}}",
            NULL),
        400);
    CHECK_EQ(Seen.ReceiverCalls, 0);

    // An activation the callback fails leaves the active parameters as they were.
    Seen.Answer = DTNMOS_E_STATE;
    NmosJson* Json = NULL;
    CHECK_EQ(Ask(Node, "PATCH", Staged,
                 "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\"}], "
                 "\"activation\": {\"mode\": \"activate_immediate\"}}",
                 &Json),
             500);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_MemberText(Json, "error"), "the card refused it");
    NmosJson_Free(Json);
    CHECK_EQ(Seen.SenderCalls, 1);
    CHECK_EQ(Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json),
             200);
    REQUIRE(Json != NULL);
    CHECK_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "239.0.0.1");
    NmosJson_Free(Json);
    CHECK(DtNmosNode_IsRegistered(Node));
    DtNmosNode_Free(Node);
}
