// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestInterop.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Interop and compliance: the AMWA NMOS Testing Tool, and a registry of NMOS
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Built only with DTNMOS_INTEROP_TESTS, and run with "ctest -L interop" on a machine the
// test bed reaches: DTNMOS_TESTING_TOOL is the base URL of the AMWA NMOS Testing Tool,
// e.g. http://192.168.39.60:5000, and DTNMOS_TEST_REGISTRY that of a registry, such as
// the one of nmos-cpp, e.g. http://192.168.39.60:8010. DTNMOS_PAUSE_REGISTRY and
// DTNMOS_RESUME_REGISTRY keep that registry off DNS-SD during IS-04-01.
//
// As a node: a node of dtnmos registers a device, a video sender and a video receiver
// with the registry and serves its APIs, and the tool runs a suite against it, IS-04-01,
// IS-05-01 or IS-05-02. A test the tool fails fails the case; every result but a pass is
// printed, warnings and "not implemented" among them. A suite takes minutes.
//
// As a client: the same node is found in the registry, its SDP fetched, a subscription
// reports its sender, and its receiver is connected to its sender and disconnected.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosOs.h"
#include "NmosTest.h"
#include "dtnmos_node.h"
#include "dtnmos_query.h"

// The namespace of the IDs of the node of these tests, a version 4 UUID of its own.
static const DtNmosId Namespace = {"3b9e6f52-41d7-4c0a-b8e3-5f2a7d1c9e64"};

