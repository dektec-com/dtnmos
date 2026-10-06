// #*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_query.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API of a registry's clients, and of finding registries
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos_query.h. Discover() searches the network once for registries,
// with DNS-SD; a RegistrySearch keeps searching on a thread of its own, for the nodes and
// clients of an application to share.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dtnmos.hpp"
#include "dtnmos_http.hpp"
#include "dtnmos_query.h"

namespace DtNmos
{

class Node;

namespace Detail
{
struct RegistrySearchState;
} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+= Finding registries through DNS-SD +=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A registry announces its APIs on the network with DNS-SD. Discover() searches once,
// through multicast DNS on the local link and through the network's DNS server, and
// returns the registries it found.
//

// Which API of a registry to look for: the values of DtNmosService.
enum class Service : int
{
    None = DTNMOS_SERVICE_NONE,   // Not given; refused
    Query = DTNMOS_SERVICE_QUERY, // The Query API (_nmos-query._tcp)
    Registration =
        DTNMOS_SERVICE_REGISTRATION // The Registration API (_nmos-register._tcp)
};

// How registries are searched for: the values of DtNmosSearch, and Both, which is the
// two combined.
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
    // The IPv4 address of the network interface to search on; "" for the one of the
    // default route.
    std::string InterfaceAddress;
    // Where to send multicast DNS queries, "<IPv4 address>:<port>"; "" for
    // "224.0.0.251:5353". A test may give a responder of its own.
    std::string Destination;
    uint32_t TimeoutMs = 0; // How long to collect answers; 1000 when 0
    LogFunction Log;        // Receives log messages; may be empty
    DtNmos::Search Searches = DtNmos::Search::Both;
    // The DNS server, "<IPv4 address>:<port>"; "" for the host's first IPv4 DNS server,
    // at port 53.
    std::string DnsServer;
    // The domain to search through the DNS server, e.g. "example.com"; "" for the host's
    // own. Without a DNS server or a domain, only multicast DNS is searched.
    std::string DnsDomain;
};

// One API of a registry that was found. A string not announced is "".
struct RegistryInfo
{
    DtNmos::Service Service = DtNmos::Service::None; // Which API this is
    std::string Instance; // The announced name, e.g. "Registry 1"
    std::string Host;     // The host name, e.g. "registry-1.local"
    std::string Address;  // The host's IPv4 address, when announced
    uint16_t Port = 0;
    // The base URL of the API, "<protocol>://<address>:<port>"; with the host name
    // instead of the address for https, or when no address was announced.
    std::string Url;
    std::string ApiProto;    // "http" or "https"
    std::string ApiVersions; // e.g. "v1.2,v1.3"
    int Priority = -1; // Lower is preferred; 100 and up are for development; -1 for none
    bool Auth = false; // The API asks for authorization (IS-10)
    bool Usable = false; // dtnmos can use it: v1.3, over http or https, without auth
    DtNmos::Search FoundBy = DtNmos::Search::Multicast; // The search that found it

    friend bool operator==(const RegistryInfo&, const RegistryInfo&) = default;
};

// Searches the network for registries of Config.Service, through multicast DNS and the
// DNS server at the same time, and returns what it found: usable ones first; then by
// priority, those without one last; those from the DNS server before those from
// multicast DNS; then by name. Among registries of the same priority, IS-04 asks a client
// to pick one at random; that is up to the program. Finding none is not a failure. Fails
// with:
//   Result::InvalidArgument  an address or domain in Config is malformed
//   Result::Network          the socket could not be opened, or the multicast query
//                            could not be sent
[[nodiscard]] Expected<std::vector<RegistryInfo>> Discover(const DiscoveryConfig& Config);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= One search of an application +=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//

// The APIs a search looks for.
enum class Finds : unsigned
{
    Query = DTNMOS_FINDS_QUERY,
    Registration = DTNMOS_FINDS_REGISTRATION,
    Both = DTNMOS_FINDS_QUERY | DTNMOS_FINDS_REGISTRATION
};

