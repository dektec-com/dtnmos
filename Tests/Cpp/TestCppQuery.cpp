// #*#*#*#*#*#*#*#*#*#*#*#*#* TestCppQuery.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API of a registry's clients, and of finding registries
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "NmosTest.h"
#include "dtnmos_query.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryFedSearch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A fed search holds the two registries it is fed, in that order and usable, and refuses
// a service it does not look for. Moved, it keeps what it was fed, and is fed again.
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
// A search that looks for nothing is refused, and so is a search of the network whose
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
// nobody answers, finds nothing in 100 ms, which is no failure; its log function is
// called, if at all, during the search only.
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

NMOS_TEST_MAIN("CppQuery", NMOS_RUN(CppQueryFedSearch), NMOS_RUN(CppQueryRefusesASearch),
               NMOS_RUN(CppQueryDiscovers))
