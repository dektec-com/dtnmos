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
#include "NmosTest.h"

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
    int FirstNodeStatus;     // when set, the answer to the first registration of the node
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
            NMOS_EXPECT(NmosJson_Member(Json, "data") != NULL);
            NmosJson_Free(Json);
        }
        Status = 201;
        if (strcmp(r->Type, "node") == 0 && Registry->FirstNodeStatus != 0)
        {
            Status = Registry->FirstNodeStatus;
            Registry->FirstNodeStatus = 0;
        }
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
    NMOS_EXPECT(DtNmosNode_AddDevice(Node, &Device) == DTNMOS_OK);
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
    Flow.Format.Video.Interlaced = true;
    Flow.Format.Video.Depth = 10;
    Flow.Format.Video.Sampling = DTNMOS_SAMPLING_YCBCR_422;
    // The sender and the receiver are on the address of the APIs.
    DtNmosSenderConfig Sender = {sizeof(Sender), {SENDER_ID}, {DEVICE_ID}, "camera", "",
                                 &Flow,          Host,        0,           NULL};
    NMOS_EXPECT(DtNmosNode_AddSender(Node, &Sender, NULL, NULL) == DTNMOS_OK);
    DtNmosReceiverConfig Receiver = {sizeof(Receiver),
                                     {RECEIVER_ID},
                                     {DEVICE_ID},
                                     "monitor",
                                     "",
                                     DTNMOS_MEDIA_AUDIO,
                                     Host,
                                     0,
                                     NULL,
                                     NULL,
                                     0,
                                     NULL};
    NMOS_EXPECT(DtNmosNode_AddReceiver(Node, &Receiver, NULL, NULL) == DTNMOS_OK);
    return Node;
}

