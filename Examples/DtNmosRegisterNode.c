// #*#*#*#*#*#*#*#*#*#*#*#* DtNmosRegisterNode.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Example: registers a node with a sender and a receiver, and prints what a
// controller activates on them
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Registers a node, its device, a video sender and a video receiver with a registry,
// given with --registry or found with DNS-SD, and serves the Node API and the Connection
// API of IS-05 for --seconds. The sender sends the first flow of the SDP of --sdp, or a
// flow of 1080p25 to 239.100.1.1:5004. The IDs follow from --label, so that the node
// keeps them when it starts again. Each activation a controller makes is printed:
//
//     node "dtnmos example" 6aac9516-..., registering with http://192.168.1.5:8010
//     sender 9dfb9312-..., receiver a3b1ccff-...
//     serves at http://192.168.1.20:42993
//     registered
//     receiver a3b1ccff-...: receive video 239.100.1.1:5004 from any source, ...
//     receiver a3b1ccff-...: stop receiving
//     unregistered
//
// Needs dtnmos built with libcurl and with the server; without the server it registers
// the node but serves no Connection API. Exits with 0 when the time is over, 2 when no
// registry was found, and 1 when a call failed.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/ExampleCommon.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption Options[] = {
    {"--registry", true, "The base URL of the registry, e.g. http://registry:8010"},
    {"--label", true, "The label of the node, from which its IDs follow"},
    {"--sdp", true, "An SDP file whose first flow the sender sends"},
    {"--port", true, "The port of the APIs of the node; any free one by default"},
    {"--seconds", true, "How long the node runs; 60 by default"},
    {"--verbose", false, "Prints what the node does"},
};

