// #*#*#*#*#*#*#*#*#*#*#*# DtNmosRegisterNode.cpp *#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Example: DtNmosRegisterNode.c written with the C++ API
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Does what DtNmosRegisterNode.c does, with the same options and output, through the C++
// API: runs an NMOS node for --seconds, registers it with a device, a video sender and a
// video receiver, and serves its Node API and Connection API (IS-05), so that a
// controller can connect them. The registry is the one at --registry, or one the node
// finds on the network with DNS-SD. Each activation a controller makes is printed; the
// program sends or receives nothing.
//
// Needs dtnmos built with libcurl and the server; without the server it registers the
// node but serves no Connection API. Exits with 0 when the time is over, and 1 when a
// call failed.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "Common/ExampleCommon.h"
#include "dtnmos_node.hpp"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption Options[] = {
    {"--registry", true, "The base URL of the registry, e.g. http://registry:8010"},
    {"--label", true, "The label of the node, from which its IDs follow"},
    {"--sdp", true, "An SDP file whose first flow the sender sends"},
    {"--address", true,
     "The address the sender sends from and the receiver receives on; that of the APIs "
     "of the node by default"},
    {"--port", true, "The port of the APIs of the node; any free one by default"},
    {"--seconds", true, "How long the node runs; 60 by default"},
    {"--verbose", false, "Prints what the node does"},
    {"--ptp", true,
     "The node's clock is that of this PTP grandmaster, an EUI-64 such as "
     "00-1b-19-ff-fe-00-00-01; internal by default"},
    {"--locked", false, "With --ptp: the node is locked to the grandmaster"},
    {"--traceable", false, "With --ptp: the grandmaster is traceable to TAI"},
};