// A node of the tests, the address and port it serves at, and what it was activated
// with.
typedef struct NmosInteropNode
{
    DtNmosNode* Node;
    char Host[64];
    uint16_t Port;
    DtNmosId Sender;
    DtNmosId Receiver;
    int ReceiverActivations;
    char ReceivedFrom[64];
} NmosInteropNode;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ActivateReceiver(void* User, const DtNmosId* Receiver,
                                     const DtNmosReceiverActivation* Activation)
{
    (void)Receiver;
    NmosInteropNode* Interop = User;
    Interop->ReceiverActivations++;
    snprintf(Interop->ReceivedFrom, sizeof(Interop->ReceivedFrom), "%s",
             Activation->MasterEnable ? Activation->SenderId.Text : "");
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ActivateSender(void* User, const DtNmosId* Sender,
                                   const DtNmosSenderActivation* Activation)
{
    (void)User;
    (void)Sender;
    (void)Activation;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Getenv -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The value of the variable Name, or null when it is not set; prints that it is needed.
//
static const char* Getenv(const char* Name)
{
    const char* Value = getenv(Name);
    if (Value == NULL || Value[0] == '\0')
    {
        printf("    %s is not set; the interop tests need it\n", Name);
        return NULL;
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Frees the node, which deletes what it registered; the cleanup of a failing case.
//
static void StopNode(void* Context)
{
    NmosInteropNode* Interop = Context;
    DtNmosNode_Freep(&Interop->Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Starts a node with a device, a video sender and a video receiver, registered with the
// registry of DTNMOS_TEST_REGISTRY, and waits until the registry holds it; or, when it
// Searches, one that searches for its registry with DNS-SD, which it does not wait for.
// The sender sends from the address the node serves at, so that its SDP has a source
// filter.
//
static void StartNode(NmosInteropNode* Interop, int Searches)
{
    memset(Interop, 0, sizeof(*Interop));
    const char* Registry = Searches ? NULL : Getenv("DTNMOS_TEST_REGISTRY");
    NMOS_ASSERT(Searches || Registry != NULL);
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    DtNmosId_FromName(&Namespace, "node", &Config.Id);
    Config.Label = "dtnmos interop";
    Config.Description = "The node of the interop tests of dtnmos";
    Config.Hostname = "dtnmos-interop";
    Config.RegistrationUrl = Registry;
    Config.Http = DtNmos_CurlHttp;
    // A registry that does not answer is given up within a heartbeat, as IS-04-01 tests.
    Config.TimeoutMs = 2000;
    Interop->Node = DtNmosNode_Alloc();
    NMOS_ASSERT(Interop->Node != NULL);
    NmosTest_SetCleanup(StopNode, Interop);
    NMOS_ASSERT(DtNmosNode_Open(Interop->Node, &Config) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosNode_Serve(Interop->Node) == DTNMOS_OK);

    // The base URL is http://<host>:<port>.
    char Url[128];
    size_t Size = sizeof(Url);
    NMOS_ASSERT(DtNmosNode_ApiUrl(Interop->Node, Url, &Size) == DTNMOS_OK);
    const char* Host = strstr(Url, "//") + 2;
    const char* Colon = strrchr(Url, ':');
    NMOS_ASSERT(Colon > Host && (size_t)(Colon - Host) < sizeof(Interop->Host));
    memcpy(Interop->Host, Host, (size_t)(Colon - Host));
    Interop->Port = (uint16_t)atoi(Colon + 1);

    DtNmosDeviceConfig Device = {sizeof(Device), {""}, "dtnmos interop device", ""};
    DtNmosId_FromName(&Config.Id, "device", &Device.Id);
    NMOS_ASSERT(DtNmosNode_AddDevice(Interop->Node, &Device) == DTNMOS_OK);

    DtNmosFlow Flow;
    memset(&Flow, 0, sizeof(Flow));
    Flow.Size = sizeof(Flow);
    Flow.Media = DTNMOS_MEDIA_VIDEO;
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "239.100.10.1");
    Flow.DestinationPort = 5004;
    snprintf(Flow.SourceIp, sizeof(Flow.SourceIp), "%s", Interop->Host);
    Flow.PayloadType = 96;
    Flow.ClockRate = 90000;
    Flow.RefClock.Kind = DTNMOS_REFCLOCK_LOCALMAC;
    snprintf(Flow.RefClock.LocalMac, sizeof(Flow.RefClock.LocalMac), "00-14-F4-00-00-01");
    Flow.MediaClockDirect = 1;
    DtNmosVideoFormat* Video = &Flow.Format.Video;
    Video->Width = 1920;
    Video->Height = 1080;
    Video->RateNumerator = 25;
    Video->RateDenominator = 1;
    Video->Depth = 10;
    snprintf(Video->Sampling, sizeof(Video->Sampling), "YCbCr-4:2:2");
    snprintf(Video->Colorimetry, sizeof(Video->Colorimetry), "BT709");
    snprintf(Video->Tcs, sizeof(Video->Tcs), "SDR");
    snprintf(Video->PackingMode, sizeof(Video->PackingMode), "2110GPM");
    snprintf(Video->Ssn, sizeof(Video->Ssn), "ST2110-20:2017");
    snprintf(Video->TransmitterType, sizeof(Video->TransmitterType), "2110TPN");

    DtNmosSenderConfig Sender;
    memset(&Sender, 0, sizeof(Sender));
    Sender.Size = sizeof(Sender);
    DtNmosId_FromName(&Device.Id, "sender", &Sender.Id);
    Sender.DeviceId = Device.Id;
    Sender.Label = "dtnmos interop sender";
    Sender.Description = "";
    Sender.Flow = &Flow;
    Sender.SourceIp = Interop->Host;
    NMOS_ASSERT(DtNmosNode_AddSender(Interop->Node, &Sender, ActivateSender, Interop) ==
                DTNMOS_OK);
    Interop->Sender = Sender.Id;

    DtNmosReceiverConfig Receiver;
    memset(&Receiver, 0, sizeof(Receiver));
    Receiver.Size = sizeof(Receiver);
    DtNmosId_FromName(&Device.Id, "receiver", &Receiver.Id);
    Receiver.DeviceId = Device.Id;
    Receiver.Label = "dtnmos interop receiver";
    Receiver.Description = "";
    Receiver.Media = DTNMOS_MEDIA_VIDEO;
    Receiver.InterfaceIp = Interop->Host;
    NMOS_ASSERT(DtNmosNode_AddReceiver(Interop->Node, &Receiver, ActivateReceiver,
                                       Interop) == DTNMOS_OK);
    Interop->Receiver = Receiver.Id;

    // The poll thread of the server registers the node.
    for (int Wait = 0; !Searches && Wait < 100 && !DtNmosNode_IsRegistered(Interop->Node);
         Wait++)
    {
        NmosOs_SleepMs(100);
    }
    NMOS_ASSERT(Searches || DtNmosNode_IsRegistered(Interop->Node));
    printf("    node at %s:%u%s\n", Interop->Host, (unsigned)Interop->Port,
           Searches ? ", searching for its registry" : "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunSuite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Runs Suite of the Testing Tool against the node, with one API per version of
// Versions, and counts the tests the tool fails into *Failed; a node that Searches finds
// its registry itself. Prints every result but a pass and the tally of each state.
//
static void RunSuite(const char* Suite, const char* const* Versions, size_t Count,
                     int Searches, int* Failed)
{
    *Failed = -1;
    const char* Tool = Getenv("DTNMOS_TESTING_TOOL");
    NMOS_ASSERT(Tool != NULL);
    NmosInteropNode Interop;
    StartNode(&Interop, Searches);
    NMOS_ASSERT(Interop.Node != NULL);

    NmosBuffer Body;
    memset(&Body, 0, sizeof(Body));
    NmosBuffer_Printf(&Body, "{\"suite\": \"%s\", \"output\": \"json\", \"host\": [",
                      Suite);
    for (size_t i = 0; i < Count; i++)
    {
        NmosBuffer_Printf(&Body, "%s\"%s\"", i == 0 ? "" : ", ", Interop.Host);
    }
    DTNMOS_APPEND_LITERAL(&Body, "], \"port\": [");
    for (size_t i = 0; i < Count; i++)
    {
        NmosBuffer_Printf(&Body, "%s%u", i == 0 ? "" : ", ", (unsigned)Interop.Port);
    }
    DTNMOS_APPEND_LITERAL(&Body, "], \"version\": [");
    for (size_t i = 0; i < Count; i++)
    {
        NmosBuffer_Printf(&Body, "%s\"%s\"", i == 0 ? "" : ", ", Versions[i]);
    }
    DTNMOS_APPEND_LITERAL(&Body, "]}");
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    NmosBuffer_Printf(&Url, "%s/api", Tool);
    DtNmosHttpRequest Request = {sizeof(Request),    "POST",    Url.Data,
                                 "application/json", Body.Data, Body.Length,
                                 30u * 60u * 1000u};
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    const DtNmosResult Asked = DtNmos_CurlHttp(NULL, &Request, Response);
    NmosBuffer_Free(&Url);
    NmosBuffer_Free(&Body);
    StopNode(&Interop);
    NmosTest_SetCleanup(NULL, NULL);
    if (Asked != DTNMOS_OK || DtNmosHttpResponse_Status(Response) != 200)
    {
        printf("    the tool answered %d: %s\n", DtNmosHttpResponse_Status(Response),
               Asked != DTNMOS_OK ? DtNmos_GetLastError()
                                  : DtNmosHttpResponse_Body(Response, NULL));
        DtNmosHttpResponse_Free(Response);
        NMOS_FAIL("%s did not run", Suite);
    }
    size_t Length = 0;
    const char* Text = DtNmosHttpResponse_Body(Response, &Length);
    NmosJson* Json = NULL;
    const DtNmosResult Parsed = NmosJson_Parse(Text, Length, &Json);
    DtNmosHttpResponse_Free(Response);
    NMOS_ASSERT(Parsed == DTNMOS_OK);
    const NmosJson* Results = NmosJson_Member(Json, "results");
    if (Results == NULL || Results->Type != DTNMOS_JSON_ARRAY)
    {
        NmosJson_Free(Json);
        NMOS_FAIL("the tool gave no results for %s", Suite);
    }

    // The states of the tool, and how many tests ended in each.
    static const char* const States[] = {"Pass",          "Fail",
                                         "Warning",       "Could Not Test",
                                         "Manual",        "Not Implemented",
                                         "Test Disabled", "Not Applicable"};
    int Tally[sizeof(States) / sizeof(States[0])] = {0};
    *Failed = 0;
    for (size_t i = 0; i < Results->Count; i++)
    {
        const char* State = NmosJson_MemberText(&Results->Items[i], "state");
        const char* Name = NmosJson_MemberText(&Results->Items[i], "name");
        const char* Detail = NmosJson_MemberText(&Results->Items[i], "detail");
        for (size_t s = 0; State != NULL && s < sizeof(States) / sizeof(States[0]); s++)
        {
            Tally[s] += strcmp(State, States[s]) == 0;
        }
        if (State != NULL && strcmp(State, "Pass") != 0 &&
            strcmp(State, "Not Applicable") != 0 && strcmp(State, "Test Disabled") != 0)
        {
            // A failure is printed whole, with the schema and the value a validation
            // names; any other result in one line.
            printf("    %-15s %s: %.*s\n", State, Name != NULL ? Name : "?",
                   strcmp(State, "Fail") == 0 ? INT_MAX : 200,
                   Detail != NULL ? Detail : "");
        }
        *Failed += State != NULL && strcmp(State, "Fail") == 0;
    }
    printf("    %s:", Suite);
    for (size_t s = 0; s < sizeof(States) / sizeof(States[0]); s++)
    {
        if (Tally[s] > 0)
        {
            printf(" %d %s,", Tally[s], States[s]);
        }
    }
    printf(" of %zu\n", Results->Count);
    NmosJson_Free(Json);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodePassesIs0401 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunCommand -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Runs the command of the variable Name, when it is set.
//
static void RunCommand(const char* Name)
{
    const char* Command = getenv(Name);
    if (Command != NULL && Command[0] != '\0')
    {
        const int Status = system(Command);
        printf("    %s: %s, %d\n", Name, Command, Status);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodePassesIs0401 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// IS-04-01, the Node API and registration. The tool announces mock registries of its
// own with DNS-SD, once it saw the node ask, and the node searches for them, registers
// with the most preferred and moves to the next when one fails. No other registry may
// answer meanwhile: DTNMOS_PAUSE_REGISTRY, when it is set, is a command that keeps the
// registry of the test bed from answering on DNS-SD, e.g. "nmos-testbed pause", and
// DTNMOS_RESUME_REGISTRY one that lets it answer again.
//
NMOS_TEST(NodePassesIs0401)
{
    static const char* const Versions[] = {"v1.3"};
    int Failed = 0;
    RunCommand("DTNMOS_PAUSE_REGISTRY");
    RunSuite("IS-04-01", Versions, 1, 1, &Failed);
    RunCommand("DTNMOS_RESUME_REGISTRY");
    NMOS_ASSERT_EQ(Failed, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodePassesIs0501 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// IS-05-01, the Connection API.
//
NMOS_TEST(NodePassesIs0501)
{
    static const char* const Versions[] = {"v1.1"};
    int Failed = 0;
    RunSuite("IS-05-01", Versions, 1, 0, &Failed);
    NMOS_ASSERT_EQ(Failed, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodePassesIs0502 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// IS-05-02, the Connection API with IS-04: the Node API and the Connection API, which
// the node serves on one port.
//
NMOS_TEST(NodePassesIs0502)
{
    static const char* const Versions[] = {"v1.3", "v1.1"};
    int Failed = 0;
    RunSuite("IS-05-02", Versions, 2, 0, &Failed);
    NMOS_ASSERT_EQ(Failed, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- ClientWorksWithTheRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The node of the tests, registered with DTNMOS_TEST_REGISTRY, as a query sees it: its
// sender is listed and found, its SDP read, a subscription reports it as present, and
// its receiver is connected to its sender, which its callback is told, and disconnected.
//
static int SubscriptionSaw;

static void OnChange(void* User, const DtNmosChange* Change)
{
    const NmosInteropNode* Interop = User;
    SubscriptionSaw += strcmp(Change->Id, Interop->Sender.Text) == 0;
}

NMOS_TEST(ClientWorksWithTheRegistry)
{
    NmosInteropNode Interop;
    StartNode(&Interop, 0);
    NMOS_ASSERT(Interop.Node != NULL && DtNmosNode_IsRegistered(Interop.Node));

    DtNmosQueryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.RegistryUrl = getenv("DTNMOS_TEST_REGISTRY");
    Config.Http = DtNmos_CurlHttp;
    DtNmosQuery* Query = DtNmosQuery_Alloc();
    NMOS_ASSERT(Query != NULL);
    NMOS_ASSERT(DtNmosQuery_Open(Query, &Config) == DTNMOS_OK);

    // Listed and found, with an SDP of one video flow.
    DtNmosSenderList* Senders = NULL;
    NMOS_ASSERT(DtNmosQuery_Senders(Query, &Senders) == DTNMOS_OK);
    int Listed = 0;
    for (size_t i = 0; i < DtNmosSenderList_Count(Senders); i++)
    {
        Listed +=
            strcmp(DtNmosSenderList_At(Senders, i)->Id.Text, Interop.Sender.Text) == 0;
    }
    DtNmosSenderList_Free(Senders);
    NMOS_ASSERT_EQ(Listed, 1);
    DtNmosSenderList* Found = NULL;
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, "dtnmos interop sender", &Found) ==
                DTNMOS_OK);
    DtNmosSdp* Sdp = NULL;
    const DtNmosResult Read =
        DtNmosQuery_SenderSdp(Query, DtNmosSenderList_At(Found, 0), &Sdp);
    DtNmosSenderList_Free(Found);
    NMOS_ASSERT(Read == DTNMOS_OK);
    NMOS_ASSERT_EQ(DtNmosSdp_FlowCount(Sdp), 1);
    NMOS_ASSERT_STR(DtNmosSdp_Flow(Sdp, 0)->DestinationIp, "239.100.10.1");
    DtNmosSdp_Free(Sdp);

    // A subscription reports the sender in its first message.
    DtNmosSubscriptionConfig Wanted;
    memset(&Wanted, 0, sizeof(Wanted));
    Wanted.Size = sizeof(Wanted);
    Wanted.ResourcePath = "/senders";
    Wanted.OnChange = OnChange;
    Wanted.OnChangeUser = &Interop;
    DtNmosSubscription* Subscription = DtNmosSubscription_Alloc();
    NMOS_ASSERT(Subscription != NULL);
    NMOS_ASSERT(DtNmosSubscription_Open(Subscription, Query, &Wanted) == DTNMOS_OK);
    SubscriptionSaw = 0;
    for (int Poll = 0; Poll < 50 && SubscriptionSaw == 0; Poll++)
    {
        DtNmosSubscription_Poll(Subscription, 100);
    }
    DtNmosSubscription_Freep(&Subscription);
    NMOS_ASSERT(SubscriptionSaw > 0);

    // Connected and disconnected, as a controller does it.
    DtNmosConnection* Connection = NULL;
    NMOS_ASSERT(DtNmosQuery_Connect(Query, "dtnmos interop receiver",
                                    "dtnmos interop sender", &Connection) == DTNMOS_OK);
    NMOS_ASSERT_STR(DtNmosConnection_Sender(Connection)->Id.Text, Interop.Sender.Text);
    DtNmosConnection_Free(Connection);
    NMOS_ASSERT_STR(Interop.ReceivedFrom, Interop.Sender.Text);
    DtNmosReceiverList* Disconnected = NULL;
    NMOS_ASSERT(DtNmosQuery_Disconnect(Query, "dtnmos interop receiver", &Disconnected) ==
                DTNMOS_OK);
    DtNmosReceiverList_Free(Disconnected);
    NMOS_ASSERT_STR(Interop.ReceivedFrom, "");
    NMOS_ASSERT_EQ(Interop.ReceiverActivations, 2);

    DtNmosQuery_Freep(&Query);
    StopNode(&Interop);
    NmosTest_SetCleanup(NULL, NULL);
}

NMOS_TEST_MAIN("Interop", NMOS_RUN(ClientWorksWithTheRegistry),
               NMOS_RUN(NodePassesIs0401), NMOS_RUN(NodePassesIs0501),
               NMOS_RUN(NodePassesIs0502))
