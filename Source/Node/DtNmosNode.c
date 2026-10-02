// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosNode.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The NMOS node
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosDiscovery.h"
#include "NmosJson.h"
#include "NmosNode.h"
#include "NmosOs.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosNode_Lock(DtNmosNode* Node)
{
    NmosOs_MutexLock(Node->Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosNode_Unlock(DtNmosNode* Node)
{
    NmosOs_MutexUnlock(Node->Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static char* CopyText(const char* Text)
{
    const size_t Length = Text == NULL ? 0 : strlen(Text);
    char* Copy = malloc(Length + 1);
    if (Copy != NULL)
    {
        if (Length > 0)
        {
            memcpy(Copy, Text, Length);
        }
        Copy[Length] = '\0';
    }
    return Copy;
}

static void NodeLog(DtNmosNode* Node, DtNmosLogLevel Level, const char* Format, ...)
    DTNMOS_PRINTF(3, 4);
static void ForgetFailed(DtNmosNode* Node);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NodeLog -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void NodeLog(DtNmosNode* Node, DtNmosLogLevel Level, const char* Format, ...)
{
    if (Node->Log == NULL)
    {
        return;
    }
    char Message[640];
    va_list Arguments;
    va_start(Arguments, Format);
    vsnprintf(Message, sizeof(Message), Format, Arguments);
    va_end(Arguments);
    Node->Log(Node->LogUser, Level, Message);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HostOfUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the host of an URL, without brackets around an IPv6 address, into host.
//
static bool HostOfUrl(const char* Url, char* Host, size_t Size)
{
    const char* Start = strstr(Url, "://");
    Start = Start == NULL ? Url : Start + 3;
    const char* At = strchr(Start, '@');
    const char* Slash = strchr(Start, '/');
    if (At != NULL && (Slash == NULL || At < Slash))
    {
        Start = At + 1;
    }
    const char* End = NULL;
    if (*Start == '[')
    {
        ++Start;
        End = strchr(Start, ']');
    }
    else
    {
        End = Start + strcspn(Start, ":/?#");
    }
    if (End == NULL || End == Start || (size_t)(End - Start) >= Size)
    {
        return false;
    }
    memcpy(Host, Start, (size_t)(End - Start));
    Host[End - Start] = '\0';
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrationBase -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the base of the Registration API of the registry at url, which the caller
// frees, or null when out of memory.
//
static char* RegistrationBase(const char* Url)
{
    size_t Length = strlen(Url);
    while (Length > 0 && Url[Length - 1] == '/')
    {
        --Length;
    }
    NmosBuffer Base;
    memset(&Base, 0, sizeof(Base));
    NmosBuffer_Append(&Base, Url, Length);
    DTNMOS_APPEND_LITERAL(&Base, "/x-nmos/registration/v1.3/");
    if (Base.Failed)
    {
        NmosBuffer_Free(&Base);
        return NULL;
    }
    return Base.Data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosNode* DtNmosNode_Alloc(void)
{
    return calloc(1, sizeof(DtNmosNode));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosNode_CheckOpen(const DtNmosNode* Node, const char* Function)
{
    if (Node == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "%s() needs a node.", Function);
    }
    if (!Node->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "%s() needs an open node.", Function);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Open(DtNmosNode* Node, const DtNmosNodeConfig* Config)
{
    if (Node == NULL || Config == NULL || Config->Id.Text[0] == '\0' ||
        Config->Http == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A node needs an ID and an HTTP function.");
    }
    if (Node->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "The node is open already; close it first.");
    }
    // The first version of the config ends before the fields of moving to another
    // registry.
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(
        Config, DtNmosNodeConfig, offsetof(DtNmosNodeConfig, RegistryFailed));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    if (Config->ApiVersion != NULL && strcmp(Config->ApiVersion, "v1.3") != 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "IS-04 %s is not supported; dtnmos speaks v1.3.",
                              Config->ApiVersion);
    }
    // A node without a registry takes those of the search of the application, which a
    // config of an older header does not have.
    const bool Searches =
        Config->RegistrationUrl == NULL || Config->RegistrationUrl[0] == '\0';
    DtNmosRegistrySearch* Search =
        Config->Size >= offsetof(DtNmosNodeConfig, Search) + sizeof(Config->Search)
            ? Config->Search
            : NULL;
    if (Searches && Search == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A node needs the URL of its registry, or a search that "
                              "finds the Registration API.");
    }
    DtNmosNode* Result = Node;
    Result->Searches = Searches;
    Result->Search = Searches ? Search : NULL;
    Result->Mutex = NmosOs_MutexCreate();
    Result->Id = Config->Id;
    Result->Label = CopyText(Config->Label);
    Result->Description = CopyText(Config->Description);
    Result->Hostname = CopyText(Config->Hostname);
    Result->ApiPort = Config->ApiPort;
    char Host[256] = "";
    if (Config->ApiHost != NULL && Config->ApiHost[0] != '\0')
    {
        snprintf(Host, sizeof(Host), "%s", Config->ApiHost);
    }
    else
    {
        // The address on the way to the registry, or, for a node that takes its registry
        // from a search, on the way to the group of multicast DNS: that of the default
        // route.
        char Registry[256] = "224.0.0.251";
        if ((!Searches &&
             !HostOfUrl(Config->RegistrationUrl, Registry, sizeof(Registry))) ||
            !NmosOs_AddressToward(Registry, Host, sizeof(Host)))
        {
            snprintf(Host, sizeof(Host), "127.0.0.1");
        }
    }
    Result->ApiHost = CopyText(Host);
    Result->Registration = Searches ? NULL : RegistrationBase(Config->RegistrationUrl);
    Result->Http = Config->Http;
    Result->HttpUser = Config->HttpUser;
    Result->TimeoutMs = Config->TimeoutMs == 0 ? 5000 : Config->TimeoutMs;
    Result->HeartbeatMs = Config->HeartbeatMs == 0 ? 5000 : Config->HeartbeatMs;
    Result->Log = Config->Log;
    Result->LogUser = Config->LogUser;
    // A config of an older header ends before the fields of moving to another registry.
    if (Config->Size >= offsetof(DtNmosNodeConfig, FailuresBeforeSwitch) +
                            sizeof(Config->FailuresBeforeSwitch))
    {
        Result->RegistryFailed = Config->RegistryFailed;
        Result->RegistryFailedUser = Config->RegistryFailedUser;
        Result->FailuresBeforeSwitch = Config->FailuresBeforeSwitch;
    }
    // A node that found its registry itself moves on at its first failure, as IS-04 asks;
    // one that was given its registry gives it a few polls.
    if (Result->FailuresBeforeSwitch == 0)
    {
        Result->FailuresBeforeSwitch = Searches ? 1 : 3;
    }
    Result->FirstRegistration = true;
    if (Result->Mutex == NULL || Result->Label == NULL || Result->Description == NULL ||
        Result->Hostname == NULL || Result->ApiHost == NULL ||
        (!Searches && Result->Registration == NULL))
    {
        NmosNode_Release(Result);
        return NmosError_FailMemory();
    }
    NmosOs_VersionNow(&Result->LastVersion, Result->Version, sizeof(Result->Version));
    Result->Open = true;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FreeSender(NmosNodeSender* Sender)
{
    free(Sender->Label);
    free(Sender->Description);
    free(Sender->SourceIp);
    NmosStore_Free(&Sender->FlowStore);
    NmosConnection_ClearSender(Sender);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FreeReceiver(NmosNodeReceiver* Receiver)
{
    free(Receiver->Label);
    free(Receiver->Description);
    free(Receiver->InterfaceIp);
    NmosConnection_ClearReceiver(Receiver);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_Release(DtNmosNode* Node)
{
    for (size_t i = 0; i < Node->DeviceCount; ++i)
    {
        free(Node->Devices[i].Label);
        free(Node->Devices[i].Description);
    }
    for (size_t i = 0; i < Node->SenderCount; ++i)
    {
        FreeSender(&Node->Senders[i]);
    }
    for (size_t i = 0; i < Node->ReceiverCount; ++i)
    {
        FreeReceiver(&Node->Receivers[i]);
    }
    free(Node->Devices);
    free(Node->Senders);
    free(Node->Receivers);
    free(Node->Removals);
    free(Node->Label);
    free(Node->Description);
    free(Node->Hostname);
    free(Node->ApiHost);
    free(Node->Registration);
    ForgetFailed(Node);
    NmosOs_MutexFree(Node->Mutex);
    memset(Node, 0, sizeof(*Node));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistryRequest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Performs a request to the Registration API; path follows its base. Returns the status
// in status, or fails when no answer came.
//
static DtNmosResult RegistryRequest(DtNmosNode* Node, const char* Method,
                                    const char* Path, const char* Body, int* Status)
{
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    NmosBuffer_Printf(&Url, "%s%s", Node->Registration, Path);
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    if (Url.Failed || Response == NULL)
    {
        NmosBuffer_Free(&Url);
        DtNmosHttpResponse_Free(Response);
        return NmosError_FailMemory();
    }
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = Method;
    Request.Url = Url.Data;
    Request.TimeoutMs = Node->TimeoutMs;
    if (Body != NULL)
    {
        Request.ContentType = "application/json";
        Request.Body = Body;
        Request.BodyLength = strlen(Body);
    }
    NmosError_Clear();
    DtNmosResult Result = Node->Http(Node->HttpUser, &Request, Response);
    if (Result == DTNMOS_OK)
    {
        *Status = DtNmosHttpResponse_Status(Response);
    }
    else if (DtNmos_GetLastError()[0] == '\0')
    {
        NmosError_Fail(Result, "%s %s failed.", Method, Url.Data);
    }
    NmosBuffer_Free(&Url);
    DtNmosHttpResponse_Free(Response);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegisterResource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Registers a resource of type with the JSON data; 200 and 201 are both success.
//
static DtNmosResult RegisterResource(DtNmosNode* Node, const char* Type, const char* Data)
{
    NmosBuffer Body;
    memset(&Body, 0, sizeof(Body));
    NmosBuffer_Printf(&Body, "{\"type\": \"%s\", \"data\": %s}", Type, Data);
    if (Body.Failed)
    {
        NmosBuffer_Free(&Body);
        return NmosError_FailMemory();
    }
    int Status = 0;
    DtNmosResult Result = RegistryRequest(Node, "POST", "resource", Body.Data, &Status);
    NmosBuffer_Free(&Body);
    // A registry that answers the first registration of the node with 200 holds a node of
    // its ID from before, which IS-04 has the node delete and register anew.
    const bool Stale = Result == DTNMOS_OK && Status == 200 &&
                       strcmp(Type, "node") == 0 && Node->FirstRegistration;
    if (Result == DTNMOS_OK && strcmp(Type, "node") == 0)
    {
        Node->FirstRegistration = false;
    }
    if (Stale)
    {
        char Path[96];
        snprintf(Path, sizeof(Path), "resource/nodes/%s", Node->Id.Text);
        int Deleted = 0;
        NodeLog(
            Node, DTNMOS_LOG_INFO,
            "The registry held node %s from before; it deletes it and registers anew.",
            Node->Id.Text);
        Result = RegistryRequest(Node, "DELETE", Path, NULL, &Deleted);
        return Result == DTNMOS_OK ? RegisterResource(Node, Type, Data) : Result;
    }
    if (Result == DTNMOS_OK && Status != 200 && Status != 201)
    {
        Result = NmosError_Fail(
            DTNMOS_E_HTTP, "The registry answered %d to registering a %s.", Status, Type);
    }
    if (Result == DTNMOS_OK)
    {
        NodeLog(Node, DTNMOS_LOG_DEBUG, "Registered a %s.", Type);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteCommon -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the members that every resource of IS-04 has.
//
static void WriteCommon(NmosBuffer* b, const DtNmosId* Id, const char* Version,
                        const char* Label, const char* Description)
{
    NmosBuffer_Printf(b, "\"id\": \"%s\", \"version\": \"%s\", \"label\": ", Id->Text,
                      Version);
    NmosJson_WriteString(b, Label);
    DTNMOS_APPEND_LITERAL(b, ", \"description\": ");
    NmosJson_WriteString(b, Description);
    DTNMOS_APPEND_LITERAL(b, ", \"tags\": {}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteBaseUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosNode_WriteBaseUrl(const DtNmosNode* Node, NmosBuffer* b)
{
    const bool Ipv6 = strchr(Node->ApiHost, ':') != NULL;
    NmosBuffer_Printf(b, "http://%s%s%s:%u", Ipv6 ? "[" : "", Node->ApiHost,
                      Ipv6 ? "]" : "", (unsigned)Node->ApiPort);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsBound -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a sender of the node sends from Address or a receiver receives on it.
//
static bool IsBound(const DtNmosNode* Node, const char* Address)
{
    for (size_t i = 0; i < Node->SenderCount; ++i)
    {
        if (strcmp(Node->Senders[i].ActiveSourceIp, Address) == 0)
        {
            return true;
        }
    }
    for (size_t i = 0; i < Node->ReceiverCount; ++i)
    {
        if (strcmp(Node->Receivers[i].InterfaceIp, Address) == 0)
        {
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteInterfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the network interfaces of the host that the senders and receivers of the node
// are bound to, each once, as IS-04 lists them in a node: those that the node makes
// available to its devices, and not every interface of the host. The caller holds the
// lock.
//
static void WriteInterfaces(const DtNmosNode* Node, NmosBuffer* b)
{
    size_t Count = 0;
    NmosInterface* List = NmosOs_Interfaces(&Count);
    DTNMOS_APPEND_LITERAL(b, "[");
    bool First = true;
    for (size_t i = 0; i < Count; ++i)
    {
        if (!IsBound(Node, List[i].Address))
        {
            continue;
        }
        bool Listed = false;
        for (size_t j = 0; j < i && !Listed; ++j)
        {
            Listed =
                strcmp(List[j].Name, List[i].Name) == 0 && IsBound(Node, List[j].Address);
        }
        if (Listed)
        {
            continue;
        }
        NmosBuffer_Printf(b, "%s{\"chassis_id\": null, \"port_id\": \"%s\", \"name\": ",
                          First ? "" : ", ", List[i].PortId);
        NmosJson_WriteString(b, List[i].Name);
        DTNMOS_APPEND_LITERAL(b, "}");
        First = false;
    }
    DTNMOS_APPEND_LITERAL(b, "]");
    free(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteBindings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the interface bindings of a sender or receiver of the one leg on Address: the
// name of the interface of the host that has it, or none when no interface has it.
//
static void WriteBindings(NmosBuffer* b, const char* Address)
{
    size_t Count = 0;
    NmosInterface* List = NmosOs_Interfaces(&Count);
    DTNMOS_APPEND_LITERAL(b, "[");
    for (size_t i = 0; i < Count; ++i)
    {
        if (strcmp(List[i].Address, Address) == 0)
        {
            NmosJson_WriteString(b, List[i].Name);
            break;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "]");
    free(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteSelf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteSelf(const DtNmosNode* Node, NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    WriteCommon(b, &Node->Id, Node->Version, Node->Label, Node->Description);
    DTNMOS_APPEND_LITERAL(b, ", \"href\": \"");
    NmosNode_WriteBaseUrl(Node, b);
    DTNMOS_APPEND_LITERAL(b, "/\", \"hostname\": ");
    NmosJson_WriteString(b, Node->Hostname);
    DTNMOS_APPEND_LITERAL(
        b, ", \"api\": {\"versions\": [\"v1.3\"], \"endpoints\": [{\"host\": ");
    NmosJson_WriteString(b, Node->ApiHost);
    NmosBuffer_Printf(
        b,
        ", \"port\": %u, \"protocol\": \"http\"}]}, \"caps\": {}, \"services\": "
        "[], \"clocks\": [{\"name\": \"clk0\", \"ref_type\": \"internal\"}], "
        "\"interfaces\": ",
        (unsigned)Node->ApiPort);
    WriteInterfaces(Node, b);
    DTNMOS_APPEND_LITERAL(b, "}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteDevice(const DtNmosNode* Node, const NmosNodeDevice* Device,
                          NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    WriteCommon(b, &Device->Id, Device->Version, Device->Label, Device->Description);
    NmosBuffer_Printf(b,
                      ", \"type\": \"urn:x-nmos:device:generic\", \"node_id\": \"%s\", "
                      "\"senders\": [",
                      Node->Id.Text);
    bool First = true;
    for (size_t i = 0; i < Node->SenderCount; ++i)
    {
        if (strcmp(Node->Senders[i].DeviceId.Text, Device->Id.Text) == 0)
        {
            NmosBuffer_Printf(b, "%s\"%s\"", First ? "" : ", ", Node->Senders[i].Id.Text);
            First = false;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "], \"receivers\": [");
    First = true;
    for (size_t i = 0; i < Node->ReceiverCount; ++i)
    {
        if (strcmp(Node->Receivers[i].DeviceId.Text, Device->Id.Text) == 0)
        {
            NmosBuffer_Printf(b, "%s\"%s\"", First ? "" : ", ",
                              Node->Receivers[i].Id.Text);
            First = false;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "], \"controls\": [{\"href\": \"");
    NmosNode_WriteBaseUrl(Node, b);
    DTNMOS_APPEND_LITERAL(b, "/x-nmos/connection/v1.1/\", \"type\": "
                             "\"urn:x-nmos:control:sr-ctrl/v1.1\"}]}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsVideo(const NmosNodeSender* Sender)
{
    return Sender->Flow.Media == DTNMOS_MEDIA_VIDEO;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteSource(const NmosNodeSender* Sender, NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    WriteCommon(b, &Sender->SourceId, Sender->Version, Sender->Label,
                Sender->Description);
    NmosBuffer_Printf(b,
                      ", \"format\": \"urn:x-nmos:format:%s\", \"caps\": {}, "
                      "\"device_id\": \"%s\", \"parents\": [], \"clock_name\": \"clk0\"",
                      IsVideo(Sender) ? "video" : "audio", Sender->DeviceId.Text);
    if (IsVideo(Sender))
    {
        const DtNmosVideoFormat* Video = &Sender->Flow.Format.Video;
        NmosBuffer_Printf(
            b, ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}}",
            (unsigned)Video->RateNumerator,
            (unsigned)(Video->RateDenominator == 0 ? 1 : Video->RateDenominator));
        return;
    }
    DTNMOS_APPEND_LITERAL(b, ", \"channels\": [");
    for (uint32_t c = 0; c < Sender->Flow.Format.Audio.Channels; ++c)
    {
        NmosBuffer_Printf(b, "%s{\"label\": \"Channel %u\"}", c == 0 ? "" : ", ",
                          (unsigned)(c + 1));
    }
    DTNMOS_APPEND_LITERAL(b, "]}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OrDefault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns text, or fallback when it is empty.
//
static const char* OrDefault(const char* Text, const char* Fallback)
{
    return Text[0] != '\0' ? Text : Fallback;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteFlow(const NmosNodeSender* Sender, NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    WriteCommon(b, &Sender->FlowId, Sender->Version, Sender->Label, Sender->Description);
    NmosBuffer_Printf(b,
                      ", \"format\": \"urn:x-nmos:format:%s\", \"source_id\": \"%s\", "
                      "\"device_id\": \"%s\", \"parents\": []",
                      IsVideo(Sender) ? "video" : "audio", Sender->SourceId.Text,
                      Sender->DeviceId.Text);
    if (IsVideo(Sender))
    {
        const DtNmosVideoFormat* Video = &Sender->Flow.Format.Video;
        const unsigned Width = (unsigned)Video->Width;
        const unsigned Height = (unsigned)Video->Height;
        const unsigned Depth = (unsigned)(Video->Depth == 0 ? 10 : Video->Depth);
        NmosBuffer_Printf(
            b,
            ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}, "
            "\"frame_width\": "
            "%u, "
            "\"frame_height\": %u, \"colorspace\": \"%s\", \"interlace_mode\": \"%s\", "
            "\"transfer_characteristic\": \"%s\", \"media_type\": \"video/raw\", "
            "\"components\": "
            "[{\"name\": \"Y\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}, "
            "{\"name\": \"Cb\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}, "
            "{\"name\": \"Cr\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}]}",
            (unsigned)Video->RateNumerator,
            (unsigned)(Video->RateDenominator == 0 ? 1 : Video->RateDenominator), Width,
            Height, OrDefault(Video->Colorimetry, "BT709"),
            Video->Interlaced ? "interlaced_tff" : "progressive",
            OrDefault(Video->Tcs, "SDR"), Width, Height, Depth, Width / 2, Height, Depth,
            Width / 2, Height, Depth);
        return;
    }
    const DtNmosAudioFormat* Audio = &Sender->Flow.Format.Audio;
    const int L16 = strcmp(Audio->Encoding, "L16") == 0;
    NmosBuffer_Printf(
        b,
        ", \"sample_rate\": {\"numerator\": %u}, \"media_type\": \"audio/%s\", "
        "\"bit_depth\": %d}",
        (unsigned)Audio->SampleRate, L16 ? "L16" : "L24", L16 ? 16 : 24);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteSender(const DtNmosNode* Node, const NmosNodeSender* Sender,
                          NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    WriteCommon(b, &Sender->Id, Sender->Version, Sender->Label, Sender->Description);
    NmosBuffer_Printf(
        b,
        ", \"flow_id\": \"%s\", \"transport\": \"urn:x-nmos:transport:%s\", "
        "\"device_id\": \"%s\", \"manifest_href\": \"",
        Sender->FlowId.Text,
        NmosNode_IsMulticast(Sender->Flow.DestinationIp) ? "rtp.mcast" : "rtp.ucast",
        Sender->DeviceId.Text);
    NmosNode_WriteBaseUrl(Node, b);
    NmosBuffer_Printf(b,
                      "/x-nmos/connection/v1.1/single/senders/%s/transportfile\", "
                      "\"interface_bindings\": ",
                      Sender->Id.Text);
    WriteBindings(b, Sender->ActiveSourceIp);
    DTNMOS_APPEND_LITERAL(b, ", \"subscription\": {\"receiver_id\": ");
    if (Sender->ReceiverId.Text[0] != '\0')
    {
        NmosBuffer_Printf(b, "\"%s\"", Sender->ReceiverId.Text);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    NmosBuffer_Printf(b, ", \"active\": %s}}", Sender->MasterEnable ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteReceiver(const NmosNodeReceiver* Receiver, NmosBuffer* b)
{
    const bool Video = Receiver->Media == DTNMOS_MEDIA_VIDEO;
    DTNMOS_APPEND_LITERAL(b, "{");
    WriteCommon(b, &Receiver->Id, Receiver->Version, Receiver->Label,
                Receiver->Description);
    NmosBuffer_Printf(
        b,
        ", \"format\": \"urn:x-nmos:format:%s\", \"caps\": {\"media_types\": "
        "[%s]}, \"device_id\": \"%s\", \"transport\": "
        "\"urn:x-nmos:transport:rtp\", \"interface_bindings\": ",
        Video ? "video" : "audio",
        Video ? "\"video/raw\"" : "\"audio/L24\", \"audio/L16\"",
        Receiver->DeviceId.Text);
    WriteBindings(b, Receiver->InterfaceIp);
    DTNMOS_APPEND_LITERAL(b, ", \"subscription\": {\"sender_id\": ");
    if (Receiver->SenderId.Text[0] != '\0')
    {
        NmosBuffer_Printf(b, "\"%s\"", Receiver->SenderId.Text);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    NmosBuffer_Printf(b, ", \"active\": %s}}", Receiver->MasterEnable ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_IsAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosNode_IsAddress(const char* Address)
{
    if (Address == NULL || Address[0] == '\0' ||
        strlen(Address) >= DTNMOS_MAX_ADDRESS_SIZE)
    {
        return false;
    }
    if (strchr(Address, ':') != NULL)
    {
        // An IPv6 address: hexadecimal groups between colons, the last two perhaps an
        // IPv4 address.
        return strspn(Address, "0123456789abcdefABCDEF:.") == strlen(Address);
    }
    // An IPv4 address: four numbers of 0 to 255 between dots.
    const char* p = Address;
    for (int Part = 0; Part < 4; ++Part)
    {
        int Value = 0;
        int Digits = 0;
        for (; *p >= '0' && *p <= '9' && Digits < 4; ++p, ++Digits)
        {
            Value = Value * 10 + (*p - '0');
        }
        if (Digits == 0 || Digits > 3 || Value > 255 || *p != (Part < 3 ? '.' : '\0'))
        {
            return false;
        }
        if (Part < 3)
        {
            ++p;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_IsMulticast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosNode_IsMulticast(const char* Address)
{
    if (strchr(Address, ':') != NULL)
    {
        return (Address[0] == 'f' || Address[0] == 'F') &&
               (Address[1] == 'f' || Address[1] == 'F');
    }
    const int First = atoi(Address);
    return First >= 224 && First <= 239;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteTransportFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosNode_WriteTransportFile(const NmosNodeSender* Sender, NmosBuffer* Text)
{
    DtNmosSession Session;
    memset(&Session, 0, sizeof(Session));
    Session.Size = sizeof(Session);
    Session.SessionId = Sender->SessionId;
    Session.SessionVersion = Sender->SessionVersion;
    Session.Name = Sender->Label;
    // The SDP gives the address the sender sends from now as its origin and its
    // source-filter.
    snprintf(Session.OriginIp, sizeof(Session.OriginIp), "%s", Sender->ActiveSourceIp);
    DtNmosFlow Flow = Sender->Flow;
    snprintf(Flow.SourceIp, sizeof(Flow.SourceIp), "%s", Sender->ActiveSourceIp);
    // A reference clock of localmac is that of the interface the sender sends from,
    // whose MAC address IS-04 gives as the port_id of its binding.
    if (Flow.RefClock.Kind == DTNMOS_REFCLOCK_LOCALMAC)
    {
        size_t Count = 0;
        NmosInterface* List = NmosOs_Interfaces(&Count);
        for (size_t i = 0; i < Count; ++i)
        {
            if (strcmp(List[i].Address, Sender->ActiveSourceIp) == 0)
            {
                for (size_t c = 0;
                     c < sizeof(Flow.RefClock.LocalMac) - 1 && List[i].PortId[c] != '\0';
                     ++c)
                {
                    Flow.RefClock.LocalMac[c] =
                        (char)toupper((unsigned char)List[i].PortId[c]);
                    Flow.RefClock.LocalMac[c + 1] = '\0';
                }
                break;
            }
        }
        free(List);
    }
    return NmosSdp_Write(&Session, &Flow, 1, Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NewVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void NewVersion(DtNmosNode* Node, char* Version, size_t Size)
{
    NmosOs_VersionNow(&Node->LastVersion, Version, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_FindDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosNodeDevice* NmosNode_FindDevice(DtNmosNode* Node, const DtNmosId* Id)
{
    for (size_t i = 0; i < Node->DeviceCount; ++i)
    {
        if (strcmp(Node->Devices[i].Id.Text, Id->Text) == 0)
        {
            return &Node->Devices[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_FindSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosNodeSender* NmosNode_FindSender(DtNmosNode* Node, const DtNmosId* Id)
{
    for (size_t i = 0; i < Node->SenderCount; ++i)
    {
        if (strcmp(Node->Senders[i].Id.Text, Id->Text) == 0)
        {
            return &Node->Senders[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_FindReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosNodeReceiver* NmosNode_FindReceiver(DtNmosNode* Node, const DtNmosId* Id)
{
    for (size_t i = 0; i < Node->ReceiverCount; ++i)
    {
        if (strcmp(Node->Receivers[i].Id.Text, Id->Text) == 0)
        {
            return &Node->Receivers[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IdTaken -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IdTaken(DtNmosNode* Node, const DtNmosId* Id)
{
    return strcmp(Node->Id.Text, Id->Text) == 0 ||
           NmosNode_FindDevice(Node, Id) != NULL ||
           NmosNode_FindSender(Node, Id) != NULL ||
           NmosNode_FindReceiver(Node, Id) != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Grow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Grows an array of count elements of size bytes to hold one more.
//
static bool Grow(void** Array, size_t* Capacity, size_t Count, size_t Size)
{
    if (Count < *Capacity)
    {
        return true;
    }
    const size_t Grown = *Capacity == 0 ? 4 : *Capacity * 2;
    void* Larger = realloc(*Array, Grown * Size);
    if (Larger == NULL)
    {
        return false;
    }
    *Array = Larger;
    *Capacity = Grown;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TouchDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The device of a sender or receiver changes with it, as it lists them, and so does the
// node, whose interfaces are those of its senders and receivers.
//
static void TouchDevice(DtNmosNode* Node, const DtNmosId* Id)
{
    NmosNodeDevice* Device = NmosNode_FindDevice(Node, Id);
    if (Device != NULL)
    {
        NewVersion(Node, Device->Version, sizeof(Device->Version));
        Device->Registered = false;
    }
    NmosNode_Touch(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Touch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_Touch(DtNmosNode* Node)
{
    NewVersion(Node, Node->Version, sizeof(Node->Version));
    Node->NodeRegistered = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddDevice(DtNmosNode* Node, const DtNmosDeviceConfig* Device)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_AddDevice");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Device == NULL || Device->Id.Text[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "A device needs an ID.");
    }
    const DtNmosResult Sized =
        DTNMOS_CHECK_SIZE(Device, DtNmosDeviceConfig, sizeof(DtNmosDeviceConfig));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    NmosNode_Lock(Node);
    DtNmosResult Result = DTNMOS_OK;
    if (IdTaken(Node, &Device->Id))
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                                Device->Id.Text);
    }
    else if (!Grow((void**)&Node->Devices, &Node->DeviceCapacity, Node->DeviceCount,
                   sizeof(*Node->Devices)))
    {
        Result = NmosError_FailMemory();
    }
    else
    {
        NmosNodeDevice* Added = &Node->Devices[Node->DeviceCount];
        memset(Added, 0, sizeof(*Added));
        Added->Id = Device->Id;
        Added->Label = CopyText(Device->Label);
        Added->Description = CopyText(Device->Description);
        if (Added->Label == NULL || Added->Description == NULL)
        {
            free(Added->Label);
            free(Added->Description);
            Result = NmosError_FailMemory();
        }
        else
        {
            NewVersion(Node, Added->Version, sizeof(Added->Version));
            ++Node->DeviceCount;
        }
    }
    NmosNode_Unlock(Node);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DerivedId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes the ID of a resource that a sender brings along, from the ID of the sender.
//
static void DerivedId(const DtNmosId* Sender, const char* What, DtNmosId* Id)
{
    if (DtNmosId_FromName(Sender, What, Id) != DTNMOS_OK)
    {
        memset(Id, 0, sizeof(*Id));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddSender(DtNmosNode* Node, const DtNmosSenderConfig* Sender,
                                  DtNmosSenderActivateFunc Activate, void* User)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_AddSender");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Sender == NULL || Sender->Id.Text[0] == '\0' || Sender->Flow == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A sender needs an ID and a flow.");
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(
        Sender, DtNmosSenderConfig, offsetof(DtNmosSenderConfig, ActivationLeadMs));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    if (!NmosNode_IsAddress(Sender->SourceIp))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A sender needs the address of the port it sends from.");
    }
    const DtNmosResult FlowSized =
        DTNMOS_CHECK_SIZE(Sender->Flow, DtNmosFlow, sizeof(DtNmosFlow));
    if (FlowSized != DTNMOS_OK)
    {
        return FlowSized;
    }
    if (Sender->Flow->Media != DTNMOS_MEDIA_VIDEO &&
        Sender->Flow->Media != DTNMOS_MEDIA_AUDIO)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A sender of the node sends video or audio, not %s.",
                              DtNmosMedia_Name(Sender->Flow->Media));
    }
    NmosNode_Lock(Node);
    DtNmosResult Result = DTNMOS_OK;
    if (IdTaken(Node, &Sender->Id))
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                                Sender->Id.Text);
    }
    else if (NmosNode_FindDevice(Node, &Sender->DeviceId) == NULL)
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has no device %s.",
                                Sender->DeviceId.Text);
    }
    else if (!Grow((void**)&Node->Senders, &Node->SenderCapacity, Node->SenderCount,
                   sizeof(*Node->Senders)))
    {
        Result = NmosError_FailMemory();
    }
    else
    {
        NmosNodeSender* Added = &Node->Senders[Node->SenderCount];
        memset(Added, 0, sizeof(*Added));
        Added->Id = Sender->Id;
        Added->DeviceId = Sender->DeviceId;
        DerivedId(&Sender->Id, "source", &Added->SourceId);
        DerivedId(&Sender->Id, "flow", &Added->FlowId);
        Added->Label = CopyText(Sender->Label);
        Added->Description = CopyText(Sender->Description);
        Added->SourceIp = CopyText(Sender->SourceIp);
        Added->Activate = Activate;
        Added->User = User;
        Added->MasterEnable = true;
        Added->SessionId = Node->LastVersion / 1000000000u;
        Added->SessionVersion = 1;
        if (Sender->Size >= offsetof(DtNmosSenderConfig, ActivationLeadMs) +
                                sizeof(Sender->ActivationLeadMs))
        {
            Added->LeadNs = (uint64_t)Sender->ActivationLeadMs * 1000000u;
        }
        if (Added->Label == NULL || Added->Description == NULL ||
            Added->SourceIp == NULL ||
            NmosFlow_Copy(&Added->Flow, &Added->FlowStore, Sender->Flow) != DTNMOS_OK ||
            NmosConnection_InitSender(Added) != DTNMOS_OK)
        {
            FreeSender(Added);
            Result = NmosError_FailMemory();
        }
        else
        {
            NewVersion(Node, Added->Version, sizeof(Added->Version));
            ++Node->SenderCount;
            TouchDevice(Node, &Sender->DeviceId);
        }
    }
    NmosNode_Unlock(Node);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddReceiver(DtNmosNode* Node,
                                    const DtNmosReceiverConfig* Receiver,
                                    DtNmosReceiverActivateFunc Activate, void* User)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_AddReceiver");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Receiver == NULL || Receiver->Id.Text[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "A receiver needs an ID.");
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(
        Receiver, DtNmosReceiverConfig, offsetof(DtNmosReceiverConfig, ActivationLeadMs));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    if (!NmosNode_IsAddress(Receiver->InterfaceIp))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A receiver needs the address of the port it receives on.");
    }
    if (Receiver->Media != DTNMOS_MEDIA_VIDEO && Receiver->Media != DTNMOS_MEDIA_AUDIO)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A receiver of the node receives video or audio, not %s.",
                              DtNmosMedia_Name(Receiver->Media));
    }
    NmosNode_Lock(Node);
    DtNmosResult Result = DTNMOS_OK;
    if (IdTaken(Node, &Receiver->Id))
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                                Receiver->Id.Text);
    }
    else if (NmosNode_FindDevice(Node, &Receiver->DeviceId) == NULL)
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has no device %s.",
                                Receiver->DeviceId.Text);
    }
    else if (!Grow((void**)&Node->Receivers, &Node->ReceiverCapacity, Node->ReceiverCount,
                   sizeof(*Node->Receivers)))
    {
        Result = NmosError_FailMemory();
    }
    else
    {
        NmosNodeReceiver* Added = &Node->Receivers[Node->ReceiverCount];
        memset(Added, 0, sizeof(*Added));
        Added->Id = Receiver->Id;
        Added->DeviceId = Receiver->DeviceId;
        Added->Media = Receiver->Media;
        Added->Label = CopyText(Receiver->Label);
        Added->Description = CopyText(Receiver->Description);
        Added->InterfaceIp = CopyText(Receiver->InterfaceIp);
        Added->Activate = Activate;
        Added->User = User;
        if (Receiver->Size >= offsetof(DtNmosReceiverConfig, ActivationLeadMs) +
                                  sizeof(Receiver->ActivationLeadMs))
        {
            Added->LeadNs = (uint64_t)Receiver->ActivationLeadMs * 1000000u;
        }
        if (Added->Label == NULL || Added->Description == NULL ||
            Added->InterfaceIp == NULL || NmosConnection_InitReceiver(Added) != DTNMOS_OK)
        {
            FreeReceiver(Added);
            Result = NmosError_FailMemory();
        }
        else
        {
            NewVersion(Node, Added->Version, sizeof(Added->Version));
            ++Node->ReceiverCount;
            TouchDevice(Node, &Receiver->DeviceId);
        }
    }
    NmosNode_Unlock(Node);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ScheduleRemoval -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Adds a resource of type to delete from the registry, when it was registered.
//
static bool ScheduleRemoval(DtNmosNode* Node, const char* Type, const DtNmosId* Id)
{
    if (!Grow((void**)&Node->Removals, &Node->RemovalCapacity, Node->RemovalCount,
              sizeof(*Node->Removals)))
    {
        return false;
    }
    NmosNodeRemoval* Removal = &Node->Removals[Node->RemovalCount++];
    snprintf(Removal->Type, sizeof(Removal->Type), "%s", Type);
    Removal->Id = *Id;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RemoveSenderAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void RemoveSenderAt(DtNmosNode* Node, size_t Index)
{
    NmosNodeSender* Sender = &Node->Senders[Index];
    if (Sender->WasRegistered)
    {
        ScheduleRemoval(Node, "senders", &Sender->Id);
        ScheduleRemoval(Node, "flows", &Sender->FlowId);
        ScheduleRemoval(Node, "sources", &Sender->SourceId);
    }
    TouchDevice(Node, &Sender->DeviceId);
    FreeSender(Sender);
    memmove(Sender, Sender + 1, (Node->SenderCount - Index - 1) * sizeof(*Sender));
    --Node->SenderCount;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RemoveReceiverAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void RemoveReceiverAt(DtNmosNode* Node, size_t Index)
{
    NmosNodeReceiver* Receiver = &Node->Receivers[Index];
    if (Receiver->WasRegistered)
    {
        ScheduleRemoval(Node, "receivers", &Receiver->Id);
    }
    TouchDevice(Node, &Receiver->DeviceId);
    FreeReceiver(Receiver);
    memmove(Receiver, Receiver + 1,
            (Node->ReceiverCount - Index - 1) * sizeof(*Receiver));
    --Node->ReceiverCount;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Remove -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Remove(DtNmosNode* Node, const DtNmosId* Id)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_Remove");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Id == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosNode_Remove() needs an ID.");
    }
    NmosNode_Lock(Node);
    DtNmosResult Result = DTNMOS_OK;
    NmosNodeDevice* Device = NmosNode_FindDevice(Node, Id);
    if (Device != NULL)
    {
        for (size_t i = Node->SenderCount; i > 0; --i)
        {
            if (strcmp(Node->Senders[i - 1].DeviceId.Text, Id->Text) == 0)
            {
                RemoveSenderAt(Node, i - 1);
            }
        }
        for (size_t i = Node->ReceiverCount; i > 0; --i)
        {
            if (strcmp(Node->Receivers[i - 1].DeviceId.Text, Id->Text) == 0)
            {
                RemoveReceiverAt(Node, i - 1);
            }
        }
        Device = NmosNode_FindDevice(Node, Id);
        if (Device->Registered || Device->WasRegistered)
        {
            ScheduleRemoval(Node, "devices", &Device->Id);
        }
        free(Device->Label);
        free(Device->Description);
        const size_t Index = (size_t)(Device - Node->Devices);
        memmove(Device, Device + 1, (Node->DeviceCount - Index - 1) * sizeof(*Device));
        --Node->DeviceCount;
    }
    else if (NmosNode_FindSender(Node, Id) != NULL)
    {
        RemoveSenderAt(Node, (size_t)(NmosNode_FindSender(Node, Id) - Node->Senders));
    }
    else if (NmosNode_FindReceiver(Node, Id) != NULL)
    {
        RemoveReceiverAt(Node,
                         (size_t)(NmosNode_FindReceiver(Node, Id) - Node->Receivers));
    }
    else
    {
        Result = NmosError_Fail(DTNMOS_E_NOT_FOUND, "The node has no %s.", Id->Text);
    }
    NmosNode_Unlock(Node);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_UpdateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                     const DtNmosFlow* Flow)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_UpdateSender");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Id == NULL || Flow == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosNode_UpdateSender() needs an ID and a flow.");
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(Flow, DtNmosFlow, sizeof(DtNmosFlow));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    NmosNode_Lock(Node);
    DtNmosResult Result = DTNMOS_OK;
    NmosNodeSender* Sender = NmosNode_FindSender(Node, Id);
    if (Sender == NULL)
    {
        Result =
            NmosError_Fail(DTNMOS_E_NOT_FOUND, "The node has no sender %s.", Id->Text);
    }
    else if (Flow->Media != Sender->Flow.Media)
    {
        Result = NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT, "Sender %s sends %s and cannot send %s.", Id->Text,
            DtNmosMedia_Name(Sender->Flow.Media), DtNmosMedia_Name(Flow->Media));
    }
    else
    {
        // The new flow gets an owner of its own, which replaces the old one only
        // once all of it was copied.
        NmosStore Store;
        memset(&Store, 0, sizeof(Store));
        Result = NmosFlow_Copy(&Sender->Flow, &Store, Flow);
        if (Result == DTNMOS_OK)
        {
            NmosStore_Free(&Sender->FlowStore);
            Sender->FlowStore = Store;
        }
        else
        {
            NmosStore_Free(&Store);
        }
    }
    if (Result == DTNMOS_OK && Sender != NULL)
    {
        NewVersion(Node, Sender->Version, sizeof(Sender->Version));
        ++Sender->SessionVersion;
        Sender->Registered = false;
    }
    NmosNode_Unlock(Node);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ForgetRegistration -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Marks everything unregistered, as when the registry has lost the node.
//
static void ForgetRegistration(DtNmosNode* Node)
{
    // Registering everything again is a first registration, which a registry holding an
    // old node of this ID answers with 200.
    Node->FirstRegistration = true;
    Node->NodeRegistered = false;
    for (size_t i = 0; i < Node->DeviceCount; ++i)
    {
        Node->Devices[i].Registered = false;
    }
    for (size_t i = 0; i < Node->SenderCount; ++i)
    {
        Node->Senders[i].Registered = false;
    }
    for (size_t i = 0; i < Node->ReceiverCount; ++i)
    {
        Node->Receivers[i].Registered = false;
    }
}

// One registration the poll makes: the resources it posts, rendered under the lock, and
// the version it stands for.
typedef struct NmosPending
{
    int Kind; // 0 node, 1 device, 2 sender with source and flow, 3 receiver
    DtNmosId Id;
    char Version[32];
    char* Bodies[3];
    const char* Types[3];
    size_t Count;
} NmosPending;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreePending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FreePending(NmosPending* p)
{
    for (size_t i = 0; i < p->Count; ++i)
    {
        free(p->Bodies[i]);
    }
    memset(p, 0, sizeof(*p));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextPending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Renders into p the first resource that is not registered, parents before children;
// returns false when all are registered.
//
static bool NextPending(DtNmosNode* Node, NmosPending* p)
{
    memset(p, 0, sizeof(*p));
    NmosBuffer b[3];
    memset(b, 0, sizeof(b));
    if (Node->Closing)
    {
        return false;
    }
    if (!Node->NodeRegistered)
    {
        p->Kind = 0;
        p->Id = Node->Id;
        snprintf(p->Version, sizeof(p->Version), "%s", Node->Version);
        NmosNode_WriteSelf(Node, &b[0]);
        p->Types[0] = "node";
        p->Count = 1;
    }
    else
    {
        for (size_t i = 0; i < Node->DeviceCount && p->Count == 0; ++i)
        {
            if (!Node->Devices[i].Registered)
            {
                p->Kind = 1;
                p->Id = Node->Devices[i].Id;
                snprintf(p->Version, sizeof(p->Version), "%s", Node->Devices[i].Version);
                NmosNode_WriteDevice(Node, &Node->Devices[i], &b[0]);
                p->Types[0] = "device";
                p->Count = 1;
            }
        }
        for (size_t i = 0; i < Node->SenderCount && p->Count == 0; ++i)
        {
            const NmosNodeSender* Sender = &Node->Senders[i];
            const NmosNodeDevice* Device = NmosNode_FindDevice(Node, &Sender->DeviceId);
            if (!Sender->Registered && Device != NULL && Device->Registered)
            {
                p->Kind = 2;
                p->Id = Sender->Id;
                snprintf(p->Version, sizeof(p->Version), "%s", Sender->Version);
                NmosNode_WriteSource(Sender, &b[0]);
                NmosNode_WriteFlow(Sender, &b[1]);
                NmosNode_WriteSender(Node, Sender, &b[2]);
                p->Types[0] = "source";
                p->Types[1] = "flow";
                p->Types[2] = "sender";
                p->Count = 3;
            }
        }
        for (size_t i = 0; i < Node->ReceiverCount && p->Count == 0; ++i)
        {
            const NmosNodeReceiver* Receiver = &Node->Receivers[i];
            const NmosNodeDevice* Device = NmosNode_FindDevice(Node, &Receiver->DeviceId);
            if (!Receiver->Registered && Device != NULL && Device->Registered)
            {
                p->Kind = 3;
                p->Id = Receiver->Id;
                snprintf(p->Version, sizeof(p->Version), "%s", Receiver->Version);
                NmosNode_WriteReceiver(Receiver, &b[0]);
                p->Types[0] = "receiver";
                p->Count = 1;
            }
        }
    }
    for (size_t i = 0; i < p->Count; ++i)
    {
        p->Bodies[i] = b[i].Failed ? NULL : b[i].Data;
        if (b[i].Failed)
        {
            NmosBuffer_Free(&b[i]);
        }
    }
    return p->Count > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MarkRegistered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Marks what p registered, unless it changed meanwhile.
//
static void MarkRegistered(DtNmosNode* Node, const NmosPending* p)
{
    // The registry holds the node, in this version or an older one.
    Node->NodeWasRegistered |= p->Kind == 0;
    if (p->Kind == 0 && strcmp(Node->Version, p->Version) == 0)
    {
        Node->NodeRegistered = true;
    }
    NmosNodeDevice* Device = p->Kind == 1 ? NmosNode_FindDevice(Node, &p->Id) : NULL;
    if (Device != NULL && strcmp(Device->Version, p->Version) == 0)
    {
        Device->Registered = true;
        Device->WasRegistered = true;
    }
    NmosNodeSender* Sender = p->Kind == 2 ? NmosNode_FindSender(Node, &p->Id) : NULL;
    if (Sender != NULL && strcmp(Sender->Version, p->Version) == 0)
    {
        Sender->Registered = true;
        Sender->WasRegistered = true;
    }
    NmosNodeReceiver* Receiver =
        p->Kind == 3 ? NmosNode_FindReceiver(Node, &p->Id) : NULL;
    if (Receiver != NULL && strcmp(Receiver->Version, p->Version) == 0)
    {
        Receiver->Registered = true;
        Receiver->WasRegistered = true;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_IsRegistered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtNmosNode_IsRegistered(const DtNmosNode* Node)
{
    if (Node == NULL || !Node->Open)
    {
        return false;
    }
    DtNmosNode* MutableNode = (DtNmosNode*)Node;
    NmosNode_Lock(MutableNode);
    bool All = Node->NodeRegistered && Node->RemovalCount == 0;
    for (size_t i = 0; All && i < Node->DeviceCount; ++i)
    {
        All = Node->Devices[i].Registered;
    }
    for (size_t i = 0; All && i < Node->SenderCount; ++i)
    {
        All = Node->Senders[i].Registered;
    }
    for (size_t i = 0; All && i < Node->ReceiverCount; ++i)
    {
        All = Node->Receivers[i].Registered;
    }
    NmosNode_Unlock(MutableNode);
    return All;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Moves the node to the registry at Url, which the caller of the node chose, and with
// which it registers everything from the start. Returns false when the memory ran out. On
// the poll thread.
//
static bool TakeRegistry(DtNmosNode* Node, const char* Url)
{
    char* Base = RegistrationBase(Url);
    if (Base == NULL)
    {
        return false;
    }
    NmosNode_Lock(Node);
    free(Node->Registration);
    Node->Registration = Base;
    ForgetRegistration(Node);
    NmosNode_Unlock(Node);
    Node->Failures = 0;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasFailed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the registry of the base URL Base failed since the node last started over.
//
static bool HasFailed(const DtNmosNode* Node, const char* Base)
{
    for (size_t i = 0; i < Node->FailedCount; ++i)
    {
        if (strcmp(Node->Failed[i], Base) == 0)
        {
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ForgetFailed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ForgetFailed(DtNmosNode* Node)
{
    for (size_t i = 0; i < Node->FailedCount; ++i)
    {
        free(Node->Failed[i]);
    }
    Node->FailedCount = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MarkFailed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Marks the registry of the node as failed, and leaves it: the node has no registry
// until it takes another, with what it registered kept, for a heartbeat first. On the
// poll thread.
//
static void MarkFailed(DtNmosNode* Node)
{
    NmosNode_Lock(Node);
    char* Base = Node->Registration;
    Node->Registration = NULL;
    NmosNode_Unlock(Node);
    if (Base == NULL)
    {
        return;
    }
    if (Node->FailedCount < sizeof(Node->Failed) / sizeof(Node->Failed[0]) &&
        !HasFailed(Node, Base))
    {
        Node->Failed[Node->FailedCount++] = Base;
    }
    else
    {
        free(Base);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeFound -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves a node without a registry to the most preferred usable one its search found that
// has not failed yet, so that it goes down the list and never back to one that failed;
// when all have, forgets that they failed and starts over from the most preferred. A
// node registered before sends a heartbeat first, which tells it whether the registry
// has it; another registers everything. Returns false when there is none, having had the
// search search sooner. On the poll thread.
//
static bool TakeFound(DtNmosNode* Node)
{
    DtNmosRegistryList* Found = NULL;
    if (DtNmosRegistrySearch_List(Node->Search, DTNMOS_SERVICE_REGISTRATION, &Found) !=
        DTNMOS_OK)
    {
        NodeLog(Node, DTNMOS_LOG_WARNING, "The search gives no registries: %s",
                DtNmos_GetLastError());
    }
    bool Taken = false;
    for (int Round = 0; Round < 2 && !Taken; ++Round)
    {
        char* Base = NULL;
        char Url[DTNMOS_MAX_URL_SIZE] = "";
        bool AnyFailed = false;
        NmosNode_Lock(Node);
        for (size_t i = 0; Base == NULL && i < DtNmosRegistryList_Count(Found); ++i)
        {
            const DtNmosRegistryInfo* Info = DtNmosRegistryList_At(Found, i);
            char* Candidate = Info->Usable ? RegistrationBase(Info->Url) : NULL;
            if (Candidate != NULL && HasFailed(Node, Candidate))
            {
                AnyFailed = true;
                free(Candidate);
                Candidate = NULL;
            }
            if (Candidate != NULL)
            {
                Base = Candidate;
                snprintf(Url, sizeof(Url), "%s", Info->Url);
            }
        }
        if (Base != NULL)
        {
            Node->Registration = Base;
            if (Node->NodeRegistered)
            {
                Node->NextHeartbeatMs = 0;
            }
            else
            {
                ForgetRegistration(Node);
            }
        }
        NmosNode_Unlock(Node);
        if (Base != NULL)
        {
            Node->Failures = 0;
            NodeLog(Node, DTNMOS_LOG_INFO, "The node registers with %s, of its search.",
                    Url);
            Taken = true;
        }
        else if (AnyFailed)
        {
            ForgetFailed(Node);
        }
        else
        {
            break;
        }
    }
    DtNmosRegistryList_Free(Found);
    if (!Taken)
    {
        NmosRegistrySearch_Hurry(Node->Search);
    }
    return Taken;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Poll(DtNmosNode* Node, uint32_t* NextMs)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_Poll");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    // Scheduled activations first, as they do not wait for the registry.
    uint32_t ScheduledMs = UINT32_MAX;
    NmosConnection_Poll(Node, &ScheduledMs);
    // A node without a registry takes one its search found; until there is one, it has
    // nothing to register with, which is no failure.
    if (Node->Registration == NULL && Node->Searches && !Node->Closing)
    {
        TakeFound(Node);
    }
    if (Node->Registration == NULL)
    {
        if (NextMs != NULL)
        {
            *NextMs = ScheduledMs < 250u ? ScheduledMs : 250u;
        }
        return DTNMOS_OK;
    }
    DtNmosResult Result = DTNMOS_OK;
    // Deletions first, children before parents as they were scheduled.
    for (;;)
    {
        NmosNode_Lock(Node);
        if (Node->RemovalCount == 0)
        {
            NmosNode_Unlock(Node);
            break;
        }
        const NmosNodeRemoval Removal = Node->Removals[0];
        NmosNode_Unlock(Node);
        char Path[96];
        snprintf(Path, sizeof(Path), "resource/%s/%s", Removal.Type, Removal.Id.Text);
        int Status = 0;
        Result = RegistryRequest(Node, "DELETE", Path, NULL, &Status);
        if (Result != DTNMOS_OK)
        {
            break;
        }
        NmosNode_Lock(Node);
        if (Node->RemovalCount > 0 &&
            strcmp(Node->Removals[0].Id.Text, Removal.Id.Text) == 0)
        {
            memmove(Node->Removals, Node->Removals + 1,
                    (Node->RemovalCount - 1) * sizeof(*Node->Removals));
            --Node->RemovalCount;
        }
        NmosNode_Unlock(Node);
    }
    // Registrations, one resource at a time, each rendered anew under the lock.
    while (Result == DTNMOS_OK)
    {
        NmosPending p;
        NmosNode_Lock(Node);
        const bool Any = NextPending(Node, &p);
        NmosNode_Unlock(Node);
        if (!Any)
        {
            break;
        }
        for (size_t i = 0; Result == DTNMOS_OK && i < p.Count; ++i)
        {
            Result = p.Bodies[i] == NULL
                         ? NmosError_FailMemory()
                         : RegisterResource(Node, p.Types[i], p.Bodies[i]);
        }
        if (Result == DTNMOS_OK)
        {
            NmosNode_Lock(Node);
            MarkRegistered(Node, &p);
            if (p.Kind == 0)
            {
                Node->NextHeartbeatMs = NmosOs_MonotonicMs() + Node->HeartbeatMs;
            }
            NmosNode_Unlock(Node);
        }
        FreePending(&p);
    }
    // The heartbeat, when it is due.
    NmosNode_Lock(Node);
    const bool Beat = Result == DTNMOS_OK && Node->NodeRegistered &&
                      NmosOs_MonotonicMs() >= Node->NextHeartbeatMs;
    NmosNode_Unlock(Node);
    if (Beat)
    {
        char Path[96];
        snprintf(Path, sizeof(Path), "health/nodes/%s", Node->Id.Text);
        int Status = 0;
        Result = RegistryRequest(Node, "POST", Path, NULL, &Status);
        NmosNode_Lock(Node);
        if (Result == DTNMOS_OK && Status == 404)
        {
            NodeLog(Node, DTNMOS_LOG_WARNING,
                    "The registry has lost node %s; it registers again.", Node->Id.Text);
            ForgetRegistration(Node);
        }
        else if (Result == DTNMOS_OK && Status != 200)
        {
            Result = NmosError_Fail(DTNMOS_E_HTTP,
                                    "The registry answered %d to a heartbeat.", Status);
        }
        Node->NextHeartbeatMs = NmosOs_MonotonicMs() + Node->HeartbeatMs;
        NmosNode_Unlock(Node);
    }
    // A registry that fails polls in a row makes way for another one: the one the caller
    // gives, when it gives one, or else, for a node that searches, the next one found.
    // The URL is swapped here, on the thread that reads it.
    bool Switched = false;
    if (Result == DTNMOS_OK)
    {
        Node->Failures = 0;
    }
    else if (++Node->Failures >= Node->FailuresBeforeSwitch && !Node->Closing)
    {
        const uint32_t Failures = Node->Failures;
        char Next[DTNMOS_MAX_URL_SIZE] = "";
        bool Moved = Node->RegistryFailed != NULL &&
                     Node->RegistryFailed(Node->RegistryFailedUser, Failures, Next,
                                          sizeof(Next)) &&
                     memchr(Next, '\0', sizeof(Next)) != NULL && Next[0] != '\0' &&
                     TakeRegistry(Node, Next);
        if (Moved)
        {
            NodeLog(Node, DTNMOS_LOG_WARNING,
                    "The registry failed %u polls in a row; the node registers with %s.",
                    (unsigned)Failures, Next);
        }
        if (!Moved && Node->Searches)
        {
            // The next registry found, with a heartbeat at once, as the failed one may
            // have been the registry of the node until a moment ago.
            NodeLog(Node, DTNMOS_LOG_WARNING,
                    "The registry failed %u polls in a row; the node takes another.",
                    (unsigned)Failures);
            MarkFailed(Node);
            Moved = TakeFound(Node);
        }
        Switched = Moved;
    }
    if (NextMs != NULL)
    {
        NmosNode_Lock(Node);
        const uint64_t Now = NmosOs_MonotonicMs();
        uint32_t Wait = Result != DTNMOS_OK || !Node->NodeRegistered ? 1000u : 0u;
        if (Result == DTNMOS_OK && Node->NodeRegistered)
        {
            Wait = Node->NextHeartbeatMs > Now ? (uint32_t)(Node->NextHeartbeatMs - Now)
                                               : 0u;
        }
        // A node that just moved to another registry contacts it at once; one that
        // searches and has none looks again soon at what its search found.
        if (Switched)
        {
            Wait = 0;
        }
        else if (Node->Searches && Node->Registration == NULL)
        {
            Wait = Wait < 250u ? Wait : 250u;
        }
        NmosNode_Unlock(Node);
        *NextMs = Wait < ScheduledMs ? Wait : ScheduledMs;
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UnregisterAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Deletes from the registry everything the node registered, children before parents.
//
static void UnregisterAll(DtNmosNode* Node)
{
    NmosNode_Lock(Node);
    for (size_t i = Node->SenderCount; i > 0; --i)
    {
        RemoveSenderAt(Node, i - 1);
    }
    for (size_t i = Node->ReceiverCount; i > 0; --i)
    {
        RemoveReceiverAt(Node, i - 1);
    }
    for (size_t i = 0; i < Node->DeviceCount; ++i)
    {
        if (Node->Devices[i].WasRegistered)
        {
            ScheduleRemoval(Node, "devices", &Node->Devices[i].Id);
        }
    }
    Node->NodeRegistered = false;
    Node->Closing = true;
    if (Node->NodeWasRegistered)
    {
        ScheduleRemoval(Node, "nodes", &Node->Id);
    }
    NmosNode_Unlock(Node);
    DtNmosNode_Poll(Node, NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_Close(DtNmosNode* Node)
{
    const DtNmosResult Result = NmosNode_CheckOpen(Node, "DtNmosNode_Close");
    if (Result != DTNMOS_OK)
    {
        return Result;
    }
    NmosServer_Stop(Node);
    UnregisterAll(Node);
    NmosNode_Release(Node);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosNode_Free(DtNmosNode* Node)
{
    if (Node == NULL)
    {
        return;
    }
    if (Node->Open)
    {
        DtNmosNode_Close(Node);
    }
    free(Node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosNode_Freep(DtNmosNode** Node)
{
    if (Node != NULL)
    {
        DtNmosNode_Free(*Node);
        *Node = NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_ApiPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint16_t DtNmosNode_ApiPort(const DtNmosNode* Node)
{
    return Node == NULL || !Node->Open ? 0 : Node->ApiPort;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_ApiUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_ApiUrl(const DtNmosNode* Node, char* Buffer, size_t* Size)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_ApiUrl");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Size == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosNode_ApiUrl() needs a size.");
    }
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosNode_WriteBaseUrl(Node, &b);
    const DtNmosResult Result = b.Failed
                                    ? NmosError_FailMemory()
                                    : NmosText_CopyText(Buffer, Size, b.Data, b.Length);
    NmosBuffer_Free(&b);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_AnswerError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers with an error of the APIs of NMOS, a JSON object with its code and message.
//
void NmosNode_AnswerError(DtNmosHttpResponse* Response, int Status, const char* Message)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosBuffer_Printf(&b, "{\"code\": %d, \"error\": ", Status);
    NmosJson_WriteString(&b, Message);
    DTNMOS_APPEND_LITERAL(&b, ", \"debug\": null}");
    DtNmosHttpResponse_SetStatus(Response, Status);
    if (!b.Failed)
    {
        DtNmosHttpResponse_SetBody(Response, "application/json", b.Data, b.Length);
    }
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_AnswerJson -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers with the JSON of b, or with an error when it failed.
//
void NmosNode_AnswerJson(DtNmosHttpResponse* Response, NmosBuffer* b)
{
    if (b->Failed)
    {
        NmosNode_AnswerError(Response, 500, "Out of memory.");
        return;
    }
    DtNmosHttpResponse_SetStatus(Response, 200);
    DtNmosHttpResponse_SetBody(Response, "application/json", b->Data, b->Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerNodeApi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers the Node API; segments follow /x-nmos/node/v1.3.
//
static void AnswerNodeApi(DtNmosNode* Node, char** Segments, size_t Count,
                          DtNmosHttpResponse* Response)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    static const char* const Collections[] = {"sources", "flows", "devices", "senders",
                                              "receivers"};
    if (Count == 0)
    {
        NmosBuffer_Printf(&b, "[\"self/\", \"sources/\", \"flows/\", \"devices/\", "
                              "\"senders/\", \"receivers/\"]");
        NmosNode_AnswerJson(Response, &b);
        NmosBuffer_Free(&b);
        return;
    }
    if (Count == 1 && strcmp(Segments[0], "self") == 0)
    {
        NmosNode_WriteSelf(Node, &b);
        NmosNode_AnswerJson(Response, &b);
        NmosBuffer_Free(&b);
        return;
    }
    int Collection = -1;
    for (int i = 0; i < 5; ++i)
    {
        if (strcmp(Segments[0], Collections[i]) == 0)
        {
            Collection = i;
        }
    }
    if (Collection < 0 || Count > 2)
    {
        NmosNode_AnswerError(Response, 404, "The Node API has no such resource.");
        return;
    }
    // Each collection is written whole, or only the member whose ID is segments[1].
    const char* Wanted = Count == 2 ? Segments[1] : NULL;
    int Written = 0;
    if (Wanted == NULL)
    {
        DTNMOS_APPEND_LITERAL(&b, "[");
    }
#define DTNMOS_WRITE_MEMBER(MemberId, Write)                                             \
    if (Wanted == NULL || strcmp((MemberId)->Text, Wanted) == 0)                         \
    {                                                                                    \
        if (Wanted == NULL && Written > 0)                                               \
        {                                                                                \
            DTNMOS_APPEND_LITERAL(&b, ", ");                                             \
        }                                                                                \
        Write;                                                                           \
        ++Written;                                                                       \
    }
    switch (Collection)
    {
    case 0:
        for (size_t i = 0; i < Node->SenderCount; ++i)
        {
            DTNMOS_WRITE_MEMBER(&Node->Senders[i].SourceId,
                                NmosNode_WriteSource(&Node->Senders[i], &b))
        }
        break;
    case 1:
        for (size_t i = 0; i < Node->SenderCount; ++i)
        {
            DTNMOS_WRITE_MEMBER(&Node->Senders[i].FlowId,
                                NmosNode_WriteFlow(&Node->Senders[i], &b))
        }
        break;
    case 2:
        for (size_t i = 0; i < Node->DeviceCount; ++i)
        {
            DTNMOS_WRITE_MEMBER(&Node->Devices[i].Id,
                                NmosNode_WriteDevice(Node, &Node->Devices[i], &b))
        }
        break;
    case 3:
        for (size_t i = 0; i < Node->SenderCount; ++i)
        {
            DTNMOS_WRITE_MEMBER(&Node->Senders[i].Id,
                                NmosNode_WriteSender(Node, &Node->Senders[i], &b))
        }
        break;
    default:
        for (size_t i = 0; i < Node->ReceiverCount; ++i)
        {
            DTNMOS_WRITE_MEMBER(&Node->Receivers[i].Id,
                                NmosNode_WriteReceiver(&Node->Receivers[i], &b))
        }
        break;
    }
#undef DTNMOS_WRITE_MEMBER
    if (Wanted != NULL && Written == 0)
    {
        NmosBuffer_Free(&b);
        NmosNode_AnswerError(Response, 404, "The node has no resource of that ID.");
        return;
    }
    if (Wanted == NULL)
    {
        DTNMOS_APPEND_LITERAL(&b, "]");
    }
    NmosNode_AnswerJson(Response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerNames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers a list of the names of the next level of an API.
//
static void AnswerNames(DtNmosHttpResponse* Response, const char* Names)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosBuffer_Append(&b, Names, strlen(Names));
    NmosNode_AnswerJson(Response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Handle(DtNmosNode* Node, const DtNmosHttpRequest* Request,
                               DtNmosHttpResponse* Response)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_Handle");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Request == NULL || Request->Url == NULL || Request->Method == NULL ||
        Response == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosNode_Handle() needs a node, a request and a response.");
    }
    const DtNmosResult Sized =
        DTNMOS_CHECK_SIZE(Request, DtNmosHttpRequest, sizeof(DtNmosHttpRequest));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    // Every answer lets a page of another origin use the APIs, as the CORS sections of
    // IS-04 and IS-05 ask, and a browser's preflight is answered at once.
    DtNmosHttpResponse_AddHeader(Response, "Access-Control-Allow-Origin", "*");
    DtNmosHttpResponse_AddHeader(Response, "Access-Control-Allow-Methods",
                                 "GET, PUT, POST, PATCH, HEAD, OPTIONS, DELETE");
    DtNmosHttpResponse_AddHeader(Response, "Access-Control-Allow-Headers",
                                 "Content-Type, Accept");
    DtNmosHttpResponse_AddHeader(Response, "Access-Control-Max-Age", "3600");
    if (strcmp(Request->Method, "OPTIONS") == 0)
    {
        DtNmosHttpResponse_SetStatus(Response, 200);
        return DTNMOS_OK;
    }
    // The path without its query, split into its segments.
    char Path[512];
    snprintf(Path, sizeof(Path), "%s", Request->Url);
    Path[strcspn(Path, "?#")] = '\0';
    char* Segments[16];
    size_t Count = 0;
    for (char* Part = strtok(Path, "/"); Part != NULL && Count < 16;
         Part = strtok(NULL, "/"))
    {
        Segments[Count++] = Part;
    }
    const int Get =
        strcmp(Request->Method, "GET") == 0 || strcmp(Request->Method, "HEAD") == 0;
    if (Count >= 3 && strcmp(Segments[0], "x-nmos") == 0 &&
        strcmp(Segments[1], "connection") == 0 && strcmp(Segments[2], "v1.1") == 0)
    {
        return NmosConnection_Handle(Node, Request, Segments + 3, Count - 3, Response);
    }
    // The target of a receiver is deprecated in IS-04 v1.3, for the Connection API, and a
    // node may leave it out with 501.
    if (Count == 6 && strcmp(Request->Method, "PUT") == 0 &&
        strcmp(Segments[0], "x-nmos") == 0 && strcmp(Segments[1], "node") == 0 &&
        strcmp(Segments[3], "receivers") == 0 && strcmp(Segments[5], "target") == 0)
    {
        NmosNode_AnswerError(Response, 501,
                             "The node connects its receivers through the Connection API "
                             "only.");
        return DTNMOS_OK;
    }
    if (!Get)
    {
        NmosNode_AnswerError(Response, 405, "The Node API answers GET only.");
        return DTNMOS_OK;
    }
    NmosNode_Lock(Node);
    if (Count == 0)
    {
        AnswerNames(Response, "[\"x-nmos/\"]");
    }
    else if (Count == 1 && strcmp(Segments[0], "x-nmos") == 0)
    {
        AnswerNames(Response, "[\"node/\", \"connection/\"]");
    }
    else if (Count == 2 && strcmp(Segments[0], "x-nmos") == 0 &&
             strcmp(Segments[1], "node") == 0)
    {
        AnswerNames(Response, "[\"v1.3/\"]");
    }
    else if (Count == 2 && strcmp(Segments[0], "x-nmos") == 0 &&
             strcmp(Segments[1], "connection") == 0)
    {
        AnswerNames(Response, "[\"v1.1/\"]");
    }
    else if (Count >= 3 && strcmp(Segments[0], "x-nmos") == 0 &&
             strcmp(Segments[1], "node") == 0 && strcmp(Segments[2], "v1.3") == 0)
    {
        AnswerNodeApi(Node, Segments + 3, Count - 3, Response);
    }
    else
    {
        NmosNode_AnswerError(Response, 404, "The node has no such API.");
    }
    NmosNode_Unlock(Node);
    return DTNMOS_OK;
}
