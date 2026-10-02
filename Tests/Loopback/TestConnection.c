// #*#*#*#*#*#*#*#*#*#*#*#*#* TestConnection.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the Connection API of the node (IS-05)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "NmosOs.h"
#include "NmosTest.h"
#include "dtnmos_node.h"

#define NODE_ID "bbbbbbbb-0000-4000-8000-000000000001"
#define DEVICE_ID "bbbbbbbb-0000-4000-8000-000000000002"
#define SENDER_ID "bbbbbbbb-0000-4000-8000-000000000003"
#define RECEIVER_ID "bbbbbbbb-0000-4000-8000-000000000004"
#define PEER_ID "bbbbbbbb-0000-4000-8000-000000000005"
#define FIXED_ID "bbbbbbbb-0000-4000-8000-000000000006"

#define CONNECTION "/x-nmos/connection/v1.1/single/"

// What the callbacks of the node received, and what they answer.
typedef struct NmosActivations
{
    int SenderCalls;
    bool SenderEnabled;
    char Destination[DTNMOS_MAX_ADDRESS_SIZE];
    int DestinationPort;
    char Source[DTNMOS_MAX_ADDRESS_SIZE];
    int ReceiverCalls;
    bool ReceiverEnabled;
    bool HasFlow;
    DtNmosMedia Media;
    char Receives[DTNMOS_MAX_ADDRESS_SIZE];
    int ReceivesPort;
    char ReceivesFrom[DTNMOS_MAX_ADDRESS_SIZE];
    char SenderId[37];
    DtNmosResult Answer;
    int Registrations; // of the registry: POSTs of a resource
    char LastRegistered[16];
    // Called from within the receiver's callback when it is not null, as a second request
    // that comes in while an activation is applied.
    void (*During)(struct NmosActivations* Seen);
    DtNmosNode* Node;
    int DuringStatus;
    uint64_t CalledNs; // of the early sender: when its callback was called, in TAI
    uint64_t AtNs;     // and the time its activation takes place
} NmosActivations;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ActivateSender(void* User, const DtNmosId* Sender,
                                   const DtNmosSenderActivation* Activation)
{
    NmosActivations* Seen = User;
    NMOS_EXPECT(strcmp(Sender->Text, SENDER_ID) == 0);
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
    NMOS_EXPECT(strcmp(Receiver->Text, RECEIVER_ID) == 0);
    ++Seen->ReceiverCalls;
    Seen->ReceiverEnabled = Activation->MasterEnable;
    Seen->HasFlow = Activation->HasFlow;
    Seen->Media = Activation->Flow.Media;
    snprintf(Seen->Receives, sizeof(Seen->Receives), "%s",
             Activation->Flow.DestinationIp);
    Seen->ReceivesPort = Activation->Flow.DestinationPort;
    snprintf(Seen->ReceivesFrom, sizeof(Seen->ReceivesFrom), "%s",
             Activation->Flow.SourceIp);
    snprintf(Seen->SenderId, sizeof(Seen->SenderId), "%s", Activation->SenderId.Text);
    if (Seen->During != NULL)
    {
        Seen->During(Seen);
    }
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
    NMOS_EXPECT(DtNmosNode_AddDevice(Node, &Device) == DTNMOS_OK);
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
    Flow.Format.Video.Sampling = DTNMOS_SAMPLING_YCBCR_422;
    DtNmosSenderConfig Sender = {
        sizeof(Sender), {SENDER_ID}, {DEVICE_ID}, "camera", "", &Flow, "192.168.1.5", 0};
    NMOS_EXPECT(DtNmosNode_AddSender(Node, &Sender, ActivateSender, Seen) == DTNMOS_OK);
    DtNmosReceiverConfig Receiver = {sizeof(Receiver),
                                     {RECEIVER_ID},
                                     {DEVICE_ID},
                                     "monitor",
                                     "",
                                     DTNMOS_MEDIA_AUDIO,
                                     "192.168.1.5",
                                     0,
                                     NULL,
                                     NULL,
                                     0};
    NMOS_EXPECT(DtNmosNode_AddReceiver(Node, &Receiver, ActivateReceiver, Seen) ==
                DTNMOS_OK);
    NMOS_EXPECT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_EXPECT(DtNmosNode_IsRegistered(Node));
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
    NMOS_EXPECT(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
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
static const NmosJson* LegMember(const NmosJson* Json, const char* Name)
{
    const NmosJson* Legs = NmosJson_Member(Json, "transport_params");
    if (Legs == NULL || Legs->Type != DTNMOS_JSON_ARRAY || Legs->Count != 1)
    {
        return NULL;
    }
    return NmosJson_Member(&Legs->Items[0], Name);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionAnswersItsParameters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(ConnectionAnswersItsParameters)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(Ask(Node, "GET", "/x-nmos/connection/v1.1/", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL && Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 2);
    NmosJson_Free(Json);

    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "senders/", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL && Json->Count == 1);
    NMOS_ASSERT_STR(NmosJson_Text(&Json->Items[0]), SENDER_ID "/");
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "receivers", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL && Json->Count == 1);
    NMOS_ASSERT_STR(NmosJson_Text(&Json->Items[0]), RECEIVER_ID "/");
    NmosJson_Free(Json);

    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "senders/" SENDER_ID, NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_EQ(Json->Count, 5);
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/transporttype",
                       NULL, &Json),
                   200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(Json), "urn:x-nmos:transport:rtp");
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/constraints", NULL, &Json),
        200);
    NMOS_ASSERT(Json != NULL && Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 1);
    NMOS_ASSERT(NmosJson_Member(&Json->Items[0], "destination_ip") != NULL);
    NmosJson_Free(Json);

    // The active parameters of the sender are where its element sends.
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT(NmosJson_Member(Json, "master_enable")->Type == DTNMOS_JSON_TRUE);
    NMOS_ASSERT(NmosJson_Member(Json, "receiver_id")->Type == DTNMOS_JSON_NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "239.0.0.1");
    NMOS_ASSERT_EQ(LegMember(Json, "destination_port")->Number, 5004);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "source_ip")), "192.168.1.5");
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/staged", NULL, &Json),
        200);
    NMOS_ASSERT(Json != NULL);
    const NmosJson* File = NmosJson_Member(Json, "transport_file");
    NMOS_ASSERT(File != NULL);
    NMOS_ASSERT(NmosJson_Member(File, "data")->Type == DTNMOS_JSON_NULL);
    // A receiver starts from any source and without a group, on its port, at the port
    // of RTP.
    NMOS_ASSERT(LegMember(Json, "source_ip")->Type == DTNMOS_JSON_NULL);
    NMOS_ASSERT(LegMember(Json, "multicast_ip")->Type == DTNMOS_JSON_NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "interface_ip")), "192.168.1.5");
    NMOS_ASSERT_EQ(LegMember(Json, "destination_port")->Number, 5004);
    NmosJson_Free(Json);

    // A receiver has no transport file, and unknown resources are not found.
    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/transportfile",
                       NULL, NULL),
                   404);
    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "senders/" PEER_ID "/active", NULL, NULL),
                   404);
    NMOS_ASSERT_EQ(Ask(Node, "GET", CONNECTION "flows/", NULL, NULL), 404);
    NMOS_ASSERT_EQ(
        Ask(Node, "PUT", CONNECTION "senders/" SENDER_ID "/staged", "{}", NULL), 405);
    NMOS_ASSERT_EQ(
        Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/active", "{}", NULL), 405);
    NMOS_ASSERT_EQ(Ask(Node, "POST", "/x-nmos/connection/v1.1/bulk/flows", "[]", NULL),
                   404);
    NMOS_ASSERT_EQ(Seen.SenderCalls + Seen.ReceiverCalls, 0);
    DtNmosNode_Free(Node);
}