// .-.-.-.-.-.-.-.-.-.-.-.- NodeRegistersParentsBeforeChildren -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeRegistersParentsBeforeChildren)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(!DtNmosNode_IsRegistered(Node));
    uint32_t NextMs = 0;
    NMOS_ASSERT(DtNmosNode_Poll(Node, &NextMs) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    const char* const Order[] = {"node", "device", "source",
                                 "flow", "sender", "receiver"};
    NMOS_ASSERT(Registry.Count >= 6);
    for (int i = 0; i < 6; ++i)
    {
        NMOS_ASSERT_STR(Registry.Requests[i].Type, Order[i]);
        NMOS_ASSERT_STR(Registry.Requests[i].Url,
                        "http://registry.test/x-nmos/registration/v1.3/resource");
    }
    // Adding an ID twice, or a receiver to a device the node lacks, fails.
    DtNmosDeviceConfig Twice = {sizeof(Twice), {DEVICE_ID}, "again", ""};
    NMOS_ASSERT(DtNmosNode_AddDevice(Node, &Twice) == DTNMOS_E_INVALID_ARGUMENT);
    DtNmosReceiverConfig Orphan = {sizeof(Orphan),
                                   {"aaaaaaaa-0000-4000-8000-00000000000f"},
                                   {"aaaaaaaa-0000-4000-8000-00000000000e"},
                                   "x",
                                   "",
                                   DTNMOS_MEDIA_VIDEO,
                                   "192.168.1.5",
                                   0,
                                   NULL,
                                   NULL,
                                   0,
                                   NULL};
    NMOS_ASSERT(DtNmosNode_AddReceiver(Node, &Orphan, NULL, NULL) ==
                DTNMOS_E_INVALID_ARGUMENT);
    // So does a receiver without the address of its port, or with a name for it.
    DtNmosReceiverConfig Portless = {sizeof(Portless),
                                     {"aaaaaaaa-0000-4000-8000-00000000000f"},
                                     {DEVICE_ID},
                                     "x",
                                     "",
                                     DTNMOS_MEDIA_VIDEO,
                                     NULL,
                                     0,
                                     NULL,
                                     NULL,
                                     0,
                                     NULL};
    NMOS_ASSERT(DtNmosNode_AddReceiver(Node, &Portless, NULL, NULL) ==
                DTNMOS_E_INVALID_ARGUMENT);
    Portless.InterfaceIp = "card.local";
    NMOS_ASSERT(DtNmosNode_AddReceiver(Node, &Portless, NULL, NULL) ==
                DTNMOS_E_INVALID_ARGUMENT);
    Portless.InterfaceIp = "192.168.1.256";
    NMOS_ASSERT(DtNmosNode_AddReceiver(Node, &Portless, NULL, NULL) ==
                DTNMOS_E_INVALID_ARGUMENT);
    DtNmosFlow Bare;
    memset(&Bare, 0, sizeof(Bare));
    Bare.Size = sizeof(Bare);
    Bare.Media = DTNMOS_MEDIA_VIDEO;
    DtNmosSenderConfig Sourceless = {sizeof(Sourceless),
                                     {"aaaaaaaa-0000-4000-8000-00000000000d"},
                                     {DEVICE_ID},
                                     "x",
                                     "",
                                     &Bare,
                                     NULL,
                                     0,
                                     NULL};
    NMOS_ASSERT(DtNmosNode_AddSender(Node, &Sourceless, NULL, NULL) ==
                DTNMOS_E_INVALID_ARGUMENT);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.- NodeRegistersAgainWhenTheRegistryLostIt -.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(NodeRegistersAgainWhenTheRegistryLostIt)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    const int Registered = Registry.Count;
    // A heartbeat the registry answers with 404 means it lost the node. The wait is
    // longer than a tick of the clock of Windows, about 16 ms, so that the heartbeat is
    // due.
    NmosOs_SleepMs(40);
    Registry.HeartbeatStatus = 404;
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT(strstr(Registry.Requests[Registered].Url, "/health/nodes/" NODE_ID) !=
                NULL);
    NMOS_ASSERT(!DtNmosNode_IsRegistered(Node));
    Registry.HeartbeatStatus = 200;
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    NMOS_ASSERT_STR(Registry.Requests[Registered + 1].Type, "node");
    NMOS_ASSERT_STR(Registry.Requests[Registered + 6].Type, "receiver");
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- NodeDeletesAnOldNodeOfItsIdFirst -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A registry that answers the first registration of the node with 200 holds an old node
// of its ID, which the node deletes before it registers again, as IS-04 asks.
//
NMOS_TEST(NodeDeletesAnOldNodeOfItsIdFirst)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    Registry.FirstNodeStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    NMOS_ASSERT(Registry.Count >= 3);
    NMOS_ASSERT_STR(Registry.Requests[0].Type, "node");
    NMOS_ASSERT_STR(Registry.Requests[1].Method, "DELETE");
    NMOS_ASSERT(strstr(Registry.Requests[1].Url, "resource/nodes/" NODE_ID) != NULL);
    NMOS_ASSERT_STR(Registry.Requests[2].Type, "node");
    NMOS_ASSERT_STR(Registry.Requests[3].Type, "device");
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- NodeIsOpenedClosedAndOpenedAgain -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeIsOpenedClosedAndOpenedAgain)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    DtNmosId Id;
    NMOS_ASSERT(DtNmosNode_Id(Node, &Id) == DTNMOS_OK);
    NMOS_ASSERT_STR(Id.Text, NODE_ID);
    NMOS_ASSERT(DtNmosNode_Id(Node, NULL) == DTNMOS_E_INVALID_ARGUMENT);

    // Closing deletes what the node registered, and leaves it closed.
    const int Registered = Registry.Count;
    NMOS_ASSERT(DtNmosNode_Close(Node) == DTNMOS_OK);
    NMOS_ASSERT(Registry.Count > Registered);
    NMOS_ASSERT(!DtNmosNode_IsRegistered(Node));
    NMOS_ASSERT_EQ(DtNmosNode_ApiPort(Node), 0);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_E_STATE);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "needs an open node") != NULL);
    NMOS_ASSERT(DtNmosNode_Close(Node) == DTNMOS_E_STATE);
    NMOS_ASSERT(DtNmosNode_Id(Node, &Id) == DTNMOS_E_STATE);

    // Opened again with a config, the same handle starts empty, and opens only once.
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Id = (DtNmosId){NODE_ID};
    Config.RegistrationUrl = "http://registry.test/";
    Config.Http = RecordHttp;
    Config.HttpUser = &Registry;
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_E_STATE);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    DtNmosNode_Freep(&Node);
    NMOS_ASSERT(Node == NULL);
    DtNmosNode_Freep(&Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NodeChecksTheSizeOfAConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeChecksTheSizeOfAConfig)
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
    NMOS_ASSERT(Node != NULL);

    // 0 is not the current version: it is a Size not set.
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(
        strstr(DtNmos_GetLastError(), "Size of the DtNmosNodeConfig is not set") != NULL);
    // Smaller than the first version, and larger than this library knows.
    Config.Size = offsetof(DtNmosNodeConfig, RegistryFailed) - 1;
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "smaller than any version") != NULL);
    Config.Size = sizeof(Config) + 8;
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "older than the header") != NULL);
    // The first version opens, without the fields that came after it.
    Config.Size = offsetof(DtNmosNodeConfig, RegistryFailed);
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Node->FailuresBeforeSwitch, 3);
    NMOS_ASSERT(DtNmosNode_Close(Node) == DTNMOS_OK);
    Config.Size = sizeof(Config);
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Node->FailuresBeforeSwitch, 7);

    // A device the caller gives is checked the same way.
    DtNmosDeviceConfig Device = {0, {DEVICE_ID}, "a card", ""};
    NMOS_ASSERT(DtNmosNode_AddDevice(Node, &Device) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "DtNmosDeviceConfig") != NULL);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.- NodeDeletesWhatIsRemovedAndWhatItHad -.-.-.-.-.-.-.-.-.-.-.-.-
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
static bool GiveNextRegistry(void* User, uint32_t Failures, char* NextUrl, size_t Size)
{
    NmosNextRegistry* Next = User;
    ++Next->Calls;
    Next->Failures = Failures;
    NMOS_EXPECT(Size == DTNMOS_MAX_URL_SIZE);
    if (Next->Url == NULL)
    {
        return false;
    }
    snprintf(NextUrl, Size, "%s", Next->Url);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NodeMovesToTheNextRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeMovesToTheNextRegistry)
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
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);

    // The first registry does not answer; after two failed polls the node moves on, and
    // registers with the next one from the start.
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) != DTNMOS_OK);
    NMOS_ASSERT_EQ(Next.Calls, 0);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) != DTNMOS_OK);
    NMOS_ASSERT_EQ(Next.Calls, 1);
    NMOS_ASSERT_EQ(Next.Failures, 2);
    const int Before = Registry.Count;
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    NMOS_ASSERT(Registry.Count > Before);
    NMOS_ASSERT(strstr(Registry.Requests[Before].Url,
                       "http://registry-b.test/x-nmos/registration/v1.3/resource") !=
                NULL);
    NMOS_ASSERT_STR(Registry.Requests[Before].Type, "node");

    // A heartbeat answered with 404 is no failure: the node registers again with the
    // same registry.
    Registry.HeartbeatStatus = 404;
    NmosOs_SleepMs(40);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    Registry.HeartbeatStatus = 200;
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Next.Calls, 1);
    NMOS_ASSERT(strstr(Registry.Requests[Registry.Count - 1].Url, "registry-b.test") !=
                NULL);

    // A callback without another registry keeps the node where it is.
    Registry.Unreachable = "registry-b.test";
    Next.Url = NULL;
    for (int Poll = 0; Poll < 2; ++Poll)
    {
        NmosOs_SleepMs(40);
        NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) != DTNMOS_OK);
    }
    NMOS_ASSERT_EQ(Next.Calls, 2);
    NMOS_ASSERT(strstr(Registry.Requests[Registry.Count - 1].Url, "registry-b.test") !=
                NULL);
    Registry.Unreachable = NULL;
    DtNmosNode_Free(Node);
}

