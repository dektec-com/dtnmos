// #*#*#*#*#*#*#*#*#*#*#*#*#* TestCppQuery.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API of a registry's clients, and of finding registries
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "NmosTest.h"
#include "dtnmos_query.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryFedSearch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A fed search holds the two registries it is fed, in that order, and both are usable.
// It refuses a service it does not look for. Moved to another object, it keeps what it
// was fed, and it can be fed again.
//
NMOS_TEST(CppQueryFedSearch)
{
    auto Opened = DtNmos::RegistrySearch::Open(
        {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = true});
    NMOS_ASSERT(Opened.has_value());
    auto Search = std::make_unique<DtNmos::RegistrySearch>(std::move(*Opened));
    NMOS_ASSERT(Search
                    ->Feed(DtNmos::Service::Registration,
                           {"http://registry-a.test:8010", "http://registry-b.test"})
                    .has_value());
    const auto Listed = Search->List(DtNmos::Service::Registration);
    NMOS_ASSERT(Listed.has_value());
    NMOS_ASSERT_EQ(Listed->size(), 2);
    NMOS_ASSERT((*Listed)[0].Url == "http://registry-a.test:8010");
    NMOS_ASSERT((*Listed)[0].Usable);
    NMOS_ASSERT((*Listed)[0].Service == DtNmos::Service::Registration);
    NMOS_ASSERT((*Listed)[1].Url == "http://registry-b.test");

    const auto Refused = Search->Feed(DtNmos::Service::Query, {"http://query.test"});
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::InvalidArgument);
    NMOS_ASSERT(!Search->List(DtNmos::Service::Query).has_value());

    DtNmos::RegistrySearch Moved = std::move(*Search);
    Search.reset();
    NMOS_ASSERT_EQ(Moved.List(DtNmos::Service::Registration)->size(), 2);
    NMOS_ASSERT(Moved.Feed(DtNmos::Service::Registration, {"http://registry-c.test"})
                    .has_value());
    const auto Kept = Moved.List(DtNmos::Service::Registration);
    NMOS_ASSERT(Kept.has_value() && Kept->size() == 1);
    NMOS_ASSERT((*Kept)[0].Url == "http://registry-c.test");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryRefusesASearch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A search that looks for nothing is refused. So is a search of the network whose
