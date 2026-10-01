// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestNode.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of the node against a registry that an HTTP function of the test records
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_node.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosNode.h"
#include "NmosOs.h"
#include "check.h"
#include "tests.h"

// A request the fake registry received.
typedef struct NmosRecorded
{
    char Method[8];
    char Url[256];
    char Type[16]; // of a registration: the type of its resource
} NmosRecorded;

typedef struct NmosFakeRegistration
{
    NmosRecorded Requests[64];
    int Count;
    int HeartbeatStatus;
    const char* Unreachable; // a request to a URL that holds it gets no answer
} NmosFakeRegistration;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RecordHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult RecordHttp(void* User, const DtNmosHttpRequest* Request,
                               DtNmosHttpResponse* Response)
{
    NmosFakeRegistration* Registry = User;
    NmosRecorded* r = &Registry->Requests[Registry->Count < 64 ? Registry->Count++ : 63];
    memset(r, 0, sizeof(*r));
    snprintf(r->Method, sizeof(r->Method), "%s", Request->Method);
    snprintf(r->Url, sizeof(r->Url), "%s", Request->Url);
    if (Registry->Unreachable != NULL &&
        strstr(Request->Url, Registry->Unreachable) != NULL)
    {
        return NmosError_Fail(DTNMOS_E_HTTP, "%s did not answer.", Request->Url);
    }
    int Status = 404;
    if (strstr(Request->Url, "/x-nmos/registration/v1.3/resource") != NULL &&
        strcmp(Request->Method, "POST") == 0)
    {
        NmosJson* Json = NULL;
        if (NmosJson_Parse(Request->Body, Request->BodyLength, &Json) == DTNMOS_OK)
        {
            const char* Type = NmosJson_MemberText(Json, "type");
            snprintf(r->Type, sizeof(r->Type), "%s", Type == NULL ? "?" : Type);
            CHECK(NmosJson_Member(Json, "data") != NULL);
            NmosJson_Free(Json);
        }
        Status = 201;
    }
    else if (strstr(Request->Url, "/health/nodes/") != NULL)
    {
        Status = Registry->HeartbeatStatus;
    }
    else if (strcmp(Request->Method, "DELETE") == 0)
    {
        Status = 204;
    }
    DtNmosHttpResponse_SetStatus(Response, Status);
    return DTNMOS_OK;
}

#define NODE_ID "aaaaaaaa-0000-4000-8000-000000000001"
#define DEVICE_ID "aaaaaaaa-0000-4000-8000-000000000002"
#define SENDER_ID "aaaaaaaa-0000-4000-8000-000000000003"
#define RECEIVER_ID "aaaaaaaa-0000-4000-8000-000000000004"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes a node with a device, a video sender and an audio receiver.
//
static DtNmosNode* MakeNode(NmosFakeRegistration* Registry, const char* Host,
                            uint16_t Port, DtNmosHttpFunc Http)
{
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Id = (DtNmosId){NODE_ID};
    Config.Label = "test node";
    Config.Hostname = "test-host";
    Config.ApiHost = Host;
    Config.ApiPort = Port;
    Config.RegistrationUrl = "http://registry.test/";
    Config.Http = Http;
    Config.HttpUser = Registry;
    Config.HeartbeatMs = 1;
    DtNmosNode* Node = DtNmosNode_Alloc();
    if (Node == NULL || DtNmosNode_Open(Node, &Config) != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
        DtNmosNode_Free(Node);
        return NULL;
    }
    DtNmosDeviceConfig Device = {sizeof(Device), {DEVICE_ID}, "a card", "its port 1"};
    CHECK(DtNmosNode_AddDevice(Node, &Device) == DTNMOS_OK);
    DtNmosFlow Flow = {0};
    Flow.Size = sizeof(Flow);
    Flow.Media = DTNMOS_MEDIA_VIDEO;
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "%s", "239.0.0.1");
    Flow.DestinationPort = 5004;
    Flow.PayloadType = 96;
    Flow.ClockRate = 90000;
    Flow.Format.Video.Width = 1920;
    Flow.Format.Video.Height = 1080;
    Flow.Format.Video.RateNumerator = 25;
    Flow.Format.Video.RateDenominator = 1;
    Flow.Format.Video.Interlaced = 1;
    Flow.Format.Video.Depth = 10;
    snprintf(Flow.Format.Video.Sampling, sizeof(Flow.Format.Video.Sampling), "%s",
             "YCbCr-4:2:2");
    DtNmosSenderConfig Sender = {sizeof(Sender), {SENDER_ID},  {DEVICE_ID}, "camera", "",
                                 &Flow,          "192.168.1.5"};
    CHECK(DtNmosNode_AddSender(Node, &Sender, NULL, NULL) == DTNMOS_OK);
    DtNmosReceiverConfig Receiver = {
        sizeof(Receiver), {RECEIVER_ID}, {DEVICE_ID}, "monitor", "", DTNMOS_MEDIA_AUDIO};
    CHECK(DtNmosNode_AddReceiver(Node, &Receiver, NULL, NULL) == DTNMOS_OK);
    return Node;
}