// The namespace of this example's node IDs, the one of DtNmosRegisterNode.c, so that
// both give a node of the same label the same IDs.
static const char* const Namespace = "7c1d5a40-2b6e-4f39-9e0a-58d3b1c4e2f7";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Failed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints "What: RESULT_NAME: the message of the failure" for a failed call, as
// Example_Failed() does, and returns EXAMPLE_FAILED.
//
static int Failed(const char* What, const DtNmos::Error& Failure)
{
    std::printf("%s: %.*s: %s\n", What,
                static_cast<int>(DtNmos::Name(Failure.Code).size()),
                DtNmos::Name(Failure.Code).data(), Failure.Message.c_str());
    return EXAMPLE_FAILED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The receiver's function, called when a controller connects or disconnects it. A real
// program would set up its receiving here, and return an Error when it cannot; this one
// prints what it would receive.
//
static DtNmos::Status ActivateReceiver(const DtNmos::Id& Receiver,
                                       const DtNmos::ReceiverActivation& Activation)
{
    const std::string Id = Receiver.ToString();
    if (!Activation.MasterEnable)
    {
        std::printf("receiver %s: stop receiving\n", Id.c_str());
    }
    else if (!Activation.HasFlow)
    {
        std::printf("receiver %s: receive, but the controller gave no SDP\n", Id.c_str());
    }
    else
    {
        const DtNmos::Flow& Flow = Activation.Flow;
        const std::string_view Media = DtNmos::Name(Flow.GetMedia());
        const std::string Sender = Activation.SenderId.ToString();
        std::printf("receiver %s: receive %.*s %s:%u from %s, sender %s\n", Id.c_str(),
                    static_cast<int>(Media.size()), Media.data(),
                    Flow.DestinationIp.c_str(),
                    static_cast<unsigned>(Flow.DestinationPort),
                    Flow.SourceIp.empty() ? "any source" : Flow.SourceIp.c_str(),
                    Sender.empty() ? "-" : Sender.c_str());
    }
    std::fflush(stdout);
    return {};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The sender's function, called when a controller enables, disables or redirects it.
// Prints where it would send to.
//
static DtNmos::Status ActivateSender(const DtNmos::Id& Sender,
                                     const DtNmos::SenderActivation& Activation)
{
    const std::string Id = Sender.ToString();
    if (Activation.MasterEnable)
    {
        std::printf("sender %s: send to %s:%u\n", Id.c_str(),
                    Activation.DestinationIp.c_str(),
                    static_cast<unsigned>(Activation.DestinationPort));
    }
    else
    {
        std::printf("sender %s: stop sending\n", Id.c_str());
    }
    std::fflush(stdout);
    return {};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApiHost -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the host part of the node's API URL, its address, or "".
//
static std::string ApiHost(const DtNmos::Node& Node)
{
    const std::string Url = Node.ApiUrl().value_or("");
    const std::size_t Scheme = Url.find("://");
    std::string Host = Scheme != std::string::npos ? Url.substr(Scheme + 3) : Url;
    // An IPv6 address is between brackets, and the port follows the last colon.
    if (!Host.empty() && Host[0] == '[')
    {
        return Host.substr(1, Host.find(']') - 1);
    }
    return Host.substr(0, Host.rfind(':'));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DefaultFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the default stream: 1080p25, 10-bit 4:2:2, to a multicast group.
//
static DtNmos::Flow DefaultFlow()
{
    DtNmos::Flow Flow;
    Flow.DestinationIp = "239.100.1.1";
    Flow.DestinationPort = 5004;
    Flow.PayloadType = 96;
    Flow.ClockRate = 90000;
    Flow.RefClock.Kind = DtNmos::RefClockKind::LocalMac;
    Flow.RefClock.LocalMac = "00-00-00-00-00-00";
    Flow.MediaClockDirect = true;
    DtNmos::VideoFormat Video;
    Video.Width = 1920;
    Video.Height = 1080;
    Video.RateNumerator = 25;
    Video.RateDenominator = 1;
    Video.Depth = 10;
    Video.Sampling = DtNmos::Sampling::YCbCr422;
    Video.Colorimetry = DtNmos::Colorimetry::Bt709;
    Video.Tcs = DtNmos::Tcs::Sdr;
    Video.PackingMode = DtNmos::PackingMode::General;
    Video.Ssn = "ST2110-20:2017";
    Video.TransmitterType = DtNmos::TransmitterType::Narrow;
    Flow.Format = Video;
    return Flow;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds the device, a sender of Flow and a video receiver to the node. They send from and
// receive on Address, and their IDs are made from the node's ID. Returns EXAMPLE_OK, or
// EXAMPLE_FAILED after printing why.
//
static int AddAll(DtNmos::Node& Node, const std::string& Label, const DtNmos::Flow& Flow,
                  const std::string& Address)
{
    const DtNmos::Id NodeId = Node.GetId();
    const DtNmos::Id DeviceId =
        DtNmos::Id::FromName(NodeId, "device").value_or(DtNmos::Id());
    const DtNmos::Id SenderId =
        DtNmos::Id::FromName(NodeId, "sender").value_or(DtNmos::Id());
    const DtNmos::Id ReceiverId =
        DtNmos::Id::FromName(NodeId, "receiver").value_or(DtNmos::Id());
    DtNmos::Status Done =
        Node.AddDevice({.Id = DeviceId, .Label = Label + " device", .Description = ""});
    if (!Done)
    {
        return Failed("Node::AddDevice", Done.error());
    }
    Done = Node.AddSender({.Id = SenderId,
                           .DeviceId = DeviceId,
                           .Label = Label + " sender",
                           .Description = "",
                           .Flow = Flow,
                           .SourceIp = Address,
                           .ActivationLeadMs = 0},
                          ActivateSender);
    if (!Done)
    {
        return Failed("Node::AddSender", Done.error());
    }
    DtNmos::ReceiverConfig Receiver;
    Receiver.Id = ReceiverId;
    Receiver.DeviceId = DeviceId;
    Receiver.Label = Label + " receiver";
    Receiver.Media = DtNmos::Media::Video;
    Receiver.InterfaceIp = Address;
    Done = Node.AddReceiver(Receiver, ActivateReceiver);
    if (!Done)
    {
        return Failed("Node::AddReceiver", Done.error());
    }
    std::printf("sender %s, receiver %s\n", SenderId.ToString().c_str(),
                ReceiverId.ToString().c_str());
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Runs the node for Seconds: prints where it serves, and when it becomes registered. With
// the server, the node polls itself on a thread of its own and the program only waits;
// without it, the program polls the node when the node asks. Returns the program's exit
// code.
//
static int Run(DtNmos::Node& Node, int64_t Seconds)
{
    const bool Serves = DtNmos::HasServer();
    if (Serves)
    {
        const DtNmos::Status Served = Node.Serve();
        if (!Served)
        {
            return Failed("Node::Serve", Served.error());
        }
        std::printf("serves at %s\n", Node.ApiUrl().value_or("?").c_str());
    }
    else
    {
        std::printf("dtnmos was built without its server: no Connection API is served\n");
    }
    std::fflush(stdout);
    // The registry holds the node once it has registered. A change, an activation among
    // them, makes the registration pending again until the next poll, which is not news.
    bool Registered = false;
    for (int64_t Elapsed = 0; Elapsed < Seconds * 1000;)
    {
        std::chrono::milliseconds Next(1000);
        if (!Serves)
        {
            const auto Polled = Node.Poll();
            if (!Polled)
            {
                Failed("Node::Poll", Polled.error());
            }
            Next = Polled.value_or(Next);
        }
        if (!Registered && Node.IsRegistered())
        {
            Registered = true;
            std::printf("registered\n");
            std::fflush(stdout);
        }
        const int Wait = Next.count() > 1000 ? 1000 : static_cast<int>(Next.count());
        Example_SleepMs(Wait);
        Elapsed += Wait > 0 ? Wait : 1;
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the first flow of the SDP file at Path, or nothing after printing why.
//
static std::optional<DtNmos::Flow> ReadFlow(const char* Path)
{
    char* Text = nullptr;
    std::size_t Length = 0;
    if (!Example_ReadFile(Path, &Text, &Length))
    {
        return std::nullopt;
    }
    const auto Read = DtNmos::Sdp::Parse(std::string_view(Text, Length));
    std::free(Text);
    if (!Read)
    {
        Failed("Sdp::Parse", Read.error());
        return std::nullopt;
    }
    return Read->Flows.front();
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
            Options, static_cast<int>(std::size(Options))) ||
        !Example_Int64(Argc, Argv, "--seconds", &Seconds) ||
        !Example_Int64(Argc, Argv, "--port", &Port))
    {
        return EXAMPLE_FAILED;
    }
    const char* GivenLabel = Example_Value(Argc, Argv, "--label");
    const std::string Label = GivenLabel != nullptr ? GivenLabel : "dtnmos example";

    // The flow the sender sends: the first of an SDP file, or a flow of its own.
    DtNmos::Flow Flow = DefaultFlow();
    const char* SdpPath = Example_Value(Argc, Argv, "--sdp");
    if (SdpPath != nullptr)
    {
        const std::optional<DtNmos::Flow> Read = ReadFlow(SdpPath);
        if (!Read)
        {
            return EXAMPLE_FAILED;
        }
        Flow = *Read;
    }

    DtNmos::NodeConfig Config;
    Config.Id = DtNmos::Id::FromName(*DtNmos::Id::FromText(Namespace), Label)
                    .value_or(DtNmos::Id());
    Config.Label = Label;
    Config.ApiPort = static_cast<uint16_t>(Port);
    Config.Http = DtNmos::CurlHttp;
    const bool Verbose = Example_HasFlag(Argc, Argv, "--verbose");
    Config.Log = [Verbose, &Label](DtNmos::LogLevel Level, std::string_view Message)
    {
        Example_Log(Verbose ? const_cast<char*>(Label.c_str()) : nullptr,
                    static_cast<DtNmosLogLevel>(Level), std::string(Message).c_str());
    };
    const char* Grandmaster = Example_Value(Argc, Argv, "--ptp");
    Config.Clock = DtNmos::Clock();
    Config.Clock->Kind = DtNmos::ClockKind::Internal;
    if (Grandmaster != nullptr)
    {
        Config.Clock->Kind = DtNmos::ClockKind::Ptp;
        Config.Clock->Grandmaster = Grandmaster;
        Config.Clock->Locked = Example_HasFlag(Argc, Argv, "--locked");
        Config.Clock->Traceable = Example_HasFlag(Argc, Argv, "--traceable");
    }
    // Without --registry, the node takes the registries a search of the program finds
    // with DNS-SD; a program of more nodes shares one search among them. The node
    // borrows the search, so the program declares it before the node, which is then
    // destroyed first.
    const char* Url = Example_Value(Argc, Argv, "--registry");
    std::optional<DtNmos::RegistrySearch> Search;
    if (Url != nullptr)
    {
        Config.RegistrationUrl = Url;
    }
    else
    {
        auto Opened = DtNmos::RegistrySearch::Open(
            {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = false});
        if (!Opened)
        {
            return Failed("RegistrySearch::Open", Opened.error());
        }
        Search.emplace(std::move(*Opened));
        Config.Search = &*Search;
    }

    auto Node = DtNmos::Node::Open(Config);
    if (!Node)
    {
        return Failed("Node::Open", Node.error());
    }
    const std::string NodeId = Config.Id.ToString();
    if (Url != nullptr)
    {
        std::printf("node \"%s\" %s, registering with %s\n", Label.c_str(),
                    NodeId.c_str(), Url);
    }
    else
    {
        std::printf("node \"%s\" %s, searching for a registry with DNS-SD\n",
                    Label.c_str(), NodeId.c_str());
    }
    const char* GivenAddress = Example_Value(Argc, Argv, "--address");
    const std::string Address = GivenAddress != nullptr ? GivenAddress : ApiHost(*Node);
    int Exit = AddAll(*Node, Label, Flow, Address);
    if (Exit == EXAMPLE_OK)
    {
        Exit = Run(*Node, Seconds);
    }
    // Destroying the node closes it, which deletes what it registered from the registry;
    // the search it borrowed goes after it.
    Node = std::unexpected(DtNmos::Error{});
    std::printf("unregistered\n");
    return Exit;
}