NMOS_TEST(NodeDeletesWhatIsRemovedAndWhatItHad)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    Registry.HeartbeatStatus = 200;
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    const int Before = Registry.Count;
    const DtNmosId Sender = {SENDER_ID};
    NMOS_ASSERT(DtNmosNode_Remove(Node, &Sender) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_Remove(Node, &Sender) == DTNMOS_E_NOT_FOUND);
    NMOS_ASSERT(DtNmosNode_Poll(Node, NULL) == DTNMOS_OK);
    // The sender, its flow and its source go, and the node, which listed the interface
    // of the sender, and the device that listed the sender register anew.
    NMOS_ASSERT_STR(Registry.Requests[Before].Method, "DELETE");
    NMOS_ASSERT(strstr(Registry.Requests[Before].Url, "resource/senders/" SENDER_ID) !=
                NULL);
    NMOS_ASSERT(strstr(Registry.Requests[Before + 1].Url, "resource/flows/") != NULL);
    NMOS_ASSERT(strstr(Registry.Requests[Before + 2].Url, "resource/sources/") != NULL);
    NMOS_ASSERT_STR(Registry.Requests[Before + 3].Type, "node");
    NMOS_ASSERT_STR(Registry.Requests[Before + 4].Type, "device");
    const int Kept = Registry.Count;
    DtNmosNode_Free(Node);
    // The end of the node deletes the receiver, the device and the node.
    NMOS_ASSERT(Registry.Count == Kept + 3);
    NMOS_ASSERT(strstr(Registry.Requests[Kept].Url, "resource/receivers/" RECEIVER_ID) !=
                NULL);
    NMOS_ASSERT(strstr(Registry.Requests[Kept + 1].Url, "resource/devices/" DEVICE_ID) !=
                NULL);
    NMOS_ASSERT(strstr(Registry.Requests[Kept + 2].Url, "resource/nodes/" NODE_ID) !=
                NULL);
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
    NMOS_EXPECT(DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK);
    return Response;
}

