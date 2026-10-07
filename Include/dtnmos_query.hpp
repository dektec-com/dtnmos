// #*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_query.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API for asking a registry, following it, and finding registries
//
// SPDX-License-Identifier: BSD-3-Clause
//
// This is the C++ API of dtnmos_query.h. A Query asks a registry for its senders and
// receivers, and connects them as a controller does. A Subscription tells the program
// when resources in the registry change. Discover() searches the network once for
// registries, with DNS-SD. A RegistrySearch keeps searching on a thread of its own, and
// the nodes and clients of an application share it.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dtnmos.hpp"
#include "dtnmos_http.hpp"
#include "dtnmos_query.h"
#include "dtnmos_sdp.hpp"

namespace DtNmos
{

class Node;
class Subscription;

namespace Detail
{
struct QueryState;
struct RegistrySearchState;
struct SubscriptionState;
} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= The Query API (IS-04 v1.3) +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A Query asks one registry for its senders and receivers. It also connects them, as an
// NMOS controller does (IS-05 v1.1): it finds the node of a receiver or sender through
// the registry, and sends the request to the Connection API of that node. Open a query
// on the URL of the registry, then list or find senders and receivers.
//

// How a query is opened.
struct QueryConfig
{
    std::string RegistryUrl; // The base URL of the registry, e.g. "http://registry.local"
    std::string ApiVersion;  // The Query API version; "" for "v1.3", the only one known
    HttpFunction Http;       // Sends the requests, e.g. CurlHttp; required
    uint32_t TimeoutMs = 0;  // How long a request may take; 5000 when 0
    LogFunction Log;         // Receives log messages; may be empty
};

// A sender, as the registry lists it. A string that the registry does not give is "".
struct SenderInfo
{
    // Reads a sender from its JSON, as the Pre or Post of a Change holds it. The JSON
    // of a sender does not tell its media, so Media is Media::Other. Fails with
    // Result::Parse when Json is not a JSON object.
    [[nodiscard]] static Expected<SenderInfo> Parse(std::string_view Json);

    DtNmos::Id Id;           // The ID of the sender
    DtNmos::Id FlowId;       // The flow it sends; empty when it has none
    DtNmos::Id DeviceId;     // The device it belongs to
    std::string Label;       // Its label
    std::string Description; // Its description
    DtNmos::Media Media = DtNmos::Media::None; // From its flow; Other without one
    std::string Transport;                     // e.g. "urn:x-nmos:transport:rtp.mcast"
    std::string ManifestHref;                  // The URL of its SDP; "" when it has none

    friend bool operator==(const SenderInfo&, const SenderInfo&) = default;
};

// A receiver, as the registry lists it. A string that the registry does not give is "".
struct ReceiverInfo
{
    DtNmos::Id Id;                             // The ID of the receiver
    DtNmos::Id DeviceId;                       // The device it belongs to
    std::string Label;                         // Its label
    std::string Description;                   // Its description
    DtNmos::Media Media = DtNmos::Media::None; // From its format; Other for another one
    std::string Transport;                     // e.g. "urn:x-nmos:transport:rtp"
    DtNmos::Id SenderId; // The sender it is connected to; empty when none
    bool Active = false; // True when that connection is active

    friend bool operator==(const ReceiverInfo&, const ReceiverInfo&) = default;
};

// What Query::Connect() connected: the receiver and the sender, as the registry lists
// them, and the SDP that the receiver was given as its transport file.
struct Connection
{
    ReceiverInfo Receiver; // The receiver that was connected
    SenderInfo Sender;     // The sender it was connected to
    std::string Sdp;       // The SDP of the sender
};

// A connection to one registry, to ask it questions and to connect its receivers.
//
// The program owns the query. A query is moved, not copied, and closes when it is
// destroyed. A Subscription borrows the query it is opened with, so the program must
// destroy its subscriptions before their query. A query that is destroyed while a
// subscription borrows it prints an error and calls std::terminate().
//
// Every function also fails with Result::State for a query that was moved from.
class Query
{
  public:
    // Opens a query on the registry that Config names. Fails with Result::InvalidArgument
    // when Config has no registry URL or no Http, or an API version other than v1.3.
    [[nodiscard]] static Expected<Query> Open(const QueryConfig& Config);

    Query(Query&&) noexcept = default;
    // Closes this query, and then takes over Other. Calls std::terminate() when a
    // subscription borrows this query.
    Query& operator=(Query&& Other) noexcept;
    // Closes the query. Calls std::terminate() when a subscription borrows it.
    ~Query();

    // Connects a receiver to a sender, so that the receiver receives what the sender
    // sends. Receiver and Sender are each an ID or a label. The query asks the node of
    // the receiver to activate it at once, with the SDP of the sender. Fails with:
    //   Result::NotFound         the receiver or sender does not exist, the receiver's
    //                            device has no Connection API, or the sender has no SDP
    //   Result::Ambiguous        a label names more than one
    //   Result::InvalidArgument  the sender sends another kind of media (video, audio or
    //                            data) than the receiver receives
    //   Result::Http             the node refused; the message holds its reason
    // and the failures of the requests.
    [[nodiscard]] Expected<Connection> Connect(std::string_view Receiver,
                                               std::string_view Sender);

    // Disconnects a receiver, so that it stops receiving. Receiver is an ID or a label.
    // Returns the receiver. Fails as Connect() does.
    [[nodiscard]] Expected<ReceiverInfo> Disconnect(std::string_view Receiver);

    // Finds a receiver by its ID or its label, as FindSender() finds a sender.
    [[nodiscard]] Expected<ReceiverInfo> FindReceiver(std::string_view IdOrLabel);

