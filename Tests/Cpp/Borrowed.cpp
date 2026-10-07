// #*#*#*#*#*#*#*#*#*#*#*#*#*#* Borrowed.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Destroys a borrowed search or query, which must end the program
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The program behind the tests dtnmos.CppBorrowedSearch and dtnmos.CppBorrowedQuery.
// Given "search", it destroys a RegistrySearch while a node borrows it. Given "query", it
// destroys a Query while a subscription borrows it. Either must call std::terminate().
// The program's own terminate handler then ends it with 0, which the test expects. The
// program returns 1 when the object was destroyed without std::terminate(), or when it
// could not set up the borrower.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "dtnmos_node.hpp"
#include "dtnmos_query.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Terminated -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Ends the program with 0 when std::terminate() is called, as the test expects. The
// default handler calls abort(), which CTest counts as a failure.
//
[[noreturn]] static void Terminated()
{
    std::puts("terminated, as expected");
    std::fflush(stdout);
    std::_Exit(0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NotSetUp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints why What could not be set up, and returns 1.
//
static int NotSetUp(const char* What, const DtNmos::Error& Failure)
{
    std::printf("%s: %s\n", What, Failure.Message.c_str());
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Destroyed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Destroys Borrowed, which must call std::terminate(). Returns 1 when it does not.
//
template <typename T> static int Destroyed(std::unique_ptr<T>& Borrowed, const char* What)
{
    std::printf("destroying the %s while it is borrowed\n", What);
    std::fflush(stdout);
    Borrowed.reset();
    std::printf("the %s was destroyed while borrowed, and nothing stopped it\n", What);
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BorrowSearch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Lets a node borrow a fed search, and then destroys the search.
//
static int BorrowSearch()
{
    auto Opened = DtNmos::RegistrySearch::Open(
        {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = true});
    if (!Opened)
    {
        return NotSetUp("RegistrySearch::Open", Opened.error());
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
        return NotSetUp("Node::Open", Node.error());
    }
    return Destroyed(Search, "search");
}

// A WebSocket that never receives a message.
class SilentWebSocket : public DtNmos::WebSocketConnection
{
  public:
    DtNmos::Expected<std::string> Receive(std::chrono::milliseconds) override
    {
        return std::unexpected(DtNmos::Error{DtNmos::Result::Timeout, "no message"});
    }
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BorrowQuery -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Lets a subscription borrow a query, and then destroys the query. The registry of the
// query answers every request with a subscription.
//
static int BorrowQuery()
{
    DtNmos::QueryConfig Config;
    Config.RegistryUrl = "http://registry.test";
    Config.Http = [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    {
        return DtNmos::HttpResponse{
            201,
            {},
            "application/json",
            "{\"id\": \"1\", \"ws_href\": \"ws://registry.test/ws\"}"};
    };
    auto Opened = DtNmos::Query::Open(Config);
    if (!Opened)
    {
        return NotSetUp("Query::Open", Opened.error());
    }
    auto Query = std::make_unique<DtNmos::Query>(std::move(*Opened));
    DtNmos::SubscriptionConfig Wanted;
    Wanted.ResourcePath = "/senders";
    Wanted.WebSocket = [](const std::string&, std::chrono::milliseconds)
        -> DtNmos::Expected<std::unique_ptr<DtNmos::WebSocketConnection>>
    { return std::make_unique<SilentWebSocket>(); };
    Wanted.OnChange = [](const DtNmos::Change&) {};
    auto Subscription = DtNmos::Subscription::Open(*Query, Wanted);
    if (!Subscription)
    {
        return NotSetUp("Subscription::Open", Subscription.error());
    }
    return Destroyed(Query, "query");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    std::set_terminate(Terminated);
    if (Argc == 2 && std::strcmp(Argv[1], "search") == 0)
    {
        return BorrowSearch();
    }
    if (Argc == 2 && std::strcmp(Argv[1], "query") == 0)
    {
        return BorrowQuery();
    }
    std::puts("give search or query");
    return 1;
}