static const char* const ConnectReceiver =
    "{\"sender_id\": \"" PEER_ID "\", \"master_enable\": true, "
    "\"activation\": {\"mode\": \"activate_immediate\"}, "
    "\"transport_file\": {\"type\": \"application/sdp\", \"data\": "
    "\"v=0\\no=- 1 1 IN IP4 192.168.1.7\\ns=peer\\nt=0 0\\n"
    "m=video 5000 RTP/AVP 96\\nc=IN IP4 239.1.1.1/64\\na=rtpmap:96 raw/90000\\n"
    "m=audio 5006 RTP/AVP 97\\nc=IN IP4 239.1.1.2/64\\na=rtpmap:97 L24/48000/2\\n"
    "a=source-filter: incl IN IP4 239.1.1.2 192.168.1.7\\na=ptime:1\\n\"}, "
    "\"transport_params\": [{\"destination_port\": 5008}]}";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionConnectsAReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(ConnectionConnectsAReceiver)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    const int Registered = Seen.Registrations;
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                       ConnectReceiver, &Json),
                   200);
    NMOS_ASSERT(Json != NULL);
    const NmosJson* Activation = NmosJson_Member(Json, "activation");
    NMOS_ASSERT_STR(NmosJson_MemberText(Activation, "mode"), "activate_immediate");
    NMOS_ASSERT(NmosJson_MemberText(Activation, "activation_time") != NULL);
    NmosJson_Free(Json);

    // The callback gets the audio flow of the transport file, on the port of the PATCH.
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 1);
    NMOS_ASSERT(Seen.ReceiverEnabled);
    NMOS_ASSERT(Seen.HasFlow);
    NMOS_ASSERT_EQ(Seen.Media, DTNMOS_MEDIA_AUDIO);
    NMOS_ASSERT_STR(Seen.Receives, "239.1.1.2");
    NMOS_ASSERT_EQ(Seen.ReceivesPort, 5008);
    NMOS_ASSERT_STR(Seen.ReceivesFrom, "192.168.1.7");
    NMOS_ASSERT_STR(Seen.SenderId, PEER_ID);

    // The transport parameters took those of the flow of the transport file.
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/active", NULL, &Json),
        200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "sender_id"), PEER_ID);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "source_ip")), "192.168.1.7");
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "multicast_ip")), "239.1.1.2");
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "interface_ip")), "192.168.1.5");
    NMOS_ASSERT_EQ(LegMember(Json, "destination_port")->Number, 5008);
    NMOS_ASSERT(NmosJson_MemberText(NmosJson_Member(Json, "transport_file"), "data") !=
                NULL);
    NmosJson_Free(Json);

    // The receiver registers its subscription anew.
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", "/x-nmos/node/v1.3/receivers/" RECEIVER_ID, NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    const NmosJson* Subscription = NmosJson_Member(Json, "subscription");
    NMOS_ASSERT_STR(NmosJson_MemberText(Subscription, "sender_id"), PEER_ID);
    NMOS_ASSERT(NmosJson_Member(Subscription, "active")->Type == DTNMOS_JSON_TRUE);
    NmosJson_Free(Json);
    NMOS_ASSERT(!DtNmosNode_IsRegistered(Node));
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Seen.Registrations, Registered + 1);
    NMOS_ASSERT_STR(Seen.LastRegistered, "receiver");

    // Disabling hands no flow over, and keeps what it received from.
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                       "{\"master_enable\": false, \"transport_file\": {\"data\": null}, "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}",
                       NULL),
                   200);
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 2);
    NMOS_ASSERT(!Seen.ReceiverEnabled);
    NMOS_ASSERT(!Seen.HasFlow);

    // A receiver that is parked is subscribed to no sender in IS-04.
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", "/x-nmos/node/v1.3/receivers/" RECEIVER_ID, NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    Subscription = NmosJson_Member(Json, "subscription");
    NMOS_ASSERT(NmosJson_Member(Subscription, "sender_id")->Type == DTNMOS_JSON_NULL);
    NMOS_ASSERT(NmosJson_Member(Subscription, "active")->Type == DTNMOS_JSON_FALSE);
    NmosJson_Free(Json);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatchWhileApplied -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A PATCH of the receiver that comes in while its callback applies an activation; it