    // Finds a sender by its ID or its label. IdOrLabel is taken as an ID when it is a
    // UUID, and as a label otherwise. Fails with:
    //   Result::NotFound   no sender has that ID or label
    //   Result::Ambiguous  several senders have that label; the message lists their IDs
    // and the failures of the request.
    [[nodiscard]] Expected<SenderInfo> FindSender(std::string_view IdOrLabel);

    // Makes a sender send to DestinationIp and DestinationPort. Sender is an ID or a
    // label. The query asks the node of the sender to activate the new destination at
    // once. Returns the sender. Fails as Connect() does, and with Result::InvalidArgument
    // for a sender that does not send RTP.
    [[nodiscard]] Expected<SenderInfo> MoveSender(std::string_view Sender,
                                                  std::string_view DestinationIp,
                                                  uint16_t DestinationPort);

    // Returns all receivers of the registry. It also reads the later pages when the
    // registry returns the receivers page by page.
    [[nodiscard]] Expected<std::vector<ReceiverInfo>> Receivers();

    // Downloads the SDP of Sender from its ManifestHref, and returns the text. Fails with
    // Result::NotFound when the sender has no SDP, and the failures of the request.
    // SenderSdp() downloads and parses it in one call.
    [[nodiscard]] Expected<std::string> SenderManifest(const SenderInfo& Sender);

    // Returns all senders of the registry, each with the media of its flow. It also
    // reads the later pages when the registry returns the senders page by page.
    [[nodiscard]] Expected<std::vector<SenderInfo>> Senders();

    // Downloads the SDP of Sender and parses it, as Sdp::Parse() does.
    [[nodiscard]] Expected<DtNmos::Sdp> SenderSdp(const SenderInfo& Sender);

  private:
    friend class Subscription;

    Query() = default;

    // Fails with Result::State when the query was moved from.
    [[nodiscard]] Status CheckOpen() const;
    // Calls std::terminate() when a subscription borrows the query.
    void CheckUnborrowed() const noexcept;

    std::unique_ptr<Detail::QueryState> State;
};

// +=+=+=+=+=+=+=+=+=+= A subscription to the resources of a registry +=+=+=+=+=+=+=+=+=+=
//
// A subscription tells the program when resources in the registry change, for example
// when a sender is added, changed or removed. Open it with a query. Then call Poll()
// regularly; it calls the program's function for each change. The first poll reports
// every resource that is there.
//

// What happened to a resource. The values are those of DtNmosChangeKind.
enum class ChangeKind : int
{
    Present = DTNMOS_CHANGE_PRESENT, // It is there; the first poll reports each one so
    Added = DTNMOS_CHANGE_ADDED,
    Modified = DTNMOS_CHANGE_MODIFIED,
    Removed = DTNMOS_CHANGE_REMOVED
};

// Returns a name for Kind to use in messages, e.g. "added".
std::string_view Name(ChangeKind Kind);

// One change of a resource in the registry.
struct Change
{
    ChangeKind Kind = ChangeKind::Present; // What happened
    DtNmos::Id Id;                         // The ID of the resource
    std::optional<std::string> Pre;  // Its JSON before the change; none when not there
    std::optional<std::string> Post; // Its JSON after the change; none when it is gone

    friend bool operator==(const Change&, const Change&) = default;
};

// The program's function that receives each change. Subscription::Poll() calls it, on the
// thread that calls Poll(). An exception it throws is dropped.
using ChangeFunction = std::function<void(const Change& Changed)>;

// How a subscription is opened.
struct SubscriptionConfig
{
    std::string ResourcePath;     // Which resources to follow, e.g. "/senders"
    uint32_t MaxUpdateRateMs = 0; // The shortest time between two messages; 100 when 0
    WebSocketConnect WebSocket;   // Opens the WebSocket; empty for the one on libcurl
    ChangeFunction OnChange;      // Receives each change; Poll() calls it
};

// A subscription to changes in a registry. It is open from Open() until it is destroyed.
// Destroying it closes its WebSocket, and the registry then ends the subscription. A
// subscription is moved, not copied.
class Subscription
{
  public:
    // Opens a subscription to the resources at Config.ResourcePath. It asks the registry
    // of Registry for the subscription, and connects to the WebSocket that the registry
    // names. The subscription borrows Registry until it is destroyed. Fails with
    // Result::Parse when the answer of the registry names no WebSocket, and with the
    // failures of the request and of the connection to the WebSocket.
    [[nodiscard]] static Expected<Subscription> Open(Query& Registry,
                                                     const SubscriptionConfig& Config);

    Subscription(Subscription&&) noexcept = default;
    // Closes this subscription, and then takes over Other.
    Subscription& operator=(Subscription&& Other) noexcept;
    // Closes the subscription.
    ~Subscription();

    // Waits up to Timeout for a message from the registry. For each change in the
    // message, it calls OnChange, on this thread. The first message reports every
    // resource as ChangeKind::Present. Fails with:
    //   Result::Timeout  no message came
    //   Result::Parse    a message could not be read; polling may go on
    //   Result::Network  the WebSocket closed or failed; open a new subscription, which
    //                    starts again with every resource
    // and Result::State for a subscription that was moved from.
    [[nodiscard]] Status Poll(std::chrono::milliseconds Timeout);

    // Returns the URL of the WebSocket, or "" for a subscription that was moved from.
    std::string Url() const;

  private:
    Subscription() = default;