// .-.-.-.-.-.-.-.-.-.-.- NodeAnswersItsNodeApiAndTransportFiles -.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeAnswersItsNodeApiAndTransportFiles)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosNode* Node = MakeNode(&Registry, "192.168.1.5", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    DtNmosHttpResponse* Response = Ask(Node, "GET", "/x-nmos/node/v1.3/self");
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response), 200);
    NmosJson* Json = NULL;
    size_t Length = 0;
    const char* Body = DtNmosHttpResponse_Body(Response, &Length);
    NMOS_ASSERT(NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK);
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "id"), NODE_ID);
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "href"), "http://192.168.1.5:8080/");
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);

    Response = Ask(Node, "GET", "/x-nmos/node/v1.3/senders/?paging.limit=10");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    NMOS_ASSERT(NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK);
    NMOS_ASSERT(Json->Type == DTNMOS_JSON_ARRAY && Json->Count == 1);
    NMOS_ASSERT_STR(
        NmosJson_MemberText(&Json->Items[0], "manifest_href"),
        "http://192.168.1.5:8080/x-nmos/connection/v1.1/single/senders/" SENDER_ID
        "/transportfile");
    NMOS_ASSERT_STR(NmosJson_MemberText(&Json->Items[0], "transport"),
                    "urn:x-nmos:transport:rtp.mcast");
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);

    Response = Ask(Node, "GET", "/x-nmos/node/v1.3/flows");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    NMOS_ASSERT(NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK);
    NMOS_ASSERT(Json->Count == 1);
    NMOS_ASSERT_STR(NmosJson_MemberText(&Json->Items[0], "interlace_mode"),
                    "interlaced_tff");
    NMOS_ASSERT_STR(NmosJson_MemberText(&Json->Items[0], "media_type"), "video/raw");
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);

    // The transport file is the SDP of the flow of the sender.
    Response = Ask(Node, "GET",
                   "/x-nmos/connection/v1.1/single/senders/" SENDER_ID "/transportfile");
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response), 200);
    NMOS_ASSERT_STR(DtNmosHttpResponse_ContentType(Response), "application/sdp");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    DtNmosSdp* Sdp = NULL;
    NMOS_ASSERT(DtNmosSdp_Parse(Body, Length, &Sdp) == DTNMOS_OK);
    NMOS_ASSERT_EQ(DtNmosSdp_Flow(Sdp, 0)->Format.Video.Height, 1080);
    NMOS_ASSERT(DtNmosSdp_Flow(Sdp, 0)->Format.Video.Interlaced);
    NMOS_ASSERT_STR(DtNmosSdp_Session(Sdp)->OriginIp, "192.168.1.5");
    DtNmosSdp_Free(Sdp);
    DtNmosHttpResponse_Free(Response);

    const char* const Missing[] = {"/x-nmos/node/v1.3/senders/nobody", "/x-nmos/nothing",
                                   "/x-nmos/node/v1.3/clocks"};
    for (size_t i = 0; i < 3; ++i)
    {
        Response = Ask(Node, "GET", Missing[i]);
        NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response), 404);
        DtNmosHttpResponse_Free(Response);
    }
    Response = Ask(Node, "POST", "/x-nmos/node/v1.3/self");
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response), 405);
    DtNmosHttpResponse_Free(Response);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BindingOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Asks the node for the one sender or receiver of Path and writes the name of its one
