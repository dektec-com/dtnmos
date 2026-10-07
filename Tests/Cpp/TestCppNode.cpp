// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestCppNode.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API of a node
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "NmosTest.h"
#include "dtnmos_node.hpp"

#define NODE_ID "cccccccc-0000-4000-8000-000000000001"
#define DEVICE_ID "cccccccc-0000-4000-8000-000000000002"
#define SENDER_ID "cccccccc-0000-4000-8000-000000000003"
#define RECEIVER_ID "cccccccc-0000-4000-8000-000000000004"

#define CONNECTION "/x-nmos/connection/v1.1/single/"

// A registry that accepts every request, and records each one as "<method> <URL>". A
// request to a URL that holds Unreachable gets no answer.
struct FakeRegistry
{
    std::mutex Mutex;
    std::vector<std::string> Requests;
    std::string Unreachable;

    // Returns the HttpFunction of the registry. The function keeps a pointer to it.
    DtNmos::HttpFunction Http()
    {
        return [this](const DtNmos::HttpRequest& Request)
                   -> DtNmos::Expected<DtNmos::HttpResponse>
        {
            const std::lock_guard<std::mutex> Lock(Mutex);
            Requests.push_back(Request.Method + " " + Request.Url);
            if (!Unreachable.empty() &&
                Request.Url.find(Unreachable) != std::string::npos)
            {
                return std::unexpected(
                    DtNmos::Error{DtNmos::Result::Http, "unreachable"});
            }
            const bool Posts = Request.Method == "POST";
            return DtNmos::HttpResponse{Request.Method == "DELETE" ? 204
                                        : Posts                    ? 201
                                                                   : 200,
                                        {},
                                        "",
                                        ""};
        };
    }