    std::unique_ptr<Detail::SubscriptionState> State;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+= Finding registries through DNS-SD +=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A registry announces its APIs on the network with DNS-SD. Discover() searches once, and
// returns the registries it found. It asks multicast DNS on the local link and the DNS
// server of the network at the same time.
//

// Which API of a registry to look for. The values are those of DtNmosService.
enum class Service : int
{
    None = DTNMOS_SERVICE_NONE,   // Not given; refused
    Query = DTNMOS_SERVICE_QUERY, // The Query API (_nmos-query._tcp)
    Registration =
        DTNMOS_SERVICE_REGISTRATION // The Registration API (_nmos-register._tcp)
};

// How to search for registries. Multicast and Unicast are the values of DtNmosSearch;
// Both is the two together.
enum class Search : unsigned
{
    Multicast =
        DTNMOS_SEARCH_MULTICAST,     // Multicast DNS on the local link, domain "local"
    Unicast = DTNMOS_SEARCH_UNICAST, // The network's DNS server, in the host's domain
    Both = DTNMOS_SEARCH_MULTICAST | DTNMOS_SEARCH_UNICAST
};

// How Discover() searches.
struct DiscoveryConfig
{
    DtNmos::Service Service = DtNmos::Service::None; // Which API to look for
    // The IPv4 address of the network interface to search on; "" for the interface of
    // the default route.
    std::string InterfaceAddress;
    // Where to send the multicast DNS queries, as "<IPv4 address>:<port>"; "" for
    // "224.0.0.251:5353". A test may give a responder of its own.
    std::string Destination;
    uint32_t TimeoutMs = 0; // How long to collect answers; 1000 when 0
    LogFunction Log;        // Receives log messages; may be empty
    DtNmos::Search Searches = DtNmos::Search::Both; // Which ways to search
    // The DNS server, as "<IPv4 address>:<port>"; "" for the first IPv4 DNS server of the
    // host, at port 53.
    std::string DnsServer;
    // The domain to search through the DNS server, e.g. "example.com"; "" for the domain
    // of the host. Without a DNS server or a domain, Discover() asks multicast DNS only.
    std::string DnsDomain;
};

// One API of a registry that was found. A string that the registry does not announce is
// "".
struct RegistryInfo
{
    DtNmos::Service Service = DtNmos::Service::None; // Which API this is
    std::string Instance; // The announced name, e.g. "Registry 1"
    std::string Host;     // The host name, e.g. "registry-1.local"
    std::string Address;  // The IPv4 address of the host, when it is announced
    uint16_t Port = 0;    // The port of the API
    // The base URL of the API, "<protocol>://<address>:<port>". It has the host name
    // instead of the address for https, and when no address was announced.
    std::string Url;
    std::string ApiProto;    // "http" or "https"
    std::string ApiVersions; // e.g. "v1.2,v1.3"
    int Priority = -1; // Lower is preferred; 100 and up are for development; -1 for none
    bool Auth = false; // The API asks for authorization (IS-10)
    bool Usable = false; // dtnmos can use it: v1.3, over http or https, without auth
    DtNmos::Search FoundBy = DtNmos::Search::Multicast; // The search that found it

    friend bool operator==(const RegistryInfo&, const RegistryInfo&) = default;
};

// Searches the network for registries with the API Config.Service, and returns the ones
// it found. It asks multicast DNS and the DNS server at the same time. Finding none is
// not a failure.
//
// The registries come in this order: the usable ones first; then by priority, with
// those without a priority last; then the ones that the DNS server announced before the
// ones that multicast DNS did; then by name. IS-04 asks a client to pick at random among
// registries of the same priority; that is up to the program. Fails with:
//   Result::InvalidArgument  an address or domain in Config is malformed
//   Result::Network          the socket could not be opened, or the multicast query
//                            could not be sent
[[nodiscard]] Expected<std::vector<RegistryInfo>> Discover(const DiscoveryConfig& Config);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= One search of an application +=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//

// Which APIs a RegistrySearch looks for.
enum class Finds : unsigned
{
    Query = DTNMOS_FINDS_QUERY,
    Registration = DTNMOS_FINDS_REGISTRATION,
    Both = DTNMOS_FINDS_QUERY | DTNMOS_FINDS_REGISTRATION
};

// How a search is opened.
struct RegistrySearchConfig
{
    DtNmos::Finds Finds = DtNmos::Finds::Both; // Which APIs to look for
    // How to search for each API in Finds, as for Discover(); its Service is not used.
    // Without it, the search asks multicast DNS and the DNS server of the host, on the
    // interface of the default route.
    std::optional<DiscoveryConfig> Discovery;
    // True: the search never searches. It holds the lists that the program gives it with
    // Feed() instead.
    bool Fed = false;
};

// A search for registries that runs in the background.
//
// It searches every 3 seconds. While a node that uses it has no registry, it searches
// sooner: after 1, 2, 4 and 8 seconds, as IS-04 asks. A program opens one search, and
// gives it to its nodes in NodeConfig.Search; several nodes share it. A program that
// finds its registries in another way gives the search the list instead ("feeds" it).
//
// The program owns the search. A search is moved, not copied, and stops when it is
// destroyed. A node borrows the search, so the program must close or destroy its nodes
// before their search. A search that is destroyed while a node borrows it prints an
// error and calls std::terminate(), just as destroying a std::thread without join()
// does. Moving a search does not take it away from the nodes that borrow it.
class RegistrySearch
{
  public:
    // Opens a search. Unless it is fed, it starts searching. Fails with
    // Result::InvalidArgument when Config looks for no API.
    [[nodiscard]] static Expected<RegistrySearch>
    Open(const RegistrySearchConfig& Config);