// is asked once.
//
static void PatchWhileApplied(NmosActivations* Seen)
{
    Seen->During = NULL;
    Seen->DuringStatus =
        Ask(Seen->Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
            "{\"master_enable\": false, "
            "\"activation\": {\"mode\": \"activate_immediate\"}}",
            NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.- ConnectionRefusesAPatchWhileApplying -.-.-.-.-.-.-.-.-.-.-.-.-
//
// While the callback of a receiver applies an activation, which cannot be taken back,
// another PATCH of it is answered with 423, and is taken once the activation is done.
NMOS_TEST(ConnectionRefusesAPatchWhileApplying)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    Seen.Node = Node;
    Seen.During = PatchWhileApplied;
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                       ConnectReceiver, NULL),
                   200);
    NMOS_ASSERT_EQ(Seen.DuringStatus, 423);
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 1);
    NMOS_ASSERT(Seen.ReceiverEnabled);

    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                       "{\"master_enable\": false, "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}",
                       NULL),
                   200);
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 2);
    NMOS_ASSERT(!Seen.ReceiverEnabled);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.- ConnectionGivesTheTransportWithoutAFile -.-.-.-.-.-.-.-.-.-.-.-.
//
// A receiver given transport parameters without a transport file gets them in the flow,
// with no format: the group, the source and the port; for unicast its own address.
NMOS_TEST(ConnectionGivesTheTransportWithoutAFile)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT_EQ(
        Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
            "{\"master_enable\": true, \"transport_params\": [{\"multicast_ip\": "
            "\"239.2.2.2\", \"source_ip\": \"192.168.1.8\", "
            "\"destination_port\": 5010}], "
            "\"activation\": {\"mode\": \"activate_immediate\"}}",
            NULL),
        200);
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 1);
    NMOS_ASSERT(!Seen.HasFlow);
    NMOS_ASSERT_EQ(Seen.Media, DTNMOS_MEDIA_AUDIO);
    NMOS_ASSERT_STR(Seen.Receives, "239.2.2.2");
    NMOS_ASSERT_STR(Seen.ReceivesFrom, "192.168.1.8");
    NMOS_ASSERT_EQ(Seen.ReceivesPort, 5010);

    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
                       "{\"transport_params\": [{\"multicast_ip\": null, \"source_ip\": "
                       "null, \"destination_port\": \"auto\"}], "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}",
                       NULL),
                   200);
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 2);
    NMOS_ASSERT_STR(Seen.Receives, "192.168.1.5");
    NMOS_ASSERT_STR(Seen.ReceivesFrom, "");
    NMOS_ASSERT_EQ(Seen.ReceivesPort, 5004);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionAnswersCorsAndTheTarget -.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every answer carries the headers of CORS, with PATCH among the methods, and a
