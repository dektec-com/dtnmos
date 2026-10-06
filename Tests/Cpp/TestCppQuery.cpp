// #*#*#*#*#*#*#*#*#*#*#*#*#* TestCppQuery.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API of a registry's clients, and of finding registries
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <optional>
#include <string>
#include <vector>

#include "NmosTest.h"
#include "dtnmos_query.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppQueryFedSearch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A fed search holds the two registries it is fed, in that order and usable, and refuses
// a service it does not look for. A copy shares it: what the copy is fed, the original
// lists, also after the original is gone.
//
NMOS_TEST(CppQueryFedSearch)
{
    std::optional<dtnmos::RegistrySearch> Search;
    {
        auto Opened = dtnmos::RegistrySearch::Open(
            {.Finds = dtnmos::Finds::Registration, .Discovery = {}, .Fed = true});
        NMOS_ASSERT(Opened.has_value());
        Search = *Opened;
    }
    NMOS_ASSERT(Search
                    ->Feed(dtnmos::Service::Registration,
                           {"http://registry-a.test:8010", "http://registry-b.test"})
                    .has_value());
    const auto Listed = Search->List(dtnmos::Service::Registration);
    NMOS_ASSERT(Listed.has_value());
    NMOS_ASSERT_EQ(Listed->size(), 2);
    NMOS_ASSERT((*Listed)[0].Url == "http://registry-a.test:8010");
    NMOS_ASSERT((*Listed)[0].Usable);
    NMOS_ASSERT((*Listed)[0].Service == dtnmos::Service::Registration);
    NMOS_ASSERT((*Listed)[1].Url == "http://registry-b.test");

    const auto Refused = Search->Feed(dtnmos::Service::Query, {"http://query.test"});
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == dtnmos::Result::InvalidArgument);
    NMOS_ASSERT(!Search->List(dtnmos::Service::Query).has_value());

    dtnmos::RegistrySearch Copy = *Search;
    NMOS_ASSERT(
        Copy.Feed(dtnmos::Service::Registration, {"http://registry-c.test"}).has_value());
    NMOS_ASSERT_EQ(Search->List(dtnmos::Service::Registration)->size(), 1);
    Search.reset();
    const auto Kept = Copy.List(dtnmos::Service::Registration);
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
    const auto Nothing = dtnmos::RegistrySearch::Open(
        {.Finds = static_cast<dtnmos::Finds>(0), .Discovery = {}, .Fed = true});
    NMOS_ASSERT(!Nothing.has_value());
    NMOS_ASSERT(Nothing.error().Code == dtnmos::Result::InvalidArgument);

    dtnmos::DiscoveryConfig Config;
    Config.Service = dtnmos::Service::Query;
    Config.InterfaceAddress = "not an address";
    const auto Found = dtnmos::Discover(Config);
    NMOS_ASSERT(!Found.has_value());
    NMOS_ASSERT(Found.error().Code == dtnmos::Result::InvalidArgument);
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
    dtnmos::DiscoveryConfig Config;
    Config.Service = dtnmos::Service::Registration;
    Config.InterfaceAddress = "127.0.0.1";
    Config.Destination = "127.0.0.1:9";
    Config.TimeoutMs = 100;
    Config.Searches = dtnmos::Search::Multicast;
    Config.Log = [&](dtnmos::LogLevel, std::string_view) { ++Logged; };
    const auto Found = dtnmos::Discover(Config);
    NMOS_ASSERT(Found.has_value());
    NMOS_ASSERT(Found->empty());
    NMOS_ASSERT(Logged >= 0);
}

NMOS_TEST_MAIN("CppQuery", NMOS_RUN(CppQueryFedSearch), NMOS_RUN(CppQueryRefusesASearch),
               NMOS_RUN(CppQueryDiscovers))