    RegistrySearch(RegistrySearch&&) noexcept = default;
    // Stops this search, and then takes over Other. Calls std::terminate() when a node
    // borrows this search.
    RegistrySearch& operator=(RegistrySearch&& Other) noexcept;
    // Stops the search. Calls std::terminate() when a node borrows it.
    ~RegistrySearch();

    // Gives a fed search its list of registries for Kind. Urls are base URLs, e.g.
    // "http://registry.local:8010", with the most preferred one first. The list replaces
    // the one the search had. Fails with:
    //   Result::State            the search is not fed
    //   Result::InvalidArgument  the search does not look for Kind, or a URL is not http
    //                            or https
    [[nodiscard]] Status Feed(DtNmos::Service Kind, const std::vector<std::string>& Urls);

    // Returns the registries for Kind that the search found so far, in the order that
    // Discover() gives. The list is empty until the first search ends. The registries of
    // a fed search are usable; their URL is their name, and their place in the list is
    // their priority. Fails with Result::InvalidArgument when the search does not look
    // for Kind.
    [[nodiscard]] Expected<std::vector<RegistryInfo>> List(DtNmos::Service Kind) const;

  private:
    friend class Node;

    RegistrySearch() = default;

    // Calls std::terminate() when a node borrows the search.
    void CheckUnborrowed() const noexcept;
    // Returns the C search, or nullptr for a search that was moved from.
    DtNmosRegistrySearch* GetNative() const;

    std::unique_ptr<Detail::RegistrySearchState> State;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Not for programs: what the C++ headers use to convert the types of this header to and
// from those of the C API, and the state that a query, a subscription and a search keep
// on the heap.
//

namespace Detail
{

// Converts a DtNmosService to a Service. Each value has a case, so that the compiler
// warns about a value that the conversion misses.
inline Service FromNative(DtNmosService Native)
{
    switch (Native)
    {
    case DTNMOS_SERVICE_NONE:
        return Service::None;
    case DTNMOS_SERVICE_QUERY:
        return Service::Query;
    case DTNMOS_SERVICE_REGISTRATION:
        return Service::Registration;
    }
    return static_cast<Service>(Native);
}

// Converts the way that a registry was found, a value of DtNmosSearch, to a Search.
inline Search FromNative(DtNmosSearch Native)
{
    switch (Native)
    {
    case DTNMOS_SEARCH_MULTICAST:
        return Search::Multicast;
    case DTNMOS_SEARCH_UNICAST:
        return Search::Unicast;
    }
    return static_cast<Search>(Native);
}

inline DtNmosService ToNative(Service Value)
{
    return static_cast<DtNmosService>(Value);
}

// Converts a registry that the C API found to a RegistryInfo.
inline RegistryInfo FromNative(const DtNmosRegistryInfo& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosRegistryInfo, FoundBy);
    RegistryInfo Value;
    Value.Service = FromNative(Native.Service);
    Value.Instance = FromNative(Native.Instance);
    Value.Host = FromArray(Native.Host);
    Value.Address = FromArray(Native.Address);
    Value.Port = Native.Port;
    Value.Url = FromNative(Native.Url);
    Value.ApiProto = FromArray(Native.ApiProto);
    Value.ApiVersions = FromNative(Native.ApiVersions);
    Value.Priority = Native.Priority;
    Value.Auth = Native.Auth;
    Value.Usable = Native.Usable;
    Value.FoundBy = FromNative(Native.FoundBy);
    return Value;
}

// Frees a DtNmosRegistryList; the deleter of a std::unique_ptr.
struct RegistryListFree
{
    void operator()(DtNmosRegistryList* List) const { DtNmosRegistryList_Free(List); }
};

// Converts a list of registries that the C API returned, and frees the list.
inline std::vector<RegistryInfo> TakeList(DtNmosRegistryList* Native)
{
    const std::unique_ptr<DtNmosRegistryList, RegistryListFree> Owned(Native);
    std::vector<RegistryInfo> Value;
    for (std::size_t i = 0; i < DtNmosRegistryList_Count(Native); ++i)
    {
        Value.push_back(FromNative(*DtNmosRegistryList_At(Native, i)));
    }
    return Value;
}

// Returns Value as the C API takes it. An empty text becomes NULL.
//
// The result points into Value and into Log, so both must outlive it. Log is the
// LogFunction that the C API calls. It is the Log of Value, or a copy of it that lives
// longer than Value.
inline DtNmosDiscoveryConfig ToNative(const DiscoveryConfig& Value,
                                      const LogFunction& Log)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosDiscoveryConfig, DnsDomain);
    DtNmosDiscoveryConfig Native{};
    Native.Size = sizeof(Native);
    Native.Service = ToNative(Value.Service);
    Native.InterfaceAddress =
        Value.InterfaceAddress.empty() ? nullptr : Value.InterfaceAddress.c_str();
    Native.Destination = Value.Destination.empty() ? nullptr : Value.Destination.c_str();
    Native.TimeoutMs = Value.TimeoutMs;
    Native.Log = Log ? LogTrampoline : nullptr;
    Native.LogUser = Log ? const_cast<LogFunction*>(&Log) : nullptr;
    Native.Searches = static_cast<unsigned>(Value.Searches);
    Native.DnsServer = Value.DnsServer.empty() ? nullptr : Value.DnsServer.c_str();
    Native.DnsDomain = Value.DnsDomain.empty() ? nullptr : Value.DnsDomain.c_str();
    return Native;
}

// Frees a DtNmosRegistrySearch; the deleter of a std::unique_ptr.
struct RegistrySearchFree
{
    void operator()(DtNmosRegistrySearch* Search) const
    {
        DtNmosRegistrySearch_Free(Search);
    }
};

// Holds what a RegistrySearch owns. It is on the heap, so that it stays in one place when
// the RegistrySearch moves, and the nodes that borrow the search still find it.
struct RegistrySearchState
{
    // The log function that the C search calls while it runs. It is declared before
    // Native, so that it is destroyed after the C search.
    LogFunction Log;
    std::unique_ptr<DtNmosRegistrySearch, RegistrySearchFree> Native; // The C search
    std::atomic<int> Borrowers = 0; // How many nodes borrow the search
};

// Prints Message and calls std::terminate() when Borrowers is above 0. A program calls
// it when it destroys an object that another object still borrows.
inline void TerminateIfBorrowed(const std::atomic<int>& Borrowers,
                                const char* Message) noexcept
{
    if (Borrowers > 0)
    {
        std::fputs(Message, stderr);
        std::fputs("\n", stderr);
        std::terminate();
    }
}

// Converts a sender that the C API returned to a SenderInfo.
inline SenderInfo FromNative(const DtNmosSenderInfo& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosSenderInfo, ManifestHref);
    SenderInfo Value;
    Value.Id = Access::FromNative(Native.Id);
    Value.FlowId = Access::FromNative(Native.FlowId);
    Value.DeviceId = Access::FromNative(Native.DeviceId);
    Value.Label = FromNative(Native.Label);
    Value.Description = FromNative(Native.Description);
    Value.Media = FromNative(Native.Media);
    Value.Transport = FromNative(Native.Transport);
    Value.ManifestHref = FromNative(Native.ManifestHref);
    return Value;
}

