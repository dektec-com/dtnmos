// #*#*#*#*#*#*#*#*#*#*#*#*# BorrowedSearch.cpp *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Destroys a search while a node borrows it, which must end the program
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The test dtnmos.CppBorrowedSearch: a RegistrySearch destroyed while a node borrows it
// calls std::terminate(). The program's terminate handler ends it with 0; it returns 1
// when the search was destroyed without that, or when it could not set up the node.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <utility>

#include "dtnmos_node.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Terminated -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The terminate handler: what the test expects, so the program ends with 0, without the
// abort() of the default handler, which CTest would count as a failure.
//
[[noreturn]] static void Terminated()
{
    std::puts("terminated, as expected");
    std::fflush(stdout);
    std::_Exit(0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main()
{
    std::set_terminate(Terminated);
    auto Opened = DtNmos::RegistrySearch::Open(
        {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = true});
    if (!Opened)
    {
        return 1;
    }
    auto Search = std::make_unique<DtNmos::RegistrySearch>(std::move(*Opened));
    DtNmos::NodeConfig Config;
    Config.Id = *DtNmos::Id::FromText("dddddddd-0000-4000-8000-000000000001");
    Config.ApiHost = "192.168.1.5";
    Config.Http = [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    { return DtNmos::HttpResponse{200, {}, "", ""}; };
    Config.Search = Search.get();
    auto Node = DtNmos::Node::Open(Config);
    if (!Node)
    {
        return 1;
    }
    std::puts("destroying the search the node borrows");
    std::fflush(stdout);
    Search.reset();
    std::puts("the search was destroyed while borrowed, and nothing stopped it");
    return 1;
}