// How a search is opened.
struct RegistrySearchConfig
{
    DtNmos::Finds Finds = DtNmos::Finds::Both; // The APIs to look for
    // How to search, as for Discover(), for each API in Finds; its Service is not used.
    // Without one: multicast DNS and the host's DNS server, on the interface of the
    // default route.
    std::optional<DiscoveryConfig> Discovery;
    // True: never search; hold the lists the program gives with Feed() instead.
    bool Fed = false;
};

// A search for registries that runs in the background, every 3 seconds, and sooner while
// a node that uses it has no registry (after 1, 2, 4 and 8 seconds, as IS-04 asks). A
// program opens one, and gives it to its nodes (NodeConfig.Search); several nodes share
// one. A program that finds registries in another way "feeds" the search the list.
//
// The program owns the search, which is moved, not copied, and stops when it is
// destroyed. A node borrows it, and the program destroys the search after the nodes that
// borrow it are closed or destroyed: a search destroyed while a node borrows it prints
// so and calls std::terminate(), as a std::thread destroyed without join() does. Moving
// the search moves no node off it.
class RegistrySearch
{
  public:
    // Opens a search and, unless it is fed, starts searching. Fails with
    // Result::InvalidArgument when Config looks for nothing.
    [[nodiscard]] static Expected<RegistrySearch>
    Open(const RegistrySearchConfig& Config);

    RegistrySearch(RegistrySearch&&) noexcept = default;
    // Stops this search, which no node may borrow, before it takes Other's.
    RegistrySearch& operator=(RegistrySearch&& Other) noexcept;
    // Stops the search; calls std::terminate() while a node borrows it.
    ~RegistrySearch();

    // Gives a fed search its list of registries for Kind: base URLs, e.g.
    // "http://registry.local:8010", the most preferred first. It replaces the list it
    // had. Fails with Result::State when the search is not fed, and
    // Result::InvalidArgument when it does not look for Kind, or a URL is not http or
    // https.
    [[nodiscard]] Status Feed(DtNmos::Service Kind, const std::vector<std::string>& Urls);

    // Returns the registries for Kind found so far, in the order of Discover(); none
    // until the first search ends. The registries of a fed search are usable, with their
    // URL as name and their place in the list as priority. Fails with
    // Result::InvalidArgument when the search does not look for Kind.
    [[nodiscard]] Expected<std::vector<RegistryInfo>> List(DtNmos::Service Kind) const;

  private:
    friend class Node;

    RegistrySearch() = default;

    // Calls std::terminate() when a node borrows the search.
    void CheckUnborrowed() const noexcept;
    DtNmosRegistrySearch* GetNative() const;

    std::unique_ptr<Detail::RegistrySearchState> State;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

namespace Detail
{

// Converts a service of the C API, as FromNative(DtNmosResult) does.
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

// Converts the search that found a registry, one of the values of DtNmosSearch.
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

// Converts a registry of the C API.
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

// Frees a DtNmosRegistryList, for a std::unique_ptr.
struct RegistryListFree
{
    void operator()(DtNmosRegistryList* List) const { DtNmosRegistryList_Free(List); }
};

// Converts a list of registries of the C API, and frees it.
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

// Returns a discovery config as the C API takes it, pointing into Value, which must
// outlive it, and into Log, the LogFunction the C API calls; "" is NULL. Log is Value's,
// or a copy of it that lives longer.
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

// Frees a DtNmosRegistrySearch, for a std::unique_ptr.
struct RegistrySearchFree
{
    void operator()(DtNmosRegistrySearch* Search) const
    {
        DtNmosRegistrySearch_Free(Search);
    }
};

// What a RegistrySearch owns on the heap, where its nodes find it when it moves: the C
// search, the log function it calls for as long as it runs, which is declared first so
// that it is freed after the search, and how many nodes borrow it.
struct RegistrySearchState
{
    LogFunction Log;
    std::unique_ptr<DtNmosRegistrySearch, RegistrySearchFree> Native;
    std::atomic<int> Borrowers = 0;
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
    if (State != nullptr && State->Borrowers > 0)
    {
        std::fputs("DtNmos::RegistrySearch destroyed while a node borrows it: destroy or "
                   "close the node first.\n",
                   stderr);
        std::terminate();
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

} // namespace DtNmos