// Converts a receiver that the C API returned to a ReceiverInfo.
inline ReceiverInfo FromNative(const DtNmosReceiverInfo& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosReceiverInfo, Active);
    ReceiverInfo Value;
    Value.Id = Access::FromNative(Native.Id);
    Value.DeviceId = Access::FromNative(Native.DeviceId);
    Value.Label = FromNative(Native.Label);
    Value.Description = FromNative(Native.Description);
    Value.Media = FromNative(Native.Media);
    Value.Transport = FromNative(Native.Transport);
    Value.SenderId = Access::FromNative(Native.SenderId);
    Value.Active = Native.Active;
    return Value;
}

// Returns Value as the C API takes it. The result points into Value, so Value must
// outlive it.
inline DtNmosSenderInfo ToNative(const SenderInfo& Value)
{
    DtNmosSenderInfo Native{};
    Native.Id = Access::ToNative(Value.Id);
    Native.FlowId = Access::ToNative(Value.FlowId);
    Native.DeviceId = Access::ToNative(Value.DeviceId);
    Native.Label = Value.Label.c_str();
    Native.Description = Value.Description.c_str();
    Native.Media = ToNative(Value.Media);
    Native.Transport = Value.Transport.c_str();
    Native.ManifestHref = Value.ManifestHref.c_str();
    return Native;
}

// Free a list of senders, a list of receivers and a connection of the C API; the
// deleters of a std::unique_ptr.
struct SenderListFree
{
    void operator()(DtNmosSenderList* List) const { DtNmosSenderList_Free(List); }
};

struct ReceiverListFree
{
    void operator()(DtNmosReceiverList* List) const { DtNmosReceiverList_Free(List); }
};

struct ConnectionFree
{
    void operator()(DtNmosConnection* Value) const { DtNmosConnection_Free(Value); }
};

// Converts a list of senders that the C API returned, and frees the list.
inline std::vector<SenderInfo> TakeSenders(DtNmosSenderList* Native)
{
    const std::unique_ptr<DtNmosSenderList, SenderListFree> Owned(Native);
    std::vector<SenderInfo> Value;
    for (std::size_t i = 0; i < DtNmosSenderList_Count(Native); ++i)
    {
        Value.push_back(FromNative(*DtNmosSenderList_At(Native, i)));
    }
    return Value;
}

// Converts a list of receivers that the C API returned, and frees the list.
inline std::vector<ReceiverInfo> TakeReceivers(DtNmosReceiverList* Native)
{
    const std::unique_ptr<DtNmosReceiverList, ReceiverListFree> Owned(Native);
    std::vector<ReceiverInfo> Value;
    for (std::size_t i = 0; i < DtNmosReceiverList_Count(Native); ++i)
    {
        Value.push_back(FromNative(*DtNmosReceiverList_At(Native, i)));
    }
    return Value;
}

// Returns the sender that a C function found, and frees the list it came in. Code is the
// result of the C function, and Native is its list, which holds the one sender it found.
// Fails with the error of Code.
inline Expected<SenderInfo> TakeOneSender(DtNmosResult Code, DtNmosSenderList* Native)
{
    const Status Found = Check(Code);
    std::vector<SenderInfo> Senders = TakeSenders(Native);
    if (!Found)
    {
        return std::unexpected(Found.error());
    }
    if (Senders.empty())
    {
        return std::unexpected(Error{Result::Internal, "The library gave no sender."});
    }
    return std::move(Senders.front());
}

// Returns the receiver that a C function found, as TakeOneSender() returns a sender.
inline Expected<ReceiverInfo> TakeOneReceiver(DtNmosResult Code,
                                              DtNmosReceiverList* Native)
{
    const Status Found = Check(Code);
    std::vector<ReceiverInfo> Receivers = TakeReceivers(Native);
    if (!Found)
    {
        return std::unexpected(Found.error());
    }
    if (Receivers.empty())
    {
        return std::unexpected(Error{Result::Internal, "The library gave no receiver."});
    }
    return std::move(Receivers.front());
}

