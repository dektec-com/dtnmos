// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestCpp.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "NmosTest.h"
#include "dtnmos.hpp"

#define NAMESPACE_ID "bbbbbbbb-0000-4000-8000-000000000001"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppIdFromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Id::FromName() gives the ID that DtNmosId_FromName() gives for the same namespace and
// name, the same one each time; an empty namespace is refused with the C API's message.
//
NMOS_TEST(CppIdFromName)
{
    const auto Namespace = dtnmos::Id::FromText(NAMESPACE_ID);
    NMOS_ASSERT(Namespace.has_value());
    const auto Made = dtnmos::Id::FromName(*Namespace, "device/2110000076:1");
    NMOS_ASSERT(Made.has_value());

    const DtNmosId NativeNamespace = {NAMESPACE_ID};
    DtNmosId Native = {};
    NMOS_ASSERT(DtNmosId_FromName(&NativeNamespace, "device/2110000076:1", &Native) ==
                DTNMOS_OK);
    NMOS_ASSERT(Made->ToString() == Native.Text);
    NMOS_ASSERT(*Made == *dtnmos::Id::FromName(*Namespace, "device/2110000076:1"));

    const auto Refused = dtnmos::Id::FromName(dtnmos::Id(), "device");
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == dtnmos::Result::InvalidArgument);
    NMOS_ASSERT(!Refused.error().Message.empty());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppIdFromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Id::FromText() takes a UUID, and writes one in upper case in lower case; it refuses "",