// preflight OPTIONS is answered 200; the deprecated target of a receiver answers 501.
//
NMOS_TEST(ConnectionAnswersCorsAndTheTarget)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    const char* const Methods[] = {"GET", "OPTIONS"};
    for (int i = 0; i < 2; i++)
    {
        DtNmosHttpRequest Request;
        memset(&Request, 0, sizeof(Request));
        Request.Size = sizeof(Request);
        Request.Method = Methods[i];
        Request.Url = CONNECTION "receivers/" RECEIVER_ID "/staged";
        DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
        NMOS_ASSERT(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
        const int Status = DtNmosHttpResponse_Status(Response);
        const char* Allowed =
            DtNmosHttpResponse_FindHeader(Response, "Access-Control-Allow-Methods");
        const bool HasPatch = Allowed != NULL && strstr(Allowed, "PATCH") != NULL;
        const char* Origin =
            DtNmosHttpResponse_FindHeader(Response, "Access-Control-Allow-Origin");
        const bool AnyOrigin = Origin != NULL && strcmp(Origin, "*") == 0;
        DtNmosHttpResponse_Free(Response);
        NMOS_ASSERT_EQ(Status, 200);
        NMOS_ASSERT(HasPatch);
        NMOS_ASSERT(AnyOrigin);
    }
    NMOS_ASSERT_EQ(Ask(Node, "PUT", "/x-nmos/node/v1.3/receivers/" RECEIVER_ID "/target",
                       "{}", NULL),
                   501);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionMovesASender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(ConnectionMovesASender)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);

    // A PATCH without activation stages only.
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                       "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\", "
                       "\"destination_port\": 6000}]}",
                       &Json),
                   200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT(NmosJson_Member(NmosJson_Member(Json, "activation"), "mode")->Type ==
                DTNMOS_JSON_NULL);
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 0);
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/staged", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "192.168.1.9");
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "239.0.0.1");
    NmosJson_Free(Json);

    // The activation moves the sender, and its transport file with it.
    NMOS_ASSERT_EQ(
        Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
            "{\"receiver_id\": \"" PEER_ID "\", "
            "\"activation\": {\"mode\": \"activate_immediate\", \"requested_time\": "
            "null}}",
            NULL),
        200);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 1);
    NMOS_ASSERT(Seen.SenderEnabled);
    NMOS_ASSERT_STR(Seen.Destination, "192.168.1.9");
    NMOS_ASSERT_EQ(Seen.DestinationPort, 6000);
    NMOS_ASSERT_STR(Seen.Source, "192.168.1.5");
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/transportfile", NULL, NULL),
        200);
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = "GET";
    Request.Url = CONNECTION "senders/" SENDER_ID "/transportfile";
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    NMOS_ASSERT(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
    size_t Length = 0;
    const char* Text = DtNmosHttpResponse_Body(Response, &Length);
    DtNmosSdp* Sdp = NULL;
    NMOS_ASSERT(DtNmosSdp_Parse(Text, Length, &Sdp) == DTNMOS_OK);
    NMOS_ASSERT_STR(DtNmosSdp_Flow(Sdp, 0)->DestinationIp, "192.168.1.9");
    NMOS_ASSERT_EQ(DtNmosSdp_Flow(Sdp, 0)->DestinationPort, 6000);
    NMOS_ASSERT_EQ(DtNmosSdp_Session(Sdp)->SessionVersion, 2);
    DtNmosSdp_Free(Sdp);
    DtNmosHttpResponse_Free(Response);

    NMOS_ASSERT_EQ(Ask(Node, "GET", "/x-nmos/node/v1.3/senders/" SENDER_ID, NULL, &Json),
                   200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "transport"),
                    "urn:x-nmos:transport:rtp.ucast");
    NMOS_ASSERT_STR(
        NmosJson_MemberText(NmosJson_Member(Json, "subscription"), "receiver_id"),
        PEER_ID);
    NmosJson_Free(Json);

    // Disabling it keeps where it sends.
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                       "{\"master_enable\": false, \"activation\": {\"mode\": "
                       "\"activate_immediate\"}}",
                       NULL),
                   200);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 2);
    NMOS_ASSERT(!Seen.SenderEnabled);
    NMOS_ASSERT_STR(Seen.Destination, "192.168.1.9");
    NMOS_ASSERT_EQ(Ask(Node, "GET", "/x-nmos/node/v1.3/senders/" SENDER_ID, NULL, &Json),
                   200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT(NmosJson_Member(NmosJson_Member(Json, "subscription"), "active")->Type ==
                DTNMOS_JSON_FALSE);
    NmosJson_Free(Json);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionRefusesBadPatches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(ConnectionRefusesBadPatches)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    const char* const Staged = CONNECTION "senders/" SENDER_ID "/staged";
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"master_enable\": ", NULL), 400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "[]", NULL), 400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"colour\": \"red\"}", NULL), 400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"master_enable\": 1}", NULL), 400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"transport_params\": [{\"multicast_ip\": null}]}", NULL),
                   400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"transport_params\": [{\"destination_port\": 70000}]}", NULL),
                   400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"transport_params\": [{}, {}]}", NULL),
                   400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"activation\": {\"mode\": \"activate_scheduled_relative\", "
                       "\"requested_time\": \"soon\"}}",
                       NULL),
                   400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"activation\": {\"mode\": \"activate_later\"}}", NULL),
                   400);
    NMOS_ASSERT_EQ(
        Ask(Node, "PATCH", CONNECTION "senders/" PEER_ID "/staged", "{}", NULL), 404);
    // A transport file without audio does not connect the audio receiver.
    NMOS_ASSERT_EQ(
        Ask(Node, "PATCH", CONNECTION "receivers/" RECEIVER_ID "/staged",
            "{\"transport_file\": {\"data\": \"v=0\\no=- 1 1 IN IP4 10.0.0.1\\ns=x\\n"
            "t=0 0\\nm=video 5000 RTP/AVP 96\\nc=IN IP4 239.1.1.1\\n"
            "a=rtpmap:96 raw/90000\\n\"}, "
            "\"activation\": {\"mode\": \"activate_immediate\"}}",
            NULL),
        400);
    NMOS_ASSERT_EQ(Seen.ReceiverCalls, 0);

    // An activation the callback fails leaves the active parameters as they were.
    Seen.Answer = DTNMOS_E_STATE;
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\"}], "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}",
                       &Json),
                   500);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "error"), "the card refused it");
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 1);
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "destination_ip")), "239.0.0.1");
    NmosJson_Free(Json);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionResolvesAuto -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// "auto" is staged where IS-05 allows it, and the active parameters hold what it stands