// Converts a DtNmosChangeKind to a ChangeKind. Each value has a case, so that the
// compiler warns about a value that the conversion misses.
inline ChangeKind FromNative(DtNmosChangeKind Native)
{
    switch (Native)
    {
    case DTNMOS_CHANGE_PRESENT:
        return ChangeKind::Present;
    case DTNMOS_CHANGE_ADDED:
        return ChangeKind::Added;
    case DTNMOS_CHANGE_MODIFIED:
        return ChangeKind::Modified;
    case DTNMOS_CHANGE_REMOVED:
        return ChangeKind::Removed;
    }
    return static_cast<ChangeKind>(Native);
}

inline DtNmosChangeKind ToNative(ChangeKind Value)
{
    return static_cast<DtNmosChangeKind>(Value);
}

// Converts a change that the C API reported to a Change. An ID that is not a UUID
// becomes an empty Id.
inline Change FromNative(const DtNmosChange& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosChange, PostLength);
    Change Value;
    Value.Kind = FromNative(Native.Kind);
    Value.Id = Id::FromText(FromNative(Native.Id)).value_or(Id());
    if (Native.Pre != nullptr)
    {
        Value.Pre = std::string(Native.Pre, Native.PreLength);
    }
    if (Native.Post != nullptr)
    {
        Value.Post = std::string(Native.Post, Native.PostLength);
    }
    return Value;
}

// Free a DtNmosQuery and a DtNmosSubscription, which closes it; the deleters of a
// std::unique_ptr.
struct QueryFree
{
    void operator()(DtNmosQuery* Query) const { DtNmosQuery_Free(Query); }
};

struct SubscriptionFree
{
    void operator()(DtNmosSubscription* Subscription) const
    {
        DtNmosSubscription_Free(Subscription);
    }
};

// Holds what a Query owns. It is on the heap, so that it stays in one place when the
// Query moves, and the subscriptions that borrow the query still find it.
struct QueryState
{
    // The functions that the C query calls. They are declared before Native, so that
    // they are destroyed after the C query.
    HttpFunction Http;
    LogFunction Log;
    std::unique_ptr<DtNmosQuery, QueryFree> Native; // The C query
    std::atomic<int> Borrowers = 0; // How many subscriptions borrow the query
};

// Calls the ChangeFunction that User points to; the C callback of a subscription. The C
// callback cannot return a failure, so an exception that the function throws is
// dropped.
inline void ChangeTrampoline(void* User, const DtNmosChange* Changed) noexcept
{
    const ChangeFunction& OnChange = *static_cast<const ChangeFunction*>(User);
    (void)Guard(Result::Internal, [&] { OnChange(FromNative(*Changed)); });
}

// Holds what a Subscription owns. It is on the heap, so that the C subscription can keep
// pointers to OnChange and WebSocket when the Subscription moves.
struct SubscriptionState
{
    ChangeFunction OnChange;        // The function that the C subscription calls
    NativeWebSocket WebSocket;      // The WebSocket that the C subscription reads
    QueryState* Borrowed = nullptr; // The query it borrows
    std::unique_ptr<DtNmosSubscription, SubscriptionFree> Native; // The C subscription