// a UUID one character short or long, one without its hyphens, and one that is not hex.
//
NMOS_TEST(CppIdFromText)
{
    const auto Lower = dtnmos::Id::FromText("5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01");
    NMOS_ASSERT(Lower.has_value());
    NMOS_ASSERT(Lower->ToString() == "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01");
    const auto Upper = dtnmos::Id::FromText("5F38F7A2-1D91-5E0C-8A2B-6E1C2F7D9A01");
    NMOS_ASSERT(Upper.has_value());
    NMOS_ASSERT(*Upper == *Lower);

    const char* const Refused[] = {
        "",
        "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a0",
        "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a012",
        "5f38f7a2d1d91-5e0c-8a2b-6e1c2f7d9a01",
        "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9g01",
    };
    for (const char* Text : Refused)
    {
        const auto Read = dtnmos::Id::FromText(Text);
        NMOS_EXPECT(!Read.has_value());
        NMOS_EXPECT(!Read.has_value() &&
                    Read.error().Code == dtnmos::Result::InvalidArgument);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppIdIsAKey -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An empty Id is no ID and orders first; Ids order as their texts do, and are keys of a
// std::map and a std::unordered_set, two of three alike.
//
NMOS_TEST(CppIdIsAKey)
{
    const dtnmos::Id Empty;
    NMOS_ASSERT(Empty.IsEmpty());
    NMOS_ASSERT(Empty.ToString().empty());
    const dtnmos::Id A = *dtnmos::Id::FromText("aaaaaaaa-0000-4000-8000-000000000001");
    const dtnmos::Id B = *dtnmos::Id::FromText("bbbbbbbb-0000-4000-8000-000000000001");
    NMOS_ASSERT(!A.IsEmpty());
    NMOS_ASSERT(Empty < A);
    NMOS_ASSERT(A < B);
    NMOS_ASSERT(A != B);

    std::unordered_set<dtnmos::Id> Set = {A, B, A};
    NMOS_ASSERT_EQ(Set.size(), 2);
    std::map<dtnmos::Id, int> Map = {{B, 2}, {A, 1}};
    NMOS_ASSERT(Map.begin()->first == A);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppResultsConvert -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every result converts to its C value and back, with the C value's number and name; a
// log level as well.
//
NMOS_TEST(CppResultsConvert)
{
    const DtNmosResult Natives[] = {
        DTNMOS_OK,
        DTNMOS_E,
        DTNMOS_E_INVALID_ARGUMENT,
        DTNMOS_E_PARSE,
        DTNMOS_E_NOT_FOUND,
        DTNMOS_E_AMBIGUOUS,
        DTNMOS_E_HTTP,
        DTNMOS_E_TIMEOUT,
        DTNMOS_E_STATE,
        DTNMOS_E_NO_MEMORY,
        DTNMOS_E_INTERNAL,
        DTNMOS_E_NETWORK,
        DTNMOS_E_BUFFER_TOO_SMALL,
    };
    for (const DtNmosResult Native : Natives)
    {
        const dtnmos::Result Code = dtnmos::detail::FromNative(Native);
        NMOS_EXPECT(static_cast<int>(Code) == static_cast<int>(Native));
        NMOS_EXPECT(dtnmos::detail::ToNative(Code) == Native);
        NMOS_EXPECT(dtnmos::Name(Code) == DtNmosResult_Name(Native));
    }
    NMOS_ASSERT(dtnmos::Name(dtnmos::Result::NotFound) == "DTNMOS_E_NOT_FOUND");
    const DtNmosLogLevel Levels[] = {DTNMOS_LOG_DEBUG, DTNMOS_LOG_INFO,
                                     DTNMOS_LOG_WARNING, DTNMOS_LOG_ERROR};
    for (const DtNmosLogLevel Native : Levels)
    {
        NMOS_EXPECT(dtnmos::detail::ToNative(dtnmos::detail::FromNative(Native)) ==
                    Native);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppCheck -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A C call that fails gives an Error with its result and the message it left; one that
// succeeds, a Status that holds no Error. The version is the library's.
//
NMOS_TEST(CppCheck)
{
    NMOS_ASSERT(dtnmos::detail::Check(DTNMOS_OK).has_value());
    const dtnmos::Status Failed =
        dtnmos::detail::Check(DtNmos_SetLastError(DTNMOS_E_TIMEOUT, "no answer in time"));
    NMOS_ASSERT(!Failed.has_value());
    NMOS_ASSERT(Failed.error().Code == dtnmos::Result::Timeout);
    NMOS_ASSERT_STR(Failed.error().Message.c_str(), "no answer in time");

    const dtnmos::Version Running = dtnmos::GetVersion();
    NMOS_ASSERT_EQ(Running.Major, DTNMOS_VERSION_MAJOR);
    NMOS_ASSERT_EQ(Running.Minor, DTNMOS_VERSION_MINOR);
    NMOS_ASSERT_EQ(Running.Patch, DTNMOS_VERSION_PATCH);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppCopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A text that fits a char array of 8 is copied with its null; one of 8 characters is
// refused, and leaves the array as it was.
//
NMOS_TEST(CppCopyText)
{
    char Field[8] = "old";
    NMOS_ASSERT(dtnmos::detail::CopyText(Field, "1234567", "Field").has_value());
    NMOS_ASSERT_STR(Field, "1234567");
    const dtnmos::Status Refused = dtnmos::detail::CopyText(Field, "12345678", "Field");
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == dtnmos::Result::InvalidArgument);
    NMOS_ASSERT(Refused.error().Message.find("Field") != std::string::npos);
    NMOS_ASSERT_STR(Field, "1234567");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppLog -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C log callback of a LogFunction passes the level and the message on, "" for NULL.
//
NMOS_TEST(CppLog)
{
    std::vector<std::pair<dtnmos::LogLevel, std::string>> Logged;
    const dtnmos::LogFunction Log = [&](dtnmos::LogLevel Level, std::string_view Message)
    { Logged.emplace_back(Level, std::string(Message)); };
    dtnmos::detail::LogTrampoline(const_cast<dtnmos::LogFunction*>(&Log),
                                  DTNMOS_LOG_WARNING, "the registry is gone");
    dtnmos::detail::LogTrampoline(const_cast<dtnmos::LogFunction*>(&Log), DTNMOS_LOG_INFO,
                                  nullptr);
    NMOS_ASSERT_EQ(Logged.size(), 2);
    NMOS_ASSERT(Logged[0].first == dtnmos::LogLevel::Warning);
    NMOS_ASSERT_STR(Logged[0].second.c_str(), "the registry is gone");
    NMOS_ASSERT(Logged[1].first == dtnmos::LogLevel::Info);
    NMOS_ASSERT_STR(Logged[1].second.c_str(), "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppGuard -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Guard() passes on the Status of the function it calls. With exceptions, one that the
// function throws becomes an Error with the code given: a std::exception with its
// what(), anything else with a message of its own; a log function that throws is
// dropped.
//
NMOS_TEST(CppGuard)
{
    const dtnmos::Status Passed = dtnmos::detail::Guard(
        dtnmos::Result::Internal, []() -> dtnmos::Status
        { return std::unexpected(dtnmos::Error{dtnmos::Result::State, "not open"}); });
    NMOS_ASSERT(!Passed.has_value());
    NMOS_ASSERT(Passed.error().Code == dtnmos::Result::State);
    NMOS_ASSERT(dtnmos::detail::Guard(dtnmos::Result::Internal, [] {}).has_value());
#if defined(__cpp_exceptions)
    const dtnmos::Status Thrown = dtnmos::detail::Guard(
        dtnmos::Result::Http, [] { throw std::runtime_error("the card refused it"); });
    NMOS_ASSERT(!Thrown.has_value());
    NMOS_ASSERT(Thrown.error().Code == dtnmos::Result::Http);
    NMOS_ASSERT_STR(Thrown.error().Message.c_str(), "the card refused it");
    const dtnmos::Status Other =
        dtnmos::detail::Guard(dtnmos::Result::Internal, [] { throw 42; });
    NMOS_ASSERT(!Other.has_value());
    NMOS_ASSERT_STR(Other.error().Message.c_str(), dtnmos::detail::UnknownException);

    const dtnmos::LogFunction Throws = [](dtnmos::LogLevel, std::string_view)
    { throw std::runtime_error("the log is full"); };
    dtnmos::detail::LogTrampoline(const_cast<dtnmos::LogFunction*>(&Throws),
                                  DTNMOS_LOG_ERROR, "dropped");
#endif
}

NMOS_TEST_MAIN("Cpp", NMOS_RUN(CppIdFromName), NMOS_RUN(CppIdFromText),
               NMOS_RUN(CppIdIsAKey), NMOS_RUN(CppResultsConvert), NMOS_RUN(CppCheck),
               NMOS_RUN(CppCopyText), NMOS_RUN(CppLog), NMOS_RUN(CppGuard))