// for: the addresses of the ports of the config, and the ports of the flows.
//
NMOS_TEST(ConnectionResolvesAuto)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    const char* const Receiver = CONNECTION "receivers/" RECEIVER_ID "/staged";
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Receiver,
                       "{\"transport_params\": [{\"source_ip\": \"auto\"}]}", NULL),
                   400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Receiver,
                       "{\"transport_params\": [{\"multicast_ip\": \"auto\"}]}", NULL),
                   400);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Receiver,
                       "{\"transport_params\": [{\"interface_ip\": \"auto\", "
                       "\"destination_port\": \"auto\"}], "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}",
                       NULL),
                   200);
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/staged", NULL, &Json),
        200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "interface_ip")), "auto");
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "receivers/" RECEIVER_ID "/active", NULL, &Json),
        200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "interface_ip")), "192.168.1.5");
    NMOS_ASSERT_EQ(LegMember(Json, "destination_port")->Number, 5004);
    NmosJson_Free(Json);

    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "senders/" SENDER_ID "/staged",
                       "{\"transport_params\": [{\"source_ip\": \"auto\", "
                       "\"source_port\": \"auto\", \"destination_port\": \"auto\"}], "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}",
                       NULL),
                   200);
    NMOS_ASSERT_STR(Seen.Source, "192.168.1.5");
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" SENDER_ID "/active", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "source_ip")), "192.168.1.5");
    NMOS_ASSERT_EQ(LegMember(Json, "source_port")->Number, 5004);
    NMOS_ASSERT_EQ(LegMember(Json, "destination_port")->Number, 5004);
    NmosJson_Free(Json);

    // The SDP of the sender gives the address it sends from as origin and source.
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = "GET";
    Request.Url = CONNECTION "senders/" SENDER_ID "/transportfile";
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    NMOS_ASSERT(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
    size_t Length = 0;
    const char* Text = DtNmosHttpResponse_Body(Response, &Length);
    DtNmosSdp* Sdp = NULL;
    const bool Parsed = DtNmosSdp_Parse(Text, Length, &Sdp) == DTNMOS_OK;
    DtNmosHttpResponse_Free(Response);
    NMOS_ASSERT(Parsed);
    char Origin[DTNMOS_MAX_ADDRESS_SIZE];
    char Source[DTNMOS_MAX_ADDRESS_SIZE];
    snprintf(Origin, sizeof(Origin), "%s", DtNmosSdp_Session(Sdp)->OriginIp);
    snprintf(Source, sizeof(Source), "%s", DtNmosSdp_Flow(Sdp, 0)->SourceIp);
    DtNmosSdp_Free(Sdp);
    NMOS_ASSERT_STR(Origin, "192.168.1.5");
    NMOS_ASSERT_STR(Source, "192.168.1.5");
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivationMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the mode of the activation of the staged or active parameters of the sender
// into Mode, "" for null.
//
static void ActivationMode(DtNmosNode* Node, const char* Leaf, char* Mode, size_t Size)
{
    char Path[160];
    snprintf(Path, sizeof(Path), CONNECTION "senders/" SENDER_ID "/%s", Leaf);
    NmosJson* Json = NULL;
    Mode[0] = '\0';
    if (Ask(Node, "GET", Path, NULL, &Json) == 200 && Json != NULL)
    {
        const char* Text =
            NmosJson_MemberText(NmosJson_Member(Json, "activation"), "mode");
        snprintf(Mode, Size, "%s", Text != NULL ? Text : "");
    }
    NmosJson_Free(Json);
}

#define EARLY_ID "bbbbbbbb-0000-4000-8000-000000000006"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateEarly -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The callback of a sender with a lead: records when it was called and for when.
//
static DtNmosResult ActivateEarly(void* User, const DtNmosId* Sender,
                                  const DtNmosSenderActivation* Activation)
{
    NmosActivations* Seen = User;
    NMOS_EXPECT(strcmp(Sender->Text, EARLY_ID) == 0);
    ++Seen->SenderCalls;
    Seen->CalledNs = NmosOs_TaiNowNs();
    Seen->AtNs = Activation->AtNs;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionCallsItsLeadEarly -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The callback of a scheduled activation of a sender with an ActivationLeadMs of 150 is
// called that much before the time, which it is given; the parameters become active at
// the time, not when the callback returns.
//
NMOS_TEST(ConnectionCallsItsLeadEarly)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    DtNmosFlow Flow = {0};
    Flow.Size = sizeof(Flow);
    Flow.Media = DTNMOS_MEDIA_AUDIO;
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "%s", "239.0.0.2");
    Flow.DestinationPort = 5004;
    Flow.PayloadType = 97;
    Flow.ClockRate = 48000;
    Flow.Format.Audio.Encoding = DTNMOS_AUDIO_ENCODING_L24;
    Flow.Format.Audio.SampleRate = 48000;
    Flow.Format.Audio.Channels = 2;
    Flow.Format.Audio.PacketTimeNs = 1000000;
    DtNmosSenderConfig Early = {sizeof(Early), {EARLY_ID},    {DEVICE_ID}, "early", "",
                                &Flow,         "192.168.1.5", 150};
    NMOS_ASSERT(DtNmosNode_AddSender(Node, &Early, ActivateEarly, &Seen) == DTNMOS_OK);

    NMOS_ASSERT_EQ(Ask(Node, "PATCH", CONNECTION "senders/" EARLY_ID "/staged",
                       "{\"master_enable\": false, \"activation\": {\"mode\": "
                       "\"activate_scheduled_relative\", \"requested_time\": "
                       "\"0:400000000\"}}",
                       NULL),
                   202);
    // Polled for two seconds at most, however soon the poll asks to be called again.
    const uint64_t Until = NmosOs_MonotonicMs() + 2000;
    while (Seen.SenderCalls == 0 && NmosOs_MonotonicMs() < Until)
    {
        uint32_t NextMs = 1000;
        NMOS_ASSERT(DtNmosNode_Poll(Node, &NextMs) == DTNMOS_OK);
        NmosOs_SleepMs(NextMs < 20 ? NextMs : 20);
    }
    NMOS_ASSERT_EQ(Seen.SenderCalls, 1);
    NMOS_ASSERT(Seen.AtNs > Seen.CalledNs);
    NMOS_ASSERT(Seen.AtNs - Seen.CalledNs >= 100000000u);

    // The poll returned once the time had come, and the activation shows that time.
    NMOS_ASSERT(NmosOs_TaiNowNs() >= Seen.AtNs);
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "senders/" EARLY_ID "/active", NULL, &Json), 200);
    const char* Time =
        NmosJson_MemberText(NmosJson_Member(Json, "activation"), "activation_time");
    unsigned long long Seconds = 0;
    unsigned long long Nanoseconds = 0;
    NMOS_ASSERT(Time != NULL && sscanf(Time, "%llu:%llu", &Seconds, &Nanoseconds) == 2);
    NMOS_ASSERT((uint64_t)Seconds * 1000000000u + Nanoseconds >= Seen.AtNs);
    NmosJson_Free(Json);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionSchedulesActivations -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A scheduled activation is answered with 202, locks the staged parameters but for a
