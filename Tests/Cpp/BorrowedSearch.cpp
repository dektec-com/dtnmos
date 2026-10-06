// #*#*#*#*#*#*#*#*#*#*#*#*# BorrowedSearch.cpp *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Destroys a search while a node borrows it, which must end the program
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The test dtnmos.CppBorrowedSearch expects this program to fail: a RegistrySearch
// destroyed while a node borrows it calls std::terminate(). It returns 0 only when the
// search was destroyed without that.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

#include "dtnmos_node.hpp"

#if defined(_MSC_VER)
    #include <crtdbg.h>
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main()
{
#if defined(_MSC_VER)
    // abort() reports to stderr, without a dialog that nobody would answer.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    auto Opened = DtNmos::RegistrySearch::Open(
        {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = true});
    if (!Opened)
    {
        return 0;
    }
    std::optional<DtNmos::RegistrySearch> Search;
    Search.emplace(std::move(*Opened));
    DtNmos::NodeConfig Config;
    Config.Id = *DtNmos::Id::FromText("dddddddd-0000-4000-8000-000000000001");
    Config.ApiHost = "192.168.1.5";
    Config.Http = [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    { return DtNmos::HttpResponse{200, {}, "", ""}; };
    Config.Search = &*Search;
    auto Node = DtNmos::Node::Open(Config);
    if (!Node)
    {
        return 0;
    }
    std::puts("destroying the search the node borrows");
    std::fflush(stdout);
    Search.reset();
    return 0;
}