// interface binding into Name; returns how many bindings it has.
//
static size_t BindingOf(DtNmosNode* Node, const char* Path, char* Name, size_t Size)
{
    Name[0] = '\0';
    DtNmosHttpResponse* Response = Ask(Node, "GET", Path);
    size_t Length = 0;
    const char* Body = DtNmosHttpResponse_Body(Response, &Length);
    NmosJson* Json = NULL;
    size_t Count = 0;
    if (NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK && Json->Count == 1)
    {
        const NmosJson* Bindings = NmosJson_Member(&Json->Items[0], "interface_bindings");
        Count = Bindings != NULL ? Bindings->Count : 0;
        if (Count == 1 && NmosJson_Text(&Bindings->Items[0]) != NULL)
        {
            snprintf(Name, Size, "%s", NmosJson_Text(&Bindings->Items[0]));
        }
    }
    NmosJson_Free(Json);
    DtNmosHttpResponse_Free(Response);
    return Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.- NodeBindsToTheInterfaceOfItsAddress -.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The node lists the network interfaces of the host, and binds a sender and a receiver to
// the one that has their address; one on an address of no interface is bound to none.
//
NMOS_TEST(NodeBindsToTheInterfaceOfItsAddress)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosNode* Node = MakeNode(&Registry, "127.0.0.1", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    char Sender[64];
    char Receiver[64];
    NMOS_ASSERT_EQ(BindingOf(Node, "/x-nmos/node/v1.3/senders/", Sender, sizeof(Sender)),
                   1);
    NMOS_ASSERT_EQ(
        BindingOf(Node, "/x-nmos/node/v1.3/receivers/", Receiver, sizeof(Receiver)), 1);
    NMOS_ASSERT_STR(Sender, Receiver);

    DtNmosHttpResponse* Response = Ask(Node, "GET", "/x-nmos/node/v1.3/self");
    size_t Length = 0;
    const char* Body = DtNmosHttpResponse_Body(Response, &Length);
    NmosJson* Json = NULL;
    const bool Parsed = NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK;
    DtNmosHttpResponse_Free(Response);
    NMOS_ASSERT(Parsed);
    const NmosJson* Interfaces = NmosJson_Member(Json, "interfaces");
    int Listed = 0;
    for (size_t i = 0; Interfaces != NULL && i < Interfaces->Count; ++i)
    {
        const char* Name = NmosJson_MemberText(&Interfaces->Items[i], "name");
        const char* PortId = NmosJson_MemberText(&Interfaces->Items[i], "port_id");
        NMOS_EXPECT(PortId != NULL && strlen(PortId) == 17);
        Listed += Name != NULL && strcmp(Name, Sender) == 0;
    }
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(Listed, 1);
    DtNmosNode_Free(Node);

    Node = MakeNode(&Registry, "192.0.2.1", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT_EQ(BindingOf(Node, "/x-nmos/node/v1.3/senders/", Sender, sizeof(Sender)),
                   0);
    // The node lists only the interfaces its senders and receivers are bound to.
    Response = Ask(Node, "GET", "/x-nmos/node/v1.3/self");
    Body = DtNmosHttpResponse_Body(Response, &Length);
    const bool ParsedAgain = NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK;
    DtNmosHttpResponse_Free(Response);
    NMOS_ASSERT(ParsedAgain);
    const size_t None = NmosJson_Member(Json, "interfaces")->Count;
    NmosJson_Free(Json);
    NMOS_ASSERT_EQ(None, 0);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodeServesItselfOverHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeServesItselfOverHttp)
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
    NMOS_ASSERT(Node != NULL);
    const DtNmosResult Served = DtNmosNode_Serve(Node);
    if (Served != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    NMOS_ASSERT(Served == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_ApiPort(Node) != 0);
    // Too small a buffer tells the size it needs.
    char Base[64];
    size_t Size = 4;
    NMOS_ASSERT(DtNmosNode_ApiUrl(Node, Base, &Size) == DTNMOS_E_BUFFER_TOO_SMALL);
    NMOS_ASSERT(Size > 4 && Size <= sizeof(Base));
    Size = sizeof(Base);
    NMOS_ASSERT(DtNmosNode_ApiUrl(Node, Base, &Size) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Size, strlen(Base));
    char Url[256];
    snprintf(Url, sizeof(Url), "%s/x-nmos/node/v1.3/self", Base);
    DtNmosHttpRequest Request = {sizeof(Request), "GET", Url, NULL, NULL, 0, 3000};
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    NMOS_ASSERT(DtNmos_CurlHttp(NULL, &Request, Response) == DTNMOS_OK);
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response), 200);
    NMOS_ASSERT(strstr(DtNmosHttpResponse_Body(Response, NULL), NODE_ID) != NULL);
    DtNmosHttpResponse_Free(Response);
    // The thread of the server polls the node, which registers.
    for (int Wait = 0; Wait < 100 && !DtNmosNode_IsRegistered(Node); ++Wait)
    {
        NmosOs_SleepMs(20);
    }
    NMOS_ASSERT(DtNmosNode_IsRegistered(Node));
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TextIs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether Text is there and is Expected.
//
static bool TextIs(const char* Text, const char* Expected)
{
    return Text != NULL && strcmp(Text, Expected) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SelfClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The node's own resource, as its Node API serves it, with the first of its clocks in
// *Clock; the caller frees the resource with NmosJson_Free(). Null when it cannot be
// read or has no clock.
//
static NmosJson* SelfClock(DtNmosNode* Node, const NmosJson** Clock)
{
    DtNmosHttpResponse* Response = Ask(Node, "GET", "/x-nmos/node/v1.3/self");
    size_t Length = 0;
    const char* Body = DtNmosHttpResponse_Body(Response, &Length);
    NmosJson* Json = NULL;
    const bool Parsed = NmosJson_Parse(Body, Length, &Json) == DTNMOS_OK;
    DtNmosHttpResponse_Free(Response);
    const NmosJson* Clocks = Parsed ? NmosJson_Member(Json, "clocks") : NULL;
    if (Clocks == NULL || Clocks->Count != 1)
    {
        NmosJson_Free(Json);
        return NULL;
    }
    *Clock = &Clocks->Items[0];
    return Json;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsTrue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// 1 when member Key of Value is true, 0 when it is false, and -1 when it is neither.
//
static int IsTrue(const NmosJson* Value, const char* Key)
{
    const NmosJson* Member = NmosJson_Member(Value, Key);
    if (Member == NULL ||
        (Member->Type != DTNMOS_JSON_TRUE && Member->Type != DTNMOS_JSON_FALSE))
    {
        return -1;
    }
    return Member->Type == DTNMOS_JSON_TRUE ? 1 : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodeHasTheClockItIsGiven -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeHasTheClockItIsGiven)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosNode* Node = MakeNode(&Registry, "127.0.0.1", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);

    // A node starts with an internal clock.
    const NmosJson* Clock = NULL;
    NmosJson* Json = SelfClock(Node, &Clock);
    NMOS_ASSERT(Json != NULL);
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "name"), "clk0"));
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "ref_type"), "internal"));
    NMOS_EXPECT(NmosJson_Member(Clock, "gmid") == NULL);
    NmosJson_Free(Json);

    // A PTP clock, its grandmaster given in upper case and written in lower case, as
    // IS-04 asks; the node gets a new version.
    char Version[sizeof(Node->Version)];
    snprintf(Version, sizeof(Version), "%s", Node->Version);
    DtNmosClock Ptp;
    memset(&Ptp, 0, sizeof(Ptp));
    Ptp.Size = sizeof(Ptp);
    Ptp.Kind = DTNMOS_CLOCK_PTP;
    snprintf(Ptp.Grandmaster, sizeof(Ptp.Grandmaster), "%s", "00-1B-19-FF-FE-00-00-01");
    Ptp.Traceable = true;
    Ptp.Locked = false;
    NMOS_ASSERT(DtNmosNode_SetClock(Node, &Ptp) == DTNMOS_OK);
    NMOS_EXPECT(strcmp(Node->Version, Version) != 0);
    NMOS_EXPECT(!Node->NodeRegistered);
    Json = SelfClock(Node, &Clock);
    NMOS_ASSERT(Json != NULL);
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "name"), "clk0"));
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "ref_type"), "ptp"));
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "version"), "IEEE1588-2008"));
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "gmid"), "00-1b-19-ff-fe-00-00-01"));
    NMOS_EXPECT(IsTrue(Clock, "traceable") == 1);
    NMOS_EXPECT(IsTrue(Clock, "locked") == 0);
    NmosJson_Free(Json);

    // The same clock again, in another case, changes nothing; a lock does.
    snprintf(Version, sizeof(Version), "%s", Node->Version);
    snprintf(Ptp.Grandmaster, sizeof(Ptp.Grandmaster), "%s", "00-1b-19-ff-fe-00-00-01");
    NMOS_ASSERT(DtNmosNode_SetClock(Node, &Ptp) == DTNMOS_OK);
    NMOS_EXPECT(strcmp(Node->Version, Version) == 0);
    Ptp.Locked = true;
    NMOS_ASSERT(DtNmosNode_SetClock(Node, &Ptp) == DTNMOS_OK);
    NMOS_EXPECT(strcmp(Node->Version, Version) != 0);

    // Back to internal.
    DtNmosClock Internal;
    memset(&Internal, 0, sizeof(Internal));
    Internal.Size = sizeof(Internal);
    Internal.Kind = DTNMOS_CLOCK_INTERNAL;
    NMOS_ASSERT(DtNmosNode_SetClock(Node, &Internal) == DTNMOS_OK);
    Json = SelfClock(Node, &Clock);
    NMOS_ASSERT(Json != NULL);
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Clock, "ref_type"), "internal"));
    NmosJson_Free(Json);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodeRefusesABadClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeRefusesABadClock)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosNode* Node = MakeNode(&Registry, "127.0.0.1", 8080, RecordHttp);
    NMOS_ASSERT(Node != NULL);
    DtNmosClock Clock;
    memset(&Clock, 0, sizeof(Clock));
    Clock.Size = sizeof(Clock);
    NMOS_EXPECT(DtNmosNode_SetClock(Node, NULL) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_EXPECT(DtNmosNode_SetClock(Node, &Clock) == DTNMOS_E_INVALID_ARGUMENT);
    Clock.Kind = DTNMOS_CLOCK_PTP;
    static const char* const Bad[] = {
        "", "00-1b-19-ff-fe-00-00", "00:1b:19:ff:fe:00:00:01", "00-1b-19-ff-fe-00-00-0g",
        "0-01b-19-ff-fe-00-00-01"};
    for (size_t i = 0; i < sizeof(Bad) / sizeof(Bad[0]); ++i)
    {
        snprintf(Clock.Grandmaster, sizeof(Clock.Grandmaster), "%s", Bad[i]);
        NMOS_EXPECT(DtNmosNode_SetClock(Node, &Clock) == DTNMOS_E_INVALID_ARGUMENT);
    }
    NMOS_EXPECT(strstr(DtNmos_GetLastError(), "EUI-64") != NULL);
    snprintf(Clock.Grandmaster, sizeof(Clock.Grandmaster), "%s",
             "00-1b-19-ff-fe-00-00-01");
    Clock.Size = 0;
    NMOS_EXPECT(DtNmosNode_SetClock(Node, &Clock) == DTNMOS_E_INVALID_ARGUMENT);
    Clock.Size = sizeof(Clock);

    // The node keeps its internal clock.
    const NmosJson* Self = NULL;
    NmosJson* Json = SelfClock(Node, &Self);
    NMOS_ASSERT(Json != NULL);
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Self, "ref_type"), "internal"));
    NmosJson_Free(Json);
    NMOS_ASSERT(DtNmosNode_Close(Node) == DTNMOS_OK);
    NMOS_EXPECT(DtNmosNode_SetClock(Node, &Clock) == DTNMOS_E_STATE);
    DtNmosNode_Free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- NodeOpensWithTheClockOfItsConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NodeOpensWithTheClockOfItsConfig)
{
    NmosFakeRegistration Registry;
    memset(&Registry, 0, sizeof(Registry));
    DtNmosClock Clock;
    memset(&Clock, 0, sizeof(Clock));
    Clock.Size = sizeof(Clock);
    Clock.Kind = DTNMOS_CLOCK_PTP;
    snprintf(Clock.Grandmaster, sizeof(Clock.Grandmaster), "%s",
             "EC-46-70-FF-FE-0A-1B-2C");
    Clock.Locked = true;
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Id = (DtNmosId){NODE_ID};
    Config.RegistrationUrl = "http://registry.test/";
    Config.Http = RecordHttp;
    Config.HttpUser = &Registry;
    Config.Clock = &Clock;
    DtNmosNode* Node = DtNmosNode_Alloc();
    NMOS_ASSERT(Node != NULL);
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    const NmosJson* Self = NULL;
    NmosJson* Json = SelfClock(Node, &Self);
    NMOS_ASSERT(Json != NULL);
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Self, "gmid"), "ec-46-70-ff-fe-0a-1b-2c"));
    NMOS_EXPECT(IsTrue(Self, "locked") == 1);
    NMOS_EXPECT(IsTrue(Self, "traceable") == 0);
    NmosJson_Free(Json);
    NMOS_ASSERT(DtNmosNode_Close(Node) == DTNMOS_OK);

    // A config of 0.5's size ends before the clock: the node's clock is internal.
    Config.Size = offsetof(DtNmosNodeConfig, Clock);
    NMOS_ASSERT(DtNmosNode_Open(Node, &Config) == DTNMOS_OK);
    Json = SelfClock(Node, &Self);
    NMOS_ASSERT(Json != NULL);
    NMOS_EXPECT(TextIs(NmosJson_MemberText(Self, "ref_type"), "internal"));
    NmosJson_Free(Json);
    NMOS_ASSERT(DtNmosNode_Close(Node) == DTNMOS_OK);

    // A bad clock in the config keeps the node closed.
    Config.Size = sizeof(Config);
    Clock.Kind = DTNMOS_CLOCK_NONE;
    NMOS_EXPECT(DtNmosNode_Open(Node, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_EXPECT(!Node->Open);
    DtNmosNode_Free(Node);
}

NMOS_TEST_MAIN("Node", NMOS_RUN(NodeRegistersParentsBeforeChildren),
               NMOS_RUN(NodeRegistersAgainWhenTheRegistryLostIt),
               NMOS_RUN(NodeDeletesAnOldNodeOfItsIdFirst),
               NMOS_RUN(NodeIsOpenedClosedAndOpenedAgain),
               NMOS_RUN(NodeChecksTheSizeOfAConfig), NMOS_RUN(NodeMovesToTheNextRegistry),
               NMOS_RUN(NodeDeletesWhatIsRemovedAndWhatItHad),
               NMOS_RUN(NodeAnswersItsNodeApiAndTransportFiles),
               NMOS_RUN(NodeServesItselfOverHttp),
               NMOS_RUN(NodeBindsToTheInterfaceOfItsAddress),
               NMOS_RUN(NodeHasTheClockItIsGiven), NMOS_RUN(NodeRefusesABadClock),
               NMOS_RUN(NodeOpensWithTheClockOfItsConfig))