// PATCH that cancels it, and is applied by the poll when it is due.
//
NMOS_TEST(ConnectionSchedulesActivations)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    const char* const Staged = CONNECTION "senders/" SENDER_ID "/staged";
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"transport_params\": [{\"destination_ip\": \"192.168.1.9\"}], "
                       "\"activation\": {\"mode\": \"activate_scheduled_relative\", "
                       "\"requested_time\": \"0:200000000\"}}",
                       NULL),
                   202);
    char Mode[64];
    ActivationMode(Node, "staged", Mode, sizeof(Mode));
    NMOS_ASSERT_STR(Mode, "activate_scheduled_relative");
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"master_enable\": false}", NULL), 423);
    uint32_t NextMs = 0;
    NMOS_ASSERT(DtNmosNode_Poll(Node, &NextMs) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 0);
    NMOS_ASSERT(NextMs <= 200);

    // When it is due, the poll applies it, and the active parameters show it.
    NmosOs_SleepMs(250);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 1);
    NMOS_ASSERT_STR(Seen.Destination, "192.168.1.9");
    ActivationMode(Node, "active", Mode, sizeof(Mode));
    NMOS_ASSERT_STR(Mode, "activate_scheduled_relative");
    ActivationMode(Node, "staged", Mode, sizeof(Mode));
    NMOS_ASSERT_STR(Mode, "");

    // An activation of mode null cancels a scheduled one, which then never takes place.
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged,
                       "{\"master_enable\": false, \"activation\": {\"mode\": "
                       "\"activate_scheduled_absolute\", \"requested_time\": "
                       "\"99999999999:0\"}}",
                       NULL),
                   202);
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"activation\": {\"mode\": null}}", NULL),
                   200);
    ActivationMode(Node, "staged", Mode, sizeof(Mode));
    NMOS_ASSERT_STR(Mode, "");
    NMOS_ASSERT_EQ(Ask(Node, "PATCH", Staged, "{\"master_enable\": true}", NULL), 200);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 1);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionAnswersBulk -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bulk interface applies each patch as a PATCH of its own would, and answers the