    SubscriptionState() = default;
    SubscriptionState(const SubscriptionState&) = delete;
    SubscriptionState& operator=(const SubscriptionState&) = delete;
    // Closes the C subscription first, so that it stops using the query. Only then does
    // it stop borrowing the query.
    ~SubscriptionState()
    {
        Native.reset();
        if (Borrowed != nullptr)
        {
            --Borrowed->Borrowers;
        }
    }
};

} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Discover -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<std::vector<RegistryInfo>> Discover(const DiscoveryConfig& Config)
{
    const DtNmosDiscoveryConfig Native = Detail::ToNative(Config, Config.Log);
    DtNmosRegistryList* List = nullptr;
    const Status Found = Detail::Check(DtNmos_Discover(&Native, &List));
    if (!Found)
    {
        return std::unexpected(Found.error());
    }
    return Detail::TakeList(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<RegistrySearch> RegistrySearch::Open(const RegistrySearchConfig& Config)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosRegistrySearchConfig, Fed);
    RegistrySearch Made;
    Made.State = std::make_unique<Detail::RegistrySearchState>();
    Made.State->Native.reset(DtNmosRegistrySearch_Alloc());
    if (Made.State->Native == nullptr)
    {
        return std::unexpected(Error{Result::NoMemory, "Out of memory."});
    }
    DtNmosRegistrySearchConfig Native{};
    Native.Size = sizeof(Native);
    Native.Finds = static_cast<unsigned>(Config.Finds);
    Native.Fed = Config.Fed;
    DtNmosDiscoveryConfig Discovery{};
    if (Config.Discovery.has_value())
    {
        Made.State->Log = Config.Discovery->Log;
        Discovery = Detail::ToNative(*Config.Discovery, Made.State->Log);
        Native.Discovery = &Discovery;
    }
    const Status Opened =
        Detail::Check(DtNmosRegistrySearch_Open(Made.State->Native.get(), &Native));
    if (!Opened)
    {
        return std::unexpected(Opened.error());
    }
    return Made;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::= -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline RegistrySearch& RegistrySearch::operator=(RegistrySearch&& Other) noexcept
{
    if (this != &Other)
    {
        CheckUnborrowed();
        State = std::move(Other.State);
    }
    return *this;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::~ -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline RegistrySearch::~RegistrySearch()
{
    CheckUnborrowed();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::CheckUnborrowed -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline void RegistrySearch::CheckUnborrowed() const noexcept
{
    if (State != nullptr)
    {
        Detail::TerminateIfBorrowed(
            State->Borrowers, "DtNmos::RegistrySearch destroyed while a node borrows "
                              "it: destroy or close the node first.");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::Feed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Status RegistrySearch::Feed(DtNmos::Service Kind,
                                   const std::vector<std::string>& Urls)
{
    std::vector<const char*> Natives;
    for (const std::string& Url : Urls)
    {
        Natives.push_back(Url.c_str());
    }
    return Detail::Check(DtNmosRegistrySearch_Feed(GetNative(), Detail::ToNative(Kind),
                                                   Natives.data(), Natives.size()));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::GetNative -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline DtNmosRegistrySearch* RegistrySearch::GetNative() const
{
    return State != nullptr ? State->Native.get() : nullptr;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistrySearch::List -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<std::vector<RegistryInfo>>
RegistrySearch::List(DtNmos::Service Kind) const
{
    DtNmosRegistryList* Found = nullptr;
    const Status Listed = Detail::Check(
        DtNmosRegistrySearch_List(GetNative(), Detail::ToNative(Kind), &Found));
    if (!Listed)
    {
        return std::unexpected(Listed.error());
    }
    return Detail::TakeList(Found);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline std::string_view Name(ChangeKind Kind)
{
    return DtNmosChangeKind_Name(Detail::ToNative(Kind));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C query copies the config, except the functions it calls. The query keeps those
// in its state.
//
inline Expected<Query> Query::Open(const QueryConfig& Config)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosQueryConfig, LogUser);
    Query Made;
    Made.State = std::make_unique<Detail::QueryState>();
    Detail::QueryState& Kept = *Made.State;
    Kept.Http = Config.Http;
    Kept.Log = Config.Log;
    DtNmosQueryConfig NativeConfig{};
    NativeConfig.Size = sizeof(NativeConfig);
    NativeConfig.RegistryUrl = Detail::NullIfEmpty(Config.RegistryUrl);
    NativeConfig.ApiVersion = Detail::NullIfEmpty(Config.ApiVersion);
    const Detail::NativeHttp Http = Detail::ToNative(Kept.Http);
    NativeConfig.Http = Http.Function;
    NativeConfig.HttpUser = Http.User;
    NativeConfig.TimeoutMs = Config.TimeoutMs;
    NativeConfig.Log = Kept.Log ? Detail::LogTrampoline : nullptr;
    NativeConfig.LogUser = &Kept.Log;
    Kept.Native.reset(DtNmosQuery_Alloc());
    if (Kept.Native == nullptr)
    {
        return std::unexpected(Error{Result::NoMemory, "Out of memory."});
    }
    const Status Opened =
        Detail::Check(DtNmosQuery_Open(Kept.Native.get(), &NativeConfig));
    if (!Opened)
    {
        return std::unexpected(Opened.error());
    }
    return Made;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::= -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Query& Query::operator=(Query&& Other) noexcept
{
    if (this != &Other)
    {
        CheckUnborrowed();
        State = std::move(Other.State);
    }
    return *this;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::~ -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Query::~Query()
{
    CheckUnborrowed();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Status Query::CheckOpen() const
{
    if (State == nullptr)
    {
        return std::unexpected(
            Error{Result::State, "The query was moved to another, and is not open."});
    }
    return {};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::CheckUnborrowed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline void Query::CheckUnborrowed() const noexcept
{
    if (State != nullptr)
    {
        Detail::TerminateIfBorrowed(
            State->Borrowers, "DtNmos::Query destroyed while a subscription borrows "
                              "it: destroy the subscription first.");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<Connection> Query::Connect(std::string_view Receiver,
                                           std::string_view Sender)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const std::string ReceiverText(Receiver);
    const std::string SenderText(Sender);
    DtNmosConnection* Made = nullptr;
    const Status Connected = Detail::Check(DtNmosQuery_Connect(
        State->Native.get(), ReceiverText.c_str(), SenderText.c_str(), &Made));
    const std::unique_ptr<DtNmosConnection, Detail::ConnectionFree> Owned(Made);
    if (!Connected)
    {
        return std::unexpected(Connected.error());
    }
    Connection Value;
    Value.Receiver = Detail::FromNative(*DtNmosConnection_Receiver(Made));
    Value.Sender = Detail::FromNative(*DtNmosConnection_Sender(Made));
    Value.Sdp = Detail::FromNative(DtNmosConnection_Sdp(Made));
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::Disconnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Expected<ReceiverInfo> Query::Disconnect(std::string_view Receiver)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const std::string Text(Receiver);
    DtNmosReceiverList* Disconnected = nullptr;
    const DtNmosResult Done =
        DtNmosQuery_Disconnect(State->Native.get(), Text.c_str(), &Disconnected);
    return Detail::TakeOneReceiver(Done, Disconnected);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::FindReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Expected<ReceiverInfo> Query::FindReceiver(std::string_view IdOrLabel)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const std::string Text(IdOrLabel);
    DtNmosReceiverList* Found = nullptr;
    const DtNmosResult Done =
        DtNmosQuery_FindReceiver(State->Native.get(), Text.c_str(), &Found);
    return Detail::TakeOneReceiver(Done, Found);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::FindSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Expected<SenderInfo> Query::FindSender(std::string_view IdOrLabel)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const std::string Text(IdOrLabel);
    DtNmosSenderList* Found = nullptr;
    const DtNmosResult Done =
        DtNmosQuery_FindSender(State->Native.get(), Text.c_str(), &Found);
    return Detail::TakeOneSender(Done, Found);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::MoveSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Expected<SenderInfo> Query::MoveSender(std::string_view Sender,
                                              std::string_view DestinationIp,
                                              uint16_t DestinationPort)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const std::string Text(Sender);
    const std::string Destination(DestinationIp);
    DtNmosSenderList* Moved = nullptr;
    const DtNmosResult Done = DtNmosQuery_MoveSender(
        State->Native.get(), Text.c_str(), Destination.c_str(), DestinationPort, &Moved);
    return Detail::TakeOneSender(Done, Moved);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::Receivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<std::vector<ReceiverInfo>> Query::Receivers()
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    DtNmosReceiverList* List = nullptr;
    const Status Listed =
        Detail::Check(DtNmosQuery_Receivers(State->Native.get(), &List));
    std::vector<ReceiverInfo> Value = Detail::TakeReceivers(List);
    if (!Listed)
    {
        return std::unexpected(Listed.error());
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::SenderManifest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C function is called twice: first to learn the size of the SDP, then to write the
// SDP into a buffer of that size. It downloads the SDP on both calls.
//
inline Expected<std::string> Query::SenderManifest(const SenderInfo& Sender)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const DtNmosSenderInfo Native = Detail::ToNative(Sender);
    std::size_t Size = 0;
    const DtNmosResult Asked =
        DtNmosQuery_SenderManifest(State->Native.get(), &Native, nullptr, &Size);
    if (Asked != DTNMOS_E_BUFFER_TOO_SMALL)
    {
        return std::unexpected(
            Detail::LastError(Asked == DTNMOS_OK ? DTNMOS_E_INTERNAL : Asked));
    }
    std::string Text(Size, '\0');
    Size = Text.size();
    const Status Written = Detail::Check(
        DtNmosQuery_SenderManifest(State->Native.get(), &Native, Text.data(), &Size));
    if (!Written)
    {
        return std::unexpected(Written.error());
    }
    Text.resize(Size);
    return Text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::Senders -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<std::vector<SenderInfo>> Query::Senders()
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    DtNmosSenderList* List = nullptr;
    const Status Listed = Detail::Check(DtNmosQuery_Senders(State->Native.get(), &List));
    std::vector<SenderInfo> Value = Detail::TakeSenders(List);
    if (!Listed)
    {
        return std::unexpected(Listed.error());
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Query::SenderSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<DtNmos::Sdp> Query::SenderSdp(const SenderInfo& Sender)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const DtNmosSenderInfo Native = Detail::ToNative(Sender);
    DtNmosSdp* Parsed = nullptr;
    const Status Read =
        Detail::Check(DtNmosQuery_SenderSdp(State->Native.get(), &Native, &Parsed));
    const std::unique_ptr<DtNmosSdp, Detail::SdpFree> Owned(Parsed);
    if (!Read)
    {
        return std::unexpected(Read.error());
    }
    return Detail::FromNative(Parsed);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SenderInfo::Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Expected<SenderInfo> SenderInfo::Parse(std::string_view Json)
{
    DtNmosSenderList* Read = nullptr;
    const DtNmosResult Done = DtNmosSenderInfo_Parse(Json.data(), Json.size(), &Read);
    return Detail::TakeOneSender(Done, Read);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Subscription::Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C subscription copies the config, except the functions it calls. The subscription
// keeps those in its state.
//
inline Expected<Subscription> Subscription::Open(Query& Registry,
                                                 const SubscriptionConfig& Config)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosSubscriptionConfig, OnChangeUser);
    const Status RegistryOpen = Registry.CheckOpen();
    if (!RegistryOpen)
    {
        return std::unexpected(RegistryOpen.error());
    }
    Subscription Made;
    Made.State = std::make_unique<Detail::SubscriptionState>();
    Detail::SubscriptionState& Kept = *Made.State;
    Kept.OnChange = Config.OnChange;
    Kept.WebSocket.Connect = Config.WebSocket;
    DtNmosSubscriptionConfig NativeConfig{};
    NativeConfig.Size = sizeof(NativeConfig);
    NativeConfig.ResourcePath = Config.ResourcePath.c_str();
    NativeConfig.MaxUpdateRateMs = Config.MaxUpdateRateMs;
    NativeConfig.WebSocket = Kept.WebSocket.Get();
    NativeConfig.OnChange = Kept.OnChange ? Detail::ChangeTrampoline : nullptr;
    NativeConfig.OnChangeUser = &Kept.OnChange;
    Kept.Native.reset(DtNmosSubscription_Alloc());
    if (Kept.Native == nullptr)
    {
        return std::unexpected(Error{Result::NoMemory, "Out of memory."});
    }
    const Status Opened = Detail::Check(DtNmosSubscription_Open(
        Kept.Native.get(), Registry.State->Native.get(), &NativeConfig));
    if (!Opened)
    {
        return std::unexpected(Opened.error());
    }
    Kept.Borrowed = Registry.State.get();
    ++Kept.Borrowed->Borrowers;
    return Made;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Subscription::= -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Subscription& Subscription::operator=(Subscription&& Other) noexcept
{
    State = std::move(Other.State);
    return *this;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Subscription::~ -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The destructor of SubscriptionState closes the C subscription, and then stops
// borrowing the query.
//
inline Subscription::~Subscription() = default;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Subscription::Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Status Subscription::Poll(std::chrono::milliseconds Timeout)
{
    if (State == nullptr)
    {
        return std::unexpected(Error{
            Result::State, "The subscription was moved to another, and is not open."});
    }
    return Detail::Check(DtNmosSubscription_Poll(State->Native.get(),
                                                 static_cast<uint32_t>(Timeout.count())));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Subscription::Url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline std::string Subscription::Url() const
{
    return State != nullptr
               ? Detail::FromNative(DtNmosSubscription_Url(State->Native.get()))
               : std::string();
}

} // namespace DtNmos