// .-.-.-.-.-.-.-.-.-.-.- node_registers_parents_before_children -.-.-.-.-.-.-.-.-.-.-.-.-
//
void node_registers_parents_before_children(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    REQUIRE(Node != NULL);
    CHECK(!DtNmosNode_IsRegistered(Node));
    uint32_t NextMs = 0;
    REQUIRE(DtNmosNode_Poll(Node, &NextMs) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(Node));
    const char* const Order[] = {"node", "device", "source",
                                 "flow", "sender", "receiver"};
    REQUIRE(Registry.Count >= 6);
    for (int i = 0; i < 6; ++i)
    {
        CHECK_STR(Registry.Requests[i].Type, Order[i]);
        CHECK_STR(Registry.Requests[i].Url,
                  "http://registry.test/x-nmos/registration/v1.3/resource");
    }
    // Adding an ID twice, or a sender to a device the node lacks, fails.
    DtNmosDeviceConfig Twice = {sizeof(Twice), {DEVICE_ID}, "again", ""};
    CHECK(DtNmosNode_AddDevice(Node, &Twice) == DTNMOS_E_INVALID_ARGUMENT);
    DtNmosReceiverConfig Orphan = {sizeof(Orphan),
                                   {"aaaaaaaa-0000-4000-8000-00000000000f"},
                                   {"aaaaaaaa-0000-4000-8000-00000000000e"},
                                   "x",
                                   "",
                                   DTNMOS_MEDIA_VIDEO};
    CHECK(DtNmosNode_AddReceiver(Node, &Orphan, NULL, NULL) == DTNMOS_E_INVALID_ARGUMENT);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.- node_registers_again_when_the_registry_lost_it -.-.-.-.-.-.-.-.-.-.-
//
void node_registers_again_when_the_registry_lost_it(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    REQUIRE(Node != NULL);
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    const int Registered = Registry.Count;
    // A heartbeat the registry answers with 404 means it lost the node. The wait is
    // longer than a tick of the clock of Windows, about 16 ms, so that the heartbeat is
    // due.
    NmosOs_SleepMs(40);
    Registry.HeartbeatStatus = 404;
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK(strstr(Registry.Requests[Registered].Url, "/health/nodes/" NODE_ID) != NULL);
    CHECK(!DtNmosNode_IsRegistered(Node));
    Registry.HeartbeatStatus = 200;
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(Node));
    CHECK_STR(Registry.Requests[Registered + 1].Type, "node");
    CHECK_STR(Registry.Requests[Registered + 6].Type, "receiver");
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.- node_is_opened_closed_and_opened_again -.-.-.-.-.-.-.-.-.-.-.-.-
//
void node_is_opened_closed_and_opened_again(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    REQUIRE(Node != NULL);
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(Node));

    // Closing deletes what the node registered, and leaves it closed.
    const int Registered = Registry.Count;
    REQUIRE(DtNmosNode_Close(Node) == DTNMOS_OK);
    CHECK(Registry.Count > Registered);
    CHECK(!DtNmosNode_IsRegistered(Node));
    CHECK_EQ(DtNmosNode_ApiPort(Node), 0);
    CHECK(DtNmosNode_Poll(Node, NULL) == DTNMOS_E_STATE);
    CHECK(strstr(DtNmos_GetLastError(), "needs an open node") != NULL);
    CHECK(DtNmosNode_Close(Node) == DTNMOS_E_STATE);

    // Opened again with a config, the same handle starts empty, and opens only once.
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Id = (DtNmosId){NODE_ID};
    Config.RegistrationUrl = "http://registry.test/";
    Config.Http = RecordHttp;
    Config.HttpUser = &Registry;
    REQUIRE(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    CHECK(DtNmosNode_Open(Node, &Config) == DTNMOS_E_STATE);
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(Node));
    DtNmosNode_Freep(&Node);
    CHECK(Node == NULL);
    DtNmosNode_Freep(&Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- node_checks_the_size_of_a_config -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void node_checks_the_size_of_a_config(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Id = (DtNmosId){NODE_ID};
    Config.RegistrationUrl = "http://registry.test/";
    Config.Http = RecordHttp;
    Config.HttpUser = &Registry;
    Config.FailuresBeforeSwitch = 7;
    DtNmosNode* Node = DtNmosNode_Alloc();
    REQUIRE(Node != NULL);

    // 0 is not the current version: it is a Size not set.
    CHECK(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "Size of the DtNmosNodeConfig is not set") !=
          NULL);
    // Smaller than the first version, and larger than this library knows.
    Config.Size = offsetof(DtNmosNodeConfig, RegistryFailed) - 1;
    CHECK(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "smaller than any version") != NULL);
    Config.Size = sizeof(Config) + 8;
    CHECK(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "older than the header") != NULL);
    // The first version opens, without the fields that came after it.
    Config.Size = offsetof(DtNmosNodeConfig, RegistryFailed);
    REQUIRE(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    CHECK_EQ(Node->FailuresBeforeSwitch, 3);
    REQUIRE(DtNmosNode_Close(Node) == DTNMOS_OK);
    Config.Size = sizeof(Config);
    REQUIRE(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    CHECK_EQ(Node->FailuresBeforeSwitch, 7);

    // A device the caller gives is checked the same way.
    DtNmosDeviceConfig Device = {0, {DEVICE_ID}, "a card", ""};
    CHECK(DtNmosNode_AddDevice(Node, &Device) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "DtNmosDeviceConfig") != NULL);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.- node_deletes_what_is_removed_and_what_it_had -.-.-.-.-.-.-.-.-.-.-
//
// The registry a callback moves the node to, and what it was asked.
typedef struct NmosNextRegistry
{
    int Calls;
    uint32_t Failures;
    const char* Url; // null keeps the node where it is
} NmosNextRegistry;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GiveNextRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GiveNextRegistry(void* User, uint32_t Failures, char* NextUrl, size_t Size)
{
    NmosNextRegistry* Next = User;
    ++Next->Calls;
    Next->Failures = Failures;
    CHECK_EQ(Size, DTNMOS_MAX_URL_SIZE);
    if (Next->Url == NULL)
    {
        return 0;
    }
    snprintf(NextUrl, Size, "%s", Next->Url);
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- node_moves_to_the_next_registry -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void node_moves_to_the_next_registry(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    Registry.Unreachable = "registry-a.test";
    NmosNextRegistry Next = {0, 0, "http://registry-b.test"};
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Id = (DtNmosId){NODE_ID};
    Config.Label = "moving node";
    Config.ApiHost = "192.168.1.5";
    Config.ApiPort = 8080;
    Config.RegistrationUrl = "http://registry-a.test";
    Config.Http = RecordHttp;
    Config.HttpUser = &Registry;
    Config.HeartbeatMs = 1;
    Config.RegistryFailed = GiveNextRegistry;
    Config.RegistryFailedUser = &Next;
    Config.FailuresBeforeSwitch = 2;
    DtNmosNode* Node = DtNmosNode_Alloc();
    REQUIRE(Node != NULL);
    REQUIRE(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);

    // The first registry does not answer; after two failed polls the node moves on, and
    // registers with the next one from the start.
    CHECK(DtNmosNode_Poll(Node, NULL) != DTNMOS_OK);
    CHECK_EQ(Next.Calls, 0);
    CHECK(DtNmosNode_Poll(Node, NULL) != DTNMOS_OK);
    CHECK_EQ(Next.Calls, 1);
    CHECK_EQ(Next.Failures, 2);
    const int Before = Registry.Count;
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK(DtNmosNode_IsRegistered(Node));
    REQUIRE(Registry.Count > Before);
    CHECK(strstr(Registry.Requests[Before].Url,
                 "http://registry-b.test/x-nmos/registration/v1.3/resource") != NULL);
    CHECK_STR(Registry.Requests[Before].Type, "node");

    // A heartbeat answered with 404 is no failure: the node registers again with the
    // same registry.
    Registry.HeartbeatStatus = 404;
    NmosOs_SleepMs(40);
    CHECK(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    Registry.HeartbeatStatus = 200;
    CHECK(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    CHECK_EQ(Next.Calls, 1);
    CHECK(strstr(Registry.Requests[Registry.Count - 1].Url, "registry-b.test") != NULL);

    // A callback without another registry keeps the node where it is.
    Registry.Unreachable = "registry-b.test";
    Next.Url = NULL;
    for (int Poll = 0; Poll < 2; ++Poll)
    {
        NmosOs_SleepMs(40);
        CHECK(DtNmosNode_Poll(Node, NULL) != DTNMOS_OK);
    }
    CHECK_EQ(Next.Calls, 2);
    CHECK(strstr(Registry.Requests[Registry.Count - 1].Url, "registry-b.test") != NULL);
    Registry.Unreachable = NULL;
    DtNmosNode_Free(Node);
}

void node_deletes_what_is_removed_and_what_it_had(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    REQUIRE(Node != NULL);
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    const int Before = Registry.Count;
    const DtNmosId Sender = {SENDER_ID};
    REQUIRE(DtNmosNode_Remove(Node, &Sender) == DTNMOS_OK);
    CHECK(DtNmosNode_Remove(Node, &Sender) == DTNMOS_E_NOT_FOUND);
    REQUIRE(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    // The sender, its flow and its source go, and the device that listed it registers
    // anew.
    CHECK_STR(Registry.Requests[Before].Method, "DELETE");
    CHECK(strstr(Registry.Requests[Before].Url, "resource/senders/" SENDER_ID) != NULL);
    CHECK(strstr(Registry.Requests[Before + 1].Url, "resource/flows/") != NULL);
    CHECK(strstr(Registry.Requests[Before + 2].Url, "resource/sources/") != NULL);
    CHECK_STR(Registry.Requests[Before + 3].Type, "device");
    const int Kept = Registry.Count;
    DtNmosNode_Free(Node);
    // The end of the node deletes the receiver, the device and the node.
    REQUIRE(Registry.Count == Kept + 3);
    CHECK(strstr(Registry.Requests[Kept].Url, "resource/receivers/" RECEIVER_ID) != NULL);
    CHECK(strstr(Registry.Requests[Kept + 1].Url, "resource/devices/" DEVICE_ID) != NULL);
    CHECK(strstr(Registry.Requests[Kept + 2].Url, "resource/nodes/" NODE_ID) != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Asks the node for path and returns the response, which the caller frees.
//
static DtNmosHttpResponse* Ask(DtNmosNode* Node, const char* Method, const char* Path)
{
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = Method;
    Request.Url = Path;
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    CHECK(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
    return Response;
}

// .-.-.-.-.-.-.-.-.-.- node_answers_its_node_api_and_transport_files -.-.-.-.-.-.-.-.-.-.
//
void node_answers_its_node_api_and_transport_files(void)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    REQUIRE(Node != NULL);
    DtNmosHttpResponse* Response = Ask(Node, "GET", "/x-nmos/node/v1.3/self");
    CHECK_EQ(DtNmosHttpResponse_Status(Response), 200);
    NmosJson* Json = NULL;
    size_t Length = 0;
    const char* Body = DtNmosHttpResponse_Body(Response, &Length);
    REQUIRE(NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK);
    CHECK_STR(NmosJson_MemberText(Json, "id"), NODE_ID);
    CHECK_STR(NmosJson_MemberText(Json, "href"), "http://192.168.1.5:8080/");
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);

    Response = Ask(Node, "GET", "/x-nmos/node/v1.3/senders/?paging.limit=10");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    REQUIRE(NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK);
    REQUIRE(Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 1);
    CHECK_STR(NmosJson_MemberText(&Json->Items[0], "manifest_href"),
              "http://192.168.1.5:8080/x-nmos/connection/v1.1/single/senders/" SENDER_ID
              "/transportfile");
    CHECK_STR(NmosJson_MemberText(&Json->Items[0], "transport"),
              "urn:x-nmos:transport:rtp.mcast");
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);

    Response = Ask(Node, "GET", "/x-nmos/node/v1.3/flows");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    REQUIRE(NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK);
    REQUIRE(Json->Count == 1);
    CHECK_STR(NmosJson_MemberText(&Json->Items[0], "interlace_mode"), "interlaced_tff");
    CHECK_STR(NmosJson_MemberText(&Json->Items[0], "media_type"), "video/raw");
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);

    // The transport file is the SDP of the flow of the sender.
    Response = Ask(Node, "GET",
                   "/x-nmos/connection/v1.1/single/senders/" SENDER_ID "/transportfile");
    CHECK_EQ(DtNmosHttpResponse_Status(Response), 200);
    CHECK_STR(DtNmosHttpResponse_ContentType(Response), "application/sdp");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    DtNmosSdp* Sdp = NULL;
    REQUIRE(DtNmosSdp_Parse(Body, Length, &Sdp) == DTNMOS_OK);
    CHECK_EQ(DtNmosSdp_Flow(Sdp, 0)->Format.Video.Height, 1080);
    CHECK(DtNmosSdp_Flow(Sdp, 0)->Format.Video.Interlaced);
    CHECK_STR(DtNmosSdp_Session(Sdp)->OriginIp, "192.168.1.5");
    DtNmosSdp_Free(Sdp);
    DtNmosHttpResponse_Free(Response);

    const char* const Missing[] = {"/x-nmos/node/v1.3/senders/nobody", "/x-nmos/nothing",
                                   "/x-nmos/node/v1.3/clocks"};
    for (size_t i = 0; i < 3; ++i)
    {
        Response = Ask(Node, "GET", Missing[i]);
        CHECK_EQ(DtNmosHttpResponse_Status(Response), 404);
        DtNmosHttpResponse_Free(Response);
    }
    Response = Ask(Node, "POST", "/x-nmos/node/v1.3/self");
    CHECK_EQ(DtNmosHttpResponse_Status(Response), 405);
    DtNmosHttpResponse_Free(Response);
    DtNmosNode_Free(Node);
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
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    // The registry is fake, the node's own server is real.
    DtNmosNode* Node = MakeNode(&Registry, "127.0.0.1", 0, RecordHttp);
    REQUIRE(Node != NULL);
    const DtNmosResult Served = DtNmosNode_Serve(Node);
    if (Served != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(Served == DTNMOS_OK);
    CHECK(DtNmosNode_ApiPort(Node) != 0);
    // Too small a buffer tells the size it needs.
    char Base[64];
    size_t Size = 4;
    CHECK(DtNmosNode_ApiUrl(Node, Base, &Size) == DTNMOS_E_BUFFER_TOO_SMALL);
    CHECK(Size > 4 && Size <= sizeof(Base));
    Size = sizeof(Base);
    REQUIRE(DtNmosNode_ApiUrl(Node, Base, &Size) == DTNMOS_OK);
    CHECK_EQ(Size, strlen(Base));
    char Url[256];
    snprintf(Url, sizeof(Url), "%s/x-nmos/node/v1.3/self", Base);
    DtNmosHttpRequest Request = {sizeof(Request), "GET", Url, NULL, NULL, 0, 3000};
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    REQUIRE(DtNmos_CurlHttp(NULL, &Request, Response) == DTNMOS_OK);
    CHECK_EQ(DtNmosHttpResponse_Status(Response), 200);
    CHECK(strstr(DtNmosHttpResponse_Body(Response, NULL), NODE_ID) != NULL);
    DtNmosHttpResponse_Free(Response);
    // The thread of the server polls the node, which registers.
    for (int Wait = 0; Wait < 100 && !DtNmosNode_IsRegistered(Node); ++Wait)
    {
        NmosOs_SleepMs(20);
    }
    CHECK(DtNmosNode_IsRegistered(Node));
    DtNmosNode_Free(Node);
}