// status and error of each.
//
NMOS_TEST(ConnectionAnswersBulk)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(Ask(Node, "POST", "/x-nmos/connection/v1.1/bulk/senders",
                       "[{\"id\": \"" SENDER_ID
                       "\", \"params\": {\"master_enable\": false, "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}}, "
                       "{\"id\": \"" PEER_ID "\", \"params\": {}}, {\"params\": {}}]",
                       &Json),
                   200);
    NMOS_ASSERT(Json != NULL && Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 3);
    const int Codes[] = {200, 404, 400};
    for (size_t i = 0; i < 3; ++i)
    {
        const NmosJson* Code = NmosJson_Member(&Json->Items[i], "code");
        NMOS_EXPECT(Code != NULL && Code->Number == Codes[i]);
        NMOS_EXPECT((NmosJson_MemberText(&Json->Items[i], "error") != NULL) == (i > 0));
    }
    NMOS_ASSERT_STR(NmosJson_MemberText(&Json->Items[0], "id"), SENDER_ID);
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(Seen.SenderCalls, 1);
    NMOS_ASSERT(!Seen.SenderEnabled);
    NMOS_ASSERT_EQ(
        Ask(Node, "POST", "/x-nmos/connection/v1.1/bulk/receivers", "{}", NULL), 400);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- ConnectionStartsWithTheTransport -.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A receiver given the stream it receives at the start has that stream as its active