    // Returns how many requests went to a URL that holds Text.
    int Count(const std::string& Text)
    {
        const std::lock_guard<std::mutex> Lock(Mutex);
        int Found = 0;
        for (const std::string& Request : Requests)
        {
            Found += Request.find(Text) != std::string::npos ? 1 : 0;
        }
        return Found;
    }
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ids -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmos::Id IdOf(const char* Text)
{
    return *DtNmos::Id::FromText(Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- VideoFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns a flow of 1280x720p50 10-bit 4:2:2 video to 239.0.0.1:5004.
//
static DtNmos::Flow VideoFlow()
{
    DtNmos::Flow Flow;
    Flow.DestinationIp = "239.0.0.1";
    Flow.DestinationPort = 5004;
    Flow.PayloadType = 96;
    Flow.ClockRate = 90000;
    DtNmos::VideoFormat Video;
    Video.Width = 1280;
    Video.Height = 720;
    Video.RateNumerator = 50;
    Video.RateDenominator = 1;
    Video.Depth = 10;
    Video.Sampling = DtNmos::Sampling::YCbCr422;
    Flow.Format = Video;
    return Flow;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Opens a node at 192.168.1.5:8080, which registers with Registry at registry.test,
// and adds a device to it. Changes, when given, changes the config before the node is
// opened.
//
static DtNmos::Expected<DtNmos::Node>
OpenNode(FakeRegistry& Registry, void (*Changes)(DtNmos::NodeConfig&) = nullptr)
{
    DtNmos::NodeConfig Config;
    Config.Id = IdOf(NODE_ID);
    Config.Label = "C++ node";
    Config.ApiHost = "192.168.1.5";
    Config.ApiPort = 8080;
    Config.RegistrationUrl = "http://registry.test";
    Config.Http = Registry.Http();
    if (Changes != nullptr)
    {
        Changes(Config);
    }
    auto Node = DtNmos::Node::Open(Config);
    if (Node)
    {
        const DtNmos::Status Added = Node->AddDevice(
            {.Id = IdOf(DEVICE_ID), .Label = "a card", .Description = ""});
        if (!Added)
        {
            return std::unexpected(Added.error());
        }
    }
    return Node;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SenderOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the config of the video sender on the device, which sends from 192.168.1.5.
//
static DtNmos::SenderConfig SenderOf()
{
    return {.Id = IdOf(SENDER_ID),
            .DeviceId = IdOf(DEVICE_ID),
            .Label = "camera",
            .Description = "",
            .Flow = VideoFlow(),
            .SourceIp = "192.168.1.5",
            .ActivationLeadMs = 0};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiverOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the config of an audio receiver on the device, which receives on 192.168.1.5.
//
static DtNmos::ReceiverConfig ReceiverOf()
{
    DtNmos::ReceiverConfig Receiver;
    Receiver.Id = IdOf(RECEIVER_ID);
    Receiver.DeviceId = IdOf(DEVICE_ID);
    Receiver.Label = "monitor";
    Receiver.Media = DtNmos::Media::Audio;
    Receiver.InterfaceIp = "192.168.1.5";
    return Receiver;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Patch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends a PATCH with Body to the staged parameters at Path in the Connection API, and
// returns the answer. Path is e.g. "senders/<ID>".
//
static DtNmos::HttpResponse Patch(DtNmos::Node& Node, const std::string& Path,
                                  const std::string& Body)
{
    const auto Answer = Node.Handle(
        {"PATCH", CONNECTION + Path + "/staged", "application/json", Body, 0});
    return Answer.value_or(DtNmos::HttpResponse{});
}

static const char* const Enable =
    "{\"master_enable\": true, \"activation\": {\"mode\": \"activate_immediate\"}}";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeOpens -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A node without an HTTP function is refused. A node with one has the ID, port and URL
// of its config. Its first poll registers the node, the device, the sender and the
// receiver: at least 6 POSTs reach the registry through the program's HTTP function.
//
NMOS_TEST(CppNodeOpens)
{
    FakeRegistry Registry;
    const auto Refused =
        OpenNode(Registry, [](DtNmos::NodeConfig& Config) { Config.Http = {}; });
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::InvalidArgument);

    auto Node = OpenNode(Registry);
    NMOS_ASSERT(Node.has_value());
    NMOS_ASSERT(Node->GetId() == IdOf(NODE_ID));
    NMOS_ASSERT_EQ(Node->ApiPort(), 8080);
    NMOS_ASSERT(Node->ApiUrl().value_or("") == "http://192.168.1.5:8080");
    NMOS_ASSERT(Node->AddSender(SenderOf(), {}).has_value());
    NMOS_ASSERT(Node->AddReceiver(ReceiverOf(), {}).has_value());
    NMOS_ASSERT(!Node->AddDevice({.Id = IdOf(DEVICE_ID), .Label = "", .Description = ""})
                     .has_value());
    NMOS_ASSERT(!Node->IsRegistered());
    const auto Next = Node->Poll();
    NMOS_ASSERT(Next.has_value());
    NMOS_ASSERT(Node->IsRegistered());
    NMOS_ASSERT(Registry.Count(
                    "POST http://registry.test/x-nmos/registration/v1.3/resource") >= 6);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeActivates -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A controller activates the sender, to 239.0.0.9:5010. The sender's function gets the
// sender's ID, that destination and the source 192.168.1.5. A controller then activates
// the receiver without a transport file. The receiver's function gets no flow: HasFlow
// is false, and the flow is of the receiver's media, audio, to 192.168.1.5.
//
NMOS_TEST(CppNodeActivates)
{
    FakeRegistry Registry;
    auto Node = OpenNode(Registry);
    NMOS_ASSERT(Node.has_value());
    std::optional<DtNmos::SenderActivation> Sent;
    DtNmos::Id SentBy;
    NMOS_ASSERT(Node->AddSender(SenderOf(),
                                [&](const DtNmos::Id& Sender,
                                    const DtNmos::SenderActivation& Activation)
                                {
                                    SentBy = Sender;
                                    Sent = Activation;
                                    return DtNmos::Status();
                                })
                    .has_value());
    std::optional<DtNmos::ReceiverActivation> Received;
    NMOS_ASSERT(Node->AddReceiver(ReceiverOf(),
                                  [&](const DtNmos::Id&,
                                      const DtNmos::ReceiverActivation& Activation)
                                  {
                                      Received = Activation;
                                      return DtNmos::Status();
                                  })
                    .has_value());

    const DtNmos::HttpResponse Answer =
        Patch(*Node, "senders/" SENDER_ID,
              "{\"master_enable\": true, \"transport_params\": [{\"destination_ip\": "
              "\"239.0.0.9\", \"destination_port\": 5010}], \"activation\": {\"mode\": "
              "\"activate_immediate\"}}");
    NMOS_ASSERT_EQ(Answer.Status, 200);
    NMOS_ASSERT(Sent.has_value());
    NMOS_ASSERT(SentBy == IdOf(SENDER_ID));
    NMOS_ASSERT(Sent->MasterEnable);
    NMOS_ASSERT(Sent->DestinationIp == "239.0.0.9");
    NMOS_ASSERT_EQ(Sent->DestinationPort, 5010);
    NMOS_ASSERT(Sent->SourceIp == "192.168.1.5");
    NMOS_ASSERT(Sent->AtNs > 0);

    NMOS_ASSERT_EQ(Patch(*Node, "receivers/" RECEIVER_ID, Enable).Status, 200);
    NMOS_ASSERT(Received.has_value());
    NMOS_ASSERT(Received->MasterEnable);
    NMOS_ASSERT(!Received->HasFlow);
    NMOS_ASSERT(Received->Flow.GetMedia() == DtNmos::Media::Audio);
    NMOS_ASSERT(Received->Flow.DestinationIp == "192.168.1.5");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeFailsAnActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A function that returns an Error fails the activation: the controller gets 500 and
// the Error's message, and the node goes on polling. With exceptions, a function that
// throws fails it the same way, with the exception's what().
//
NMOS_TEST(CppNodeFailsAnActivation)
{
    FakeRegistry Registry;
    auto Node = OpenNode(Registry);
    NMOS_ASSERT(Node.has_value());
    NMOS_ASSERT(Node->AddSender(SenderOf(),
                                [](const DtNmos::Id&,
                                   const DtNmos::SenderActivation&) -> DtNmos::Status {
                                    return std::unexpected(DtNmos::Error{
                                        DtNmos::Result::State, "the card refused it"});
                                })
                    .has_value());
    const DtNmos::HttpResponse Refused = Patch(*Node, "senders/" SENDER_ID, Enable);
    NMOS_ASSERT_EQ(Refused.Status, 500);
    NMOS_ASSERT(Refused.Body.find("the card refused it") != std::string::npos);
#if defined(__cpp_exceptions)
    NMOS_ASSERT(Node->AddReceiver(ReceiverOf(),
                                  [](const DtNmos::Id&,
                                     const DtNmos::ReceiverActivation&) -> DtNmos::Status
                                  { throw std::runtime_error("no such port"); })
                    .has_value());
    const DtNmos::HttpResponse Thrown = Patch(*Node, "receivers/" RECEIVER_ID, Enable);
    NMOS_ASSERT_EQ(Thrown.Status, 500);
    NMOS_ASSERT(Thrown.Body.find("no such port") != std::string::npos);
#endif
    NMOS_ASSERT(Node->Poll().has_value());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeRemoves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Removing a sender or receiver frees its function, and what the function captured. The
// two functions share a token, so its use count is 3. Removing the sender makes it 2,
// and removing the device, which removes the receiver, makes it 1. A function that
// removes its own sender gets success; the controller gets 404, and the function is
// freed once it has returned.
//
NMOS_TEST(CppNodeRemoves)
{
    FakeRegistry Registry;
    auto Node = OpenNode(Registry);
    NMOS_ASSERT(Node.has_value());
    const auto Token = std::make_shared<int>(1);
    NMOS_ASSERT(Node->AddSender(SenderOf(), [Token](const DtNmos::Id&,
                                                    const DtNmos::SenderActivation&)
                                { return DtNmos::Status(); })
                    .has_value());
    NMOS_ASSERT(Node->AddReceiver(ReceiverOf(), [Token](const DtNmos::Id&,
                                                        const DtNmos::ReceiverActivation&)
                                  { return DtNmos::Status(); })
                    .has_value());
    NMOS_ASSERT_EQ(Token.use_count(), 3);
    NMOS_ASSERT(Node->Remove(IdOf(SENDER_ID)).has_value());
    NMOS_ASSERT_EQ(Token.use_count(), 2);
    NMOS_ASSERT(Node->Remove(IdOf(DEVICE_ID)).has_value());
    NMOS_ASSERT_EQ(Token.use_count(), 1);
    NMOS_ASSERT(Node->Remove(IdOf(SENDER_ID)).error().Code == DtNmos::Result::NotFound);

    NMOS_ASSERT(
        Node->AddDevice({.Id = IdOf(DEVICE_ID), .Label = "a card", .Description = ""})
            .has_value());
    DtNmos::Node* Self = &*Node;
    bool Removed = false;
    NMOS_ASSERT(Node->AddSender(SenderOf(),
                                [Self, Token, &Removed](const DtNmos::Id& Sender,
                                                        const DtNmos::SenderActivation&)
                                {
                                    Removed = Self->Remove(Sender).has_value();
                                    return DtNmos::Status();
                                })
                    .has_value());
    NMOS_ASSERT_EQ(Patch(*Node, "senders/" SENDER_ID, Enable).Status, 404);
    NMOS_ASSERT(Removed);
    NMOS_ASSERT_EQ(Token.use_count(), 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeRemovesWhileActivated -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Removing the sender while a controller's thread activates it returns at once. The
// function keeps its token while it runs: the use count stays 2. When the activation
// returns, the function is freed and the count is 1, and the controller gets 404.
//
NMOS_TEST(CppNodeRemovesWhileActivated)
{
    FakeRegistry Registry;
    auto Node = OpenNode(Registry);
    NMOS_ASSERT(Node.has_value());
    const auto Token = std::make_shared<int>(1);
    std::atomic<bool> Entered = false;
    std::atomic<bool> Go = false;
    NMOS_ASSERT(
        Node->AddSender(
                SenderOf(),
                [Token, &Entered, &Go](const DtNmos::Id&, const DtNmos::SenderActivation&)
                {
                    Entered = true;
                    for (int Wait = 0; Wait < 2000 && !Go; ++Wait)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    return DtNmos::Status();
                })
            .has_value());
    int Answered = 0;
    std::thread Controller(
        [&] { Answered = Patch(*Node, "senders/" SENDER_ID, Enable).Status; });
    for (int Wait = 0; Wait < 2000 && !Entered; ++Wait)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NMOS_EXPECT(Entered);
    NMOS_EXPECT(Node->Remove(IdOf(SENDER_ID)).has_value());
    NMOS_EXPECT(Token.use_count() == 2);
    Go = true;
    Controller.join();
    NMOS_ASSERT_EQ(Answered, 404);
    NMOS_ASSERT_EQ(Token.use_count(), 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeMoves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A node that is moved keeps its sender and the sender's function: a PATCH reaches the
// function. Moving a node onto another closes the other first, which unregisters it.
// The node that was moved from fails with Result::State, and has no ID.
//
NMOS_TEST(CppNodeMoves)
{
    // The registries outlive the nodes, which unregister through them when they close.
    FakeRegistry Registry;
    FakeRegistry Other;
    auto Opened = OpenNode(Registry);
    NMOS_ASSERT(Opened.has_value());
    int Calls = 0;
    NMOS_ASSERT(Opened
                    ->AddSender(SenderOf(),
                                [&](const DtNmos::Id&, const DtNmos::SenderActivation&)
                                {
                                    ++Calls;
                                    return DtNmos::Status();
                                })
                    .has_value());
    NMOS_ASSERT(Opened->Poll().has_value());
    DtNmos::Node Moved = std::move(*Opened);
    NMOS_ASSERT_EQ(Patch(Moved, "senders/" SENDER_ID, Enable).Status, 200);
    NMOS_ASSERT_EQ(Calls, 1);
    NMOS_ASSERT(Opened->Poll().error().Code == DtNmos::Result::State);
    NMOS_ASSERT(Opened->GetId().IsEmpty());

    auto Second = OpenNode(Other);
    NMOS_ASSERT(Second.has_value());
    NMOS_ASSERT(Second->Poll().has_value());
    Moved = std::move(*Second);
    NMOS_ASSERT(Registry.Count("DELETE http://registry.test/x-nmos/registration/v1.3/"
                               "resource/nodes/" NODE_ID) == 1);
    NMOS_ASSERT_EQ(Patch(Moved, "senders/" SENDER_ID, Enable).Status, 404);
    NMOS_ASSERT_EQ(Calls, 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeUsesASearch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A node given a fed search borrows it, and registers with the registry that the
// search was fed. That still works after the search was moved to another object. Once
// the node is closed, the search may be destroyed before the node.
//
NMOS_TEST(CppNodeUsesASearch)
{
    FakeRegistry Registry;
    auto Opened = DtNmos::RegistrySearch::Open(
        {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = true});
    NMOS_ASSERT(Opened.has_value());
    NMOS_ASSERT(
        Opened->Feed(DtNmos::Service::Registration, {"http://fed.test"}).has_value());
    auto Search = std::make_unique<DtNmos::RegistrySearch>(std::move(*Opened));
    static const DtNmos::RegistrySearch* Given = nullptr;
    Given = Search.get();
    auto Node = OpenNode(Registry,
                         [](DtNmos::NodeConfig& Config)
                         {
                             Config.RegistrationUrl.clear();
                             Config.Search = Given;
                         });
    NMOS_ASSERT(Node.has_value());
    auto Moved = std::make_unique<DtNmos::RegistrySearch>(std::move(*Search));
    NMOS_ASSERT(Node->Poll().has_value());
    NMOS_ASSERT(Node->IsRegistered());
    NMOS_ASSERT(Registry.Count("http://fed.test/") > 0);
    NMOS_ASSERT(Node->Close().has_value());
    Moved.reset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeMovesToAnotherRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A node whose registry does not answer asks its RegistryFailed function, which names
// another registry. The node then registers with that one.
//
NMOS_TEST(CppNodeMovesToAnotherRegistry)
{
    FakeRegistry Registry;
    Registry.Unreachable = "registry.test";
    auto Node = OpenNode(Registry,
                         [](DtNmos::NodeConfig& Config)
                         {
                             Config.FailuresBeforeSwitch = 1;
                             Config.RegistryFailed = [](uint32_t)
                             { return std::optional<std::string>("http://next.test"); };
                         });
    NMOS_ASSERT(Node.has_value());
    NMOS_ASSERT(!Node->Poll().has_value());
    for (int Poll = 0; Poll < 3 && !Node->IsRegistered(); ++Poll)
    {
        (void)Node->Poll();
    }
    NMOS_ASSERT(Node->IsRegistered());
    NMOS_ASSERT(Registry.Count("http://next.test/") > 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeChangesItsClockAndFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A PTP clock converts to the C API and back unchanged, and the node takes it. A PTP
// clock whose grandmaster is no EUI-64 is refused. The sender takes a video flow to
// another group, and refuses an audio flow.
//
NMOS_TEST(CppNodeChangesItsClockAndFlow)
{
    const DtNmos::Clock Ptp{DtNmos::ClockKind::Ptp, "00-1B-19-FF-FE-00-00-01", true,
                            true};
    const auto Native = DtNmos::Detail::ToNative(Ptp);
    NMOS_ASSERT(Native.has_value());
    NMOS_ASSERT(DtNmos::Detail::FromNative(*Native) == Ptp);

    FakeRegistry Registry;
    auto Node = OpenNode(Registry,
                         [](DtNmos::NodeConfig& Config)
                         {
                             Config.Clock = DtNmos::Clock();
                             Config.Clock->Kind = DtNmos::ClockKind::Internal;
                         });
    NMOS_ASSERT(Node.has_value());
    NMOS_ASSERT(Node->SetClock(Ptp).has_value());
    DtNmos::Clock Wrong = Ptp;
    Wrong.Grandmaster = "no EUI-64";
    const auto Refused = Node->SetClock(Wrong);
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::InvalidArgument);

    NMOS_ASSERT(Node->AddSender(SenderOf(), {}).has_value());
    DtNmos::Flow Moved = VideoFlow();
    Moved.DestinationIp = "239.0.0.2";
    NMOS_ASSERT(Node->UpdateSender(IdOf(SENDER_ID), Moved).has_value());
    DtNmos::Flow Audio = VideoFlow();
    DtNmos::AudioFormat L24;
    L24.Encoding = DtNmos::AudioEncoding::L24;
    L24.SampleRate = 48000;
    L24.Channels = 2;
    L24.PacketTimeNs = 1000000;
    Audio.Format = L24;
    NMOS_ASSERT(!Node->UpdateSender(IdOf(SENDER_ID), Audio).has_value());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- CppNodeIsDestroyedWhileActivated -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A node that serves is destroyed while a controller's request activates its sender.
// The destruction waits for the server's threads, so the function finishes, and sets
// Finished, on what it captured. The test runs only with libcurl and the server.
//
NMOS_TEST(CppNodeIsDestroyedWhileActivated)
{
    if (!DtNmos::HasServer() || !DtNmos::HasCurl())
    {
        return;
    }
    FakeRegistry Registry;
    auto Node = OpenNode(Registry,
                         [](DtNmos::NodeConfig& Config)
                         {
                             Config.ApiHost = "127.0.0.1";
                             Config.ApiPort = 0;
                         });
    NMOS_ASSERT(Node.has_value());
    std::atomic<bool> Entered = false;
    auto Finished = std::make_shared<std::atomic<bool>>(false);
    NMOS_ASSERT(
        Node->AddSender(
                SenderOf(),
                [&Entered, Finished](const DtNmos::Id&, const DtNmos::SenderActivation&)
                {
                    Entered = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    *Finished = true;
                    return DtNmos::Status();
                })
            .has_value());
    NMOS_ASSERT(Node->Serve().has_value());
    const std::string Url =
        Node->ApiUrl().value_or("") + CONNECTION "senders/" SENDER_ID "/staged";
    std::thread Controller(
        [&]
        { (void)DtNmos::CurlHttp({"PATCH", Url, "application/json", Enable, 5000}); });
    for (int Wait = 0; Wait < 2000 && !Entered; ++Wait)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NMOS_EXPECT(Entered);
    Node = std::unexpected(DtNmos::Error{});
    NMOS_EXPECT(*Finished);
    Controller.join();
}

NMOS_TEST_MAIN("CppNode", NMOS_RUN(CppNodeOpens), NMOS_RUN(CppNodeActivates),
               NMOS_RUN(CppNodeFailsAnActivation), NMOS_RUN(CppNodeRemoves),
               NMOS_RUN(CppNodeRemovesWhileActivated), NMOS_RUN(CppNodeMoves),
               NMOS_RUN(CppNodeUsesASearch), NMOS_RUN(CppNodeMovesToAnotherRegistry),
               NMOS_RUN(CppNodeChangesItsClockAndFlow),
               NMOS_RUN(CppNodeIsDestroyedWhileActivated))