// The namespace of the IDs of this example's nodes, a version 4 UUID of its own.
static const DtNmosId Namespace = {"7c1d5a40-2b6e-4f39-9e0a-58d3b1c4e2f7"};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A program would configure what receives here, and fail with DtNmos_SetLastError()
// when it cannot; this one prints what it would receive.
//
static DtNmosResult ActivateReceiver(void* User, const DtNmosId* Receiver,
                                     const DtNmosReceiverActivation* Activation)
{
    (void)User;
    if (!Activation->MasterEnable)
    {
        printf("receiver %s: stop receiving\n", Receiver->Text);
    }
    else if (!Activation->HasFlow)
    {
        printf("receiver %s: receive, but the controller gave no SDP\n", Receiver->Text);
    }
    else
    {
        const DtNmosFlow* Flow = &Activation->Flow;
        printf("receiver %s: receive %s %s:%u from %s, sender %s\n", Receiver->Text,
               DtNmosMedia_Name(Flow->Media), Flow->DestinationIp,
               (unsigned)Flow->DestinationPort,
               Flow->SourceIp[0] != '\0' ? Flow->SourceIp : "any source",
               Activation->SenderId.Text[0] != '\0' ? Activation->SenderId.Text : "-");
    }
    fflush(stdout);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ActivateSender(void* User, const DtNmosId* Sender,
                                   const DtNmosSenderActivation* Activation)
{
    (void)User;
    if (Activation->MasterEnable)
    {
        printf("sender %s: send to %s:%u\n", Sender->Text, Activation->DestinationIp,
               (unsigned)Activation->DestinationPort);
    }
    else
    {
        printf("sender %s: stop sending\n", Sender->Text);
    }
    fflush(stdout);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DefaultFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A flow of 1080p25, 10-bit 4:2:2, to a multicast group.
//
static void DefaultFlow(DtNmosFlow* Flow)
{
    memset(Flow, 0, sizeof(*Flow));
    Flow->Size = sizeof(*Flow);
    Flow->Media = DTNMOS_MEDIA_VIDEO;
    snprintf(Flow->DestinationIp, sizeof(Flow->DestinationIp), "239.100.1.1");
    Flow->DestinationPort = 5004;
    Flow->PayloadType = 96;
    Flow->ClockRate = 90000;
    Flow->RefClock.Kind = DTNMOS_REFCLOCK_LOCALMAC;
    snprintf(Flow->RefClock.LocalMac, sizeof(Flow->RefClock.LocalMac),
             "00-00-00-00-00-00");
    Flow->MediaClockDirect = 1;
    DtNmosVideoFormat* Video = &Flow->Format.Video;
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
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds the device, the sender of Flow and a video receiver, with IDs that follow from
// the ID of the node. Returns EXAMPLE_OK, or EXAMPLE_FAILED having printed why.
//
static int AddAll(DtNmosNode* Node, const DtNmosId* NodeId, const char* Label,
                  const DtNmosFlow* Flow)
{
    char Name[256];
    DtNmosDeviceConfig Device;
    memset(&Device, 0, sizeof(Device));
    Device.Size = sizeof(Device);
    DtNmosId_FromName(NodeId, "device", &Device.Id);
    snprintf(Name, sizeof(Name), "%s device", Label);
    Device.Label = Name;
    DtNmosResult Result = DtNmosNode_AddDevice(Node, &Device);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosNode_AddDevice", Result);
    }

    DtNmosSenderConfig Sender;
    memset(&Sender, 0, sizeof(Sender));
    Sender.Size = sizeof(Sender);
    DtNmosId_FromName(NodeId, "sender", &Sender.Id);
    Sender.DeviceId = Device.Id;
    snprintf(Name, sizeof(Name), "%s sender", Label);
    Sender.Label = Name;
    Sender.Flow = Flow;
    Result = DtNmosNode_AddSender(Node, &Sender, ActivateSender, NULL);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosNode_AddSender", Result);
    }

    DtNmosReceiverConfig Receiver;
    memset(&Receiver, 0, sizeof(Receiver));
    Receiver.Size = sizeof(Receiver);
    DtNmosId_FromName(NodeId, "receiver", &Receiver.Id);
    Receiver.DeviceId = Device.Id;
    snprintf(Name, sizeof(Name), "%s receiver", Label);
    Receiver.Label = Name;
    Receiver.Media = DTNMOS_MEDIA_VIDEO;
    Result = DtNmosNode_AddReceiver(Node, &Receiver, ActivateReceiver, NULL);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosNode_AddReceiver", Result);
    }
    printf("sender %s, receiver %s\n", Sender.Id.Text, Receiver.Id.Text);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// With the server, the node polls itself on a thread of its own, and the program only
// waits; without it, the program polls, when the node asks to be.
//
static int Run(DtNmosNode* Node, int64_t Seconds)
{
    const bool Serves = DtNmos_HasServer() != 0;
    if (Serves)
    {
        const DtNmosResult Result = DtNmosNode_Serve(Node);
        if (Result != DTNMOS_OK)
        {
            return Example_Failed("DtNmosNode_Serve", Result);
        }
        char Url[256];
        size_t Size = sizeof(Url);
        if (DtNmosNode_ApiUrl(Node, Url, &Size) == DTNMOS_OK)
        {
            printf("serves at %s\n", Url);
        }
    }
    else
    {
        printf("dtnmos was built without its server: no Connection API is served\n");
    }
    fflush(stdout);
    // The registry holds the node once it has registered. A change, an activation among
    // them, makes the registration pending again until the next poll, which is not news.
    bool Registered = false;
    for (int64_t Elapsed = 0; Elapsed < Seconds * 1000;)
    {
        uint32_t NextMs = 1000;
        if (!Serves)
        {
            const DtNmosResult Result = DtNmosNode_Poll(Node, &NextMs);
            if (Result != DTNMOS_OK)
            {
                Example_Failed("DtNmosNode_Poll", Result);
            }
        }
        if (!Registered && DtNmosNode_IsRegistered(Node))
        {
            Registered = true;
            printf("registered\n");
            fflush(stdout);
        }
        const int Wait = NextMs > 1000 ? 1000 : (int)NextMs;
        Example_SleepMs(Wait);
        Elapsed += Wait > 0 ? Wait : 1;
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Seconds = 60;
    int64_t Port = 0;
    if (!Example_CheckArguments(
            Argc, Argv,
            "Registers a node with a sender and a receiver, and prints "
            "what a controller activates on them.",
            Options, (int)(sizeof(Options) / sizeof(Options[0]))) ||
        !Example_Int64(Argc, Argv, "--seconds", &Seconds) ||
        !Example_Int64(Argc, Argv, "--port", &Port))
    {
        return EXAMPLE_FAILED;
    }
    const char* Label = Example_Value(Argc, Argv, "--label");
    Label = Label != NULL ? Label : "dtnmos example";

    // The flow the sender sends: the first of an SDP file, or a flow of its own. A flow
    // the node is given is copied, so the SDP can be freed once the sender is added.
    DtNmosFlow Flow;
    DefaultFlow(&Flow);
    DtNmosSdp* Sdp = NULL;
    const char* SdpPath = Example_Value(Argc, Argv, "--sdp");
    if (SdpPath != NULL)
    {
        char* Text = NULL;
        size_t Length = 0;
        if (!Example_ReadFile(SdpPath, &Text, &Length))
        {
            return EXAMPLE_FAILED;
        }
        const DtNmosResult Parsed = DtNmosSdp_Parse(Text, Length, &Sdp);
        free(Text);
        if (Parsed != DTNMOS_OK)
        {
            return Example_Failed("DtNmosSdp_Parse", Parsed);
        }
        Flow = *DtNmosSdp_Flow(Sdp, 0);
    }

    char Url[512];
    const char* Given = Example_Value(Argc, Argv, "--registry");
    if (Given != NULL)
    {
        snprintf(Url, sizeof(Url), "%s", Given);
    }
    else
    {
        const int Found =
            Example_FindRegistry(DTNMOS_SERVICE_REGISTRATION, Url, sizeof(Url));
        if (Found != EXAMPLE_OK)
        {
            DtNmosSdp_Free(Sdp);
            return Found;
        }
    }

    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    DtNmosId_FromName(&Namespace, Label, &Config.Id);
    Config.Label = Label;
    Config.ApiPort = (uint16_t)Port;
    Config.RegistrationUrl = Url;
    Config.Http = DtNmos_CurlHttp;
    Config.Log = Example_Log;
    Config.LogUser = Example_HasFlag(Argc, Argv, "--verbose") ? (void*)Url : NULL;
    DtNmosNode* Node = DtNmosNode_Alloc();
    DtNmosResult Result = Node == NULL ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
    if (Result == DTNMOS_OK)
    {
        Result = DtNmosNode_Open(Node, &Config);
    }
    if (Result != DTNMOS_OK)
    {
        DtNmosNode_Freep(&Node);
        DtNmosSdp_Free(Sdp);
        return Example_Failed("DtNmosNode_Open", Result);
    }
    printf("node \"%s\" %s, registering with %s\n", Label, Config.Id.Text, Url);
    int Exit = AddAll(Node, &Config.Id, Label, &Flow);
    DtNmosSdp_Free(Sdp);
    if (Exit == EXAMPLE_OK)
    {
        Exit = Run(Node, Seconds);
    }
    // Freeing the node closes it, which deletes what it registered from the registry.
    DtNmosNode_Freep(&Node);
    printf("unregistered\n");
    return Exit;
}