// transport parameters: source 192.168.1.9, group 239.2.2.2 and port 5010 on its port
// 192.168.1.5. One given a source that is not an address is refused.
//
NMOS_TEST(ConnectionStartsWithTheTransport)
{
    NmosActivations Seen;
    memset(&Seen, 0, sizeof(Seen));
    DtNmosNode* Node = MakeNode(&Seen);
    NMOS_ASSERT(Node != NULL);
    DtNmosReceiverConfig Receiver;
    memset(&Receiver, 0, sizeof(Receiver));
    Receiver.Size = sizeof(Receiver);
    Receiver.Id = (DtNmosId){FIXED_ID};
    Receiver.DeviceId = (DtNmosId){DEVICE_ID};
    Receiver.Label = "fixed";
    Receiver.Media = DTNMOS_MEDIA_VIDEO;
    Receiver.InterfaceIp = "192.168.1.5";
    Receiver.SourceIp = "192.168.1.9";
    Receiver.MulticastIp = "239.2.2.2";
    Receiver.DestinationPort = 5010;
    NMOS_ASSERT(DtNmosNode_AddReceiver(Node, &Receiver, ActivateReceiver, &Seen) ==
                DTNMOS_OK);

    NmosJson* Json = NULL;
    NMOS_ASSERT_EQ(
        Ask(Node, "GET", CONNECTION "receivers/" FIXED_ID "/active", NULL, &Json), 200);
    NMOS_ASSERT(Json != NULL);
    NMOS_ASSERT(NmosJson_Member(Json, "master_enable")->Type == DTNMOS_JSON_TRUE);
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "source_ip")), "192.168.1.9");
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "multicast_ip")), "239.2.2.2");
    NMOS_ASSERT_STR(NmosJson_Text(LegMember(Json, "interface_ip")), "192.168.1.5");
    NMOS_ASSERT_EQ(LegMember(Json, "destination_port")->Number, 5010);
    NmosJson_Free(Json);

    Receiver.Id = (DtNmosId){PEER_ID};
    Receiver.SourceIp = "a camera";
    NMOS_ASSERT(DtNmosNode_AddReceiver(Node, &Receiver, ActivateReceiver, &Seen) ==
                DTNMOS_E_INVALID_ARGUMENT);
    DtNmosNode_Free(Node);
}

NMOS_TEST_MAIN("Connection", NMOS_RUN(ConnectionAnswersItsParameters),
               NMOS_RUN(ConnectionConnectsAReceiver), NMOS_RUN(ConnectionMovesASender),
               NMOS_RUN(ConnectionRefusesBadPatches),
               NMOS_RUN(ConnectionAnswersCorsAndTheTarget),
               NMOS_RUN(ConnectionResolvesAuto), NMOS_RUN(ConnectionSchedulesActivations),
               NMOS_RUN(ConnectionAnswersBulk),
               NMOS_RUN(ConnectionRefusesAPatchWhileApplying),
               NMOS_RUN(ConnectionCallsItsLeadEarly),
               NMOS_RUN(ConnectionGivesTheTransportWithoutAFile),
               NMOS_RUN(ConnectionStartsWithTheTransport))