// interface address is no address.
//
NMOS_TEST(CppQueryRefusesASearch)
{
    const auto Nothing = DtNmos::RegistrySearch::Open(
        {.Finds = static_cast<DtNmos::Finds>(0), .Discovery = {}, .Fed = true});
    NMOS_ASSERT(!Nothing.has_value());
    NMOS_ASSERT(Nothing.error().Code == DtNmos::Result::InvalidArgument);

    DtNmos::DiscoveryConfig Config;
    Config.Service = DtNmos::Service::Query;
    Config.InterfaceAddress = "not an address";
    const auto Found = DtNmos::Discover(Config);
    NMOS_ASSERT(!Found.has_value());
    NMOS_ASSERT(Found.error().Code == DtNmos::Result::InvalidArgument);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryDiscovers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A search through multicast DNS alone, sent to a port on the loopback interface where
// nobody answers, finds nothing in 100 ms. Finding nothing is no failure. The search
// may call its log function only while it runs.
//
NMOS_TEST(CppQueryDiscovers)
{
    int Logged = 0;
    DtNmos::DiscoveryConfig Config;
    Config.Service = DtNmos::Service::Registration;
    Config.InterfaceAddress = "127.0.0.1";
    Config.Destination = "127.0.0.1:9";
    Config.TimeoutMs = 100;
    Config.Searches = DtNmos::Search::Multicast;
    Config.Log = [&](DtNmos::LogLevel, std::string_view) { ++Logged; };
    const auto Found = DtNmos::Discover(Config);
    NMOS_ASSERT(Found.has_value());
    NMOS_ASSERT(Found->empty());
    NMOS_ASSERT(Logged >= 0);
}

#define BASE "http://registry.test/x-nmos/query/v1.3/"
#define WS_HREF "ws://registry.test/x-nmos/query/v1.3/subscriptions/1/ws"
#define CAMERA_ID "11111111-1111-4111-8111-111111111111"
#define MONITOR_ID "44444444-4444-4444-8444-444444444444"
#define DEVICE_ID "55555555-5555-4555-8555-555555555555"
#define VIDEO_FLOW "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
#define STAGED                                                                           \
    "http://node.test/x-nmos/connection/v1.1/single/receivers/" MONITOR_ID "/staged"
#define CAMERA_SDP                                                                       \
    "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=camera 1\r\nt=0 0\r\n"                          \
    "m=video 5004 RTP/AVP 96\r\nc=IN IP4 239.0.0.1/64\r\na=rtpmap:96 raw/90000\r\n"
// The JSON that the registry gives: a video sender with an SDP, a video receiver on a
// device with a Connection API, and the sender's video flow.
#define CAMERA                                                                           \
    "{\"id\": \"" CAMERA_ID "\", \"label\": \"camera 1\", \"flow_id\": \"" VIDEO_FLOW    \
    "\", \"transport\": \"urn:x-nmos:transport:rtp.mcast\", "                            \
    "\"manifest_href\": \"http://camera.test/video.sdp\"}"
#define MONITOR                                                                          \
    "{\"id\": \"" MONITOR_ID "\", \"label\": \"monitor\", \"device_id\": \"" DEVICE_ID   \
    "\", \"format\": \"urn:x-nmos:format:video\", \"transport\": "                       \
    "\"urn:x-nmos:transport:rtp\", \"caps\": {\"media_types\": [\"video/raw\"]}, "       \
    "\"subscription\": {\"sender_id\": null, \"active\": false}}"
#define VIDEO                                                                            \
    "{\"id\": \"" VIDEO_FLOW "\", \"format\": \"urn:x-nmos:format:video\", "             \
    "\"media_type\": \"video/raw\"}"
// Makes a grain of the Query API that carries the changes in data.
#define GRAIN(data)                                                                      \
    "{\"grain_type\": \"event\", \"source_id\": \"x\", \"flow_id\": \"y\", "             \
    "\"grain\": {\"type\": \"urn:x-nmos:format:data.event\", \"topic\": \"/senders/\", " \
    "\"data\": [" data "]}}"

// A registry and the node of the monitor, faked together. They answer the requests
// they know, and record each request as "<method> <URL>".
struct FakeNetwork
{
    std::vector<std::string> Requests;

    // Returns the HttpFunction of the network, which keeps a pointer to it.
    DtNmos::HttpFunction Http()
    {
        return [this](const DtNmos::HttpRequest& Request)
                   -> DtNmos::Expected<DtNmos::HttpResponse>
        {
            Requests.push_back(Request.Method + " " + Request.Url);
            static const std::map<std::string, std::string> Routes = {
                {BASE "senders?paging.limit=100", "[" CAMERA "]"},
                {BASE "senders?label=camera%201&paging.limit=100", "[" CAMERA "]"},
                {BASE "senders?label=nobody&paging.limit=100", "[]"},
                {BASE "flows?paging.limit=100", "[" VIDEO "]"},
                {BASE "flows/" VIDEO_FLOW, VIDEO},
                {BASE "receivers?paging.limit=100", "[" MONITOR "]"},
                {BASE "receivers?label=monitor&paging.limit=100", "[" MONITOR "]"},
                {BASE "receivers/" MONITOR_ID, MONITOR},
                {BASE "devices/" DEVICE_ID,
                 "{\"id\": \"" DEVICE_ID "\", \"controls\": [{\"href\": "
                 "\"http://node.test/x-nmos/connection/v1.1\", \"type\": "
                 "\"urn:x-nmos:control:sr-ctrl/v1.1\"}]}"},
                {"http://camera.test/video.sdp", CAMERA_SDP},
            };
            if (Request.Method == "PATCH")
            {
                return DtNmos::HttpResponse{200, {}, "application/json", "{}"};
            }
            if (Request.Method == "POST" && Request.Url == BASE "subscriptions")
            {
                return DtNmos::HttpResponse{201,
                                            {},
                                            "application/json",
                                            "{\"id\": \"1\", \"ws_href\": \"" WS_HREF
                                            "\"}"};
            }
            const auto Route = Routes.find(Request.Url);
            if (Route == Routes.end())
            {
                return DtNmos::HttpResponse{404, {}, "", ""};
            }
            return DtNmos::HttpResponse{200, {}, "application/json", Route->second};
        };
    }
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenQuery -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmos::Expected<DtNmos::Query> OpenQuery(FakeNetwork& Network)
{
    DtNmos::QueryConfig Config;
    Config.RegistryUrl = "http://registry.test";
    Config.Http = Network.Http();
    Config.TimeoutMs = 2000;
    return DtNmos::Query::Open(Config);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryAsksTheRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A query lists the camera with the media of its flow. It finds the camera by its
// label, and downloads and parses its SDP. It lists the monitor and finds it by its ID.
// A label that nobody has is not found. A query without an HTTP function is refused,
// and a query that was moved from fails with Result::State.
//
NMOS_TEST(CppQueryAsksTheRegistry)
{
    FakeNetwork Network;
    auto Query = OpenQuery(Network);
    NMOS_ASSERT(Query.has_value());
    const auto Senders = Query->Senders();
    NMOS_ASSERT(Senders.has_value() && Senders->size() == 1);
    const DtNmos::SenderInfo& Camera = Senders->front();
    NMOS_ASSERT(Camera.Id == *DtNmos::Id::FromText(CAMERA_ID));
    NMOS_ASSERT(Camera.Label == "camera 1");
    NMOS_ASSERT(Camera.Media == DtNmos::Media::Video);
    NMOS_ASSERT(Camera.Transport == "urn:x-nmos:transport:rtp.mcast");
    NMOS_ASSERT(Camera.ManifestHref == "http://camera.test/video.sdp");
    const auto Found = Query->FindSender("camera 1");
    NMOS_ASSERT(Found.has_value());
    NMOS_ASSERT(*Found == Camera);
    NMOS_ASSERT(Query->SenderManifest(Camera).value_or("") == CAMERA_SDP);
    const auto Sdp = Query->SenderSdp(Camera);
    NMOS_ASSERT(Sdp.has_value());
    NMOS_ASSERT(Sdp->Session.Name == "camera 1");
    NMOS_ASSERT_EQ(Sdp->Flows.size(), 1);
    NMOS_ASSERT_EQ(Sdp->Flows[0].DestinationPort, 5004);

    const auto Receivers = Query->Receivers();
    NMOS_ASSERT(Receivers.has_value() && Receivers->size() == 1);
    NMOS_ASSERT(Receivers->front().Label == "monitor");
    NMOS_ASSERT(Receivers->front().Media == DtNmos::Media::Video);
    NMOS_ASSERT(Receivers->front().DeviceId == *DtNmos::Id::FromText(DEVICE_ID));
    NMOS_ASSERT(!Receivers->front().Active);
    const auto Monitor = Query->FindReceiver(MONITOR_ID);
    NMOS_ASSERT(Monitor.has_value() && *Monitor == Receivers->front());
    const auto Nobody = Query->FindSender("nobody");
    NMOS_ASSERT(!Nobody.has_value());
    NMOS_ASSERT(Nobody.error().Code == DtNmos::Result::NotFound);

    const auto Refused = DtNmos::Query::Open({.RegistryUrl = "http://registry.test",
                                              .ApiVersion = "",
                                              .Http = {},
                                              .TimeoutMs = 0,
                                              .Log = {}});
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::InvalidArgument);
    DtNmos::Query Moved = std::move(*Query);
    NMOS_ASSERT(Query->Senders().error().Code == DtNmos::Result::State);
    NMOS_ASSERT(Moved.Senders().has_value());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryConnects -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Connecting the monitor to the camera gives both, and the camera's SDP. The query sends
// a PATCH of the staged parameters to the monitor's node. Disconnecting the monitor by
// its ID gives the monitor, after another PATCH.
//
NMOS_TEST(CppQueryConnects)
{
    FakeNetwork Network;
    auto Query = OpenQuery(Network);
    NMOS_ASSERT(Query.has_value());
    const auto Connected = Query->Connect("monitor", "camera 1");
    NMOS_ASSERT(Connected.has_value());
    NMOS_ASSERT(Connected->Receiver.Id == *DtNmos::Id::FromText(MONITOR_ID));
    NMOS_ASSERT(Connected->Sender.Id == *DtNmos::Id::FromText(CAMERA_ID));
    NMOS_ASSERT(Connected->Sdp == CAMERA_SDP);
    NMOS_ASSERT(Network.Requests.back() == "PATCH " STAGED);

    const auto Disconnected = Query->Disconnect(MONITOR_ID);
    NMOS_ASSERT(Disconnected.has_value());
    NMOS_ASSERT(Disconnected->Label == "monitor");
    NMOS_ASSERT(Network.Requests.back() == "PATCH " STAGED);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryParsesASender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A sender read from its JSON has its ID, label and flow. Its media is Media::Other, as
// the media belongs to the flow. Text that is no JSON object is refused.
//
NMOS_TEST(CppQueryParsesASender)
{
    const auto Camera = DtNmos::SenderInfo::Parse(CAMERA);
    NMOS_ASSERT(Camera.has_value());
    NMOS_ASSERT(Camera->Id == *DtNmos::Id::FromText(CAMERA_ID));
    NMOS_ASSERT(Camera->Label == "camera 1");
    NMOS_ASSERT(Camera->FlowId == *DtNmos::Id::FromText(VIDEO_FLOW));
    NMOS_ASSERT(Camera->Media == DtNmos::Media::Other);
    const auto Refused = DtNmos::SenderInfo::Parse("[]");
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::Parse);
}

// A WebSocket of the test. It gives its messages in turn; an empty one stands for no
// message in time. After the last one, it reports that it closed.
class FakeWebSocket : public DtNmos::WebSocketConnection
{
  public:
    FakeWebSocket(std::vector<std::string> Given, bool& ClosedFlag)
        : Messages(std::move(Given)), Closed(ClosedFlag)
    {
    }
    ~FakeWebSocket() override { Closed = true; }

    DtNmos::Expected<std::string> Receive(std::chrono::milliseconds) override
    {
        if (Next == Messages.size())
        {
            return std::unexpected(DtNmos::Error{DtNmos::Result::Network,
                                                 "The server closed the WebSocket."});
        }
        std::string Message = Messages[Next++];
        if (Message.empty())
        {
            return std::unexpected(DtNmos::Error{DtNmos::Result::Timeout, "no message"});
        }
        return Message;
    }

  private:
    std::vector<std::string> Messages;
    std::size_t Next = 0;
    bool& Closed;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQuerySubscribes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A subscription to the senders connects to the WebSocket that the registry names. Its
// polls report the camera as present, then Result::Timeout, then the camera removed,
// then Result::Network when the WebSocket closed. Destroying the subscription closes
// the WebSocket, and the query may be destroyed after it.
//
NMOS_TEST(CppQuerySubscribes)
{
    FakeNetwork Network;
    auto Query = OpenQuery(Network);
    NMOS_ASSERT(Query.has_value());
    bool Closed = false;
    std::string ConnectedTo;
    std::vector<DtNmos::Change> Changes;
    DtNmos::SubscriptionConfig Config;
    Config.ResourcePath = "/senders";
    Config.WebSocket = [&](const std::string& Url, std::chrono::milliseconds)
        -> DtNmos::Expected<std::unique_ptr<DtNmos::WebSocketConnection>>
    {
        ConnectedTo = Url;
        return std::make_unique<FakeWebSocket>(
            std::vector<std::string>{
                GRAIN("{\"path\": \"" CAMERA_ID "\", \"pre\": " CAMERA
                      ", \"post\": " CAMERA "}"),
                "", GRAIN("{\"path\": \"" CAMERA_ID "\", \"pre\": " CAMERA "}")},
            Closed);
    };
    Config.OnChange = [&](const DtNmos::Change& Changed) { Changes.push_back(Changed); };
    auto Subscription = DtNmos::Subscription::Open(*Query, Config);
    NMOS_ASSERT(Subscription.has_value());
    NMOS_ASSERT(ConnectedTo == WS_HREF);
    NMOS_ASSERT(Subscription->Url() == WS_HREF);

    NMOS_ASSERT(Subscription->Poll(std::chrono::milliseconds(50)).has_value());
    NMOS_ASSERT(Subscription->Poll(std::chrono::milliseconds(50)).error().Code ==
                DtNmos::Result::Timeout);
    NMOS_ASSERT(Subscription->Poll(std::chrono::milliseconds(50)).has_value());
    NMOS_ASSERT(Subscription->Poll(std::chrono::milliseconds(50)).error().Code ==
                DtNmos::Result::Network);
    NMOS_ASSERT_EQ(Changes.size(), 2);
    NMOS_ASSERT(Changes[0].Kind == DtNmos::ChangeKind::Present);
    NMOS_ASSERT(Changes[0].Id == *DtNmos::Id::FromText(CAMERA_ID));
    NMOS_ASSERT(Changes[0].Pre.has_value() && Changes[0].Post.has_value());
    NMOS_ASSERT(Changes[1].Kind == DtNmos::ChangeKind::Removed);
    NMOS_ASSERT(!Changes[1].Post.has_value());
    NMOS_ASSERT(DtNmos::Name(DtNmos::ChangeKind::Removed) == "removed");

    NMOS_ASSERT(!Closed);
    Subscription = std::unexpected(DtNmos::Error{});
    NMOS_ASSERT(Closed);
}

NMOS_TEST_MAIN("CppQuery", NMOS_RUN(CppQueryFedSearch), NMOS_RUN(CppQueryRefusesASearch),
               NMOS_RUN(CppQueryDiscovers), NMOS_RUN(CppQueryAsksTheRegistry),
               NMOS_RUN(CppQueryConnects), NMOS_RUN(CppQueryParsesASender),
               NMOS_RUN(CppQuerySubscribes))
