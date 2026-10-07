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
// Id::FromName() gives the same ID as DtNmosId_FromName() for the same namespace and
// name, and gives it again on a second call. An empty namespace is refused with
// Result::InvalidArgument and a message.
//
NMOS_TEST(CppIdFromName)
{
    const auto Namespace = DtNmos::Id::FromText(NAMESPACE_ID);
    NMOS_ASSERT(Namespace.has_value());
    const auto Made = DtNmos::Id::FromName(*Namespace, "device/2110000076:1");
    NMOS_ASSERT(Made.has_value());

    const DtNmosId NativeNamespace = {NAMESPACE_ID};
    DtNmosId Native = {};
    NMOS_ASSERT(DtNmosId_FromName(&NativeNamespace, "device/2110000076:1", &Native) ==
                DTNMOS_OK);
    NMOS_ASSERT(Made->ToString() == Native.Text);
    NMOS_ASSERT(*Made == *DtNmos::Id::FromName(*Namespace, "device/2110000076:1"));

    const auto Refused = DtNmos::Id::FromName(DtNmos::Id(), "device");
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::InvalidArgument);
    NMOS_ASSERT(!Refused.error().Message.empty());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppIdFromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Id::FromText() reads a UUID, and turns upper-case hex into lower case. It refuses "",
// a UUID that is one character too short or too long, one without its hyphens, and one
// with a letter that is not hex.
//
NMOS_TEST(CppIdFromText)
{
    const auto Lower = DtNmos::Id::FromText("5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01");
    NMOS_ASSERT(Lower.has_value());
    NMOS_ASSERT(Lower->ToString() == "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01");
    const auto Upper = DtNmos::Id::FromText("5F38F7A2-1D91-5E0C-8A2B-6E1C2F7D9A01");
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
        const auto Read = DtNmos::Id::FromText(Text);
        NMOS_EXPECT(!Read.has_value());
        NMOS_EXPECT(!Read.has_value() &&
                    Read.error().Code == DtNmos::Result::InvalidArgument);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppIdIsAKey -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An empty Id is no ID, and orders before any other. Ids order as their texts do. They
// are keys of a std::unordered_set, which holds two of three Ids when two are equal, and
// of a std::map, which lists the smaller first.
//
NMOS_TEST(CppIdIsAKey)
{
    const DtNmos::Id Empty;
    NMOS_ASSERT(Empty.IsEmpty());
    NMOS_ASSERT(Empty.ToString().empty());
    const DtNmos::Id A = *DtNmos::Id::FromText("aaaaaaaa-0000-4000-8000-000000000001");
    const DtNmos::Id B = *DtNmos::Id::FromText("bbbbbbbb-0000-4000-8000-000000000001");
    NMOS_ASSERT(!A.IsEmpty());
    NMOS_ASSERT(Empty < A);
    NMOS_ASSERT(A < B);
    NMOS_ASSERT(A != B);

    std::unordered_set<DtNmos::Id> Set = {A, B, A};
    NMOS_ASSERT_EQ(Set.size(), 2);
    std::map<DtNmos::Id, int> Map = {{B, 2}, {A, 1}};
    NMOS_ASSERT(Map.begin()->first == A);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppResultsConvert -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every result converts to its C value and back. It keeps the C value's number, and has
// the C value's name. A log level converts to its C value and back as well.
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
        const DtNmos::Result Code = DtNmos::Detail::FromNative(Native);
        NMOS_EXPECT(static_cast<int>(Code) == static_cast<int>(Native));
        NMOS_EXPECT(DtNmos::Detail::ToNative(Code) == Native);
        NMOS_EXPECT(DtNmos::Name(Code) == DtNmosResult_Name(Native));
    }
    NMOS_ASSERT(DtNmos::Name(DtNmos::Result::NotFound) == "DTNMOS_E_NOT_FOUND");
    const DtNmosLogLevel Levels[] = {DTNMOS_LOG_DEBUG, DTNMOS_LOG_INFO,
                                     DTNMOS_LOG_WARNING, DTNMOS_LOG_ERROR};
    for (const DtNmosLogLevel Native : Levels)
    {
        NMOS_EXPECT(DtNmos::Detail::ToNative(DtNmos::Detail::FromNative(Native)) ==
                    Native);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppCheck -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Check() turns a C result that is a failure into an Error. The Error has the result and
// the message that the call left. A result of DTNMOS_OK gives a Status without an Error.
// GetVersion() gives the version of the library.
//
NMOS_TEST(CppCheck)
{
    NMOS_ASSERT(DtNmos::Detail::Check(DTNMOS_OK).has_value());
    const DtNmos::Status Failed =
        DtNmos::Detail::Check(DtNmos_SetLastError(DTNMOS_E_TIMEOUT, "no answer in time"));
    NMOS_ASSERT(!Failed.has_value());
    NMOS_ASSERT(Failed.error().Code == DtNmos::Result::Timeout);
    NMOS_ASSERT_STR(Failed.error().Message.c_str(), "no answer in time");

    const DtNmos::Version Running = DtNmos::GetVersion();
    NMOS_ASSERT_EQ(Running.Major, DTNMOS_VERSION_MAJOR);
    NMOS_ASSERT_EQ(Running.Minor, DTNMOS_VERSION_MINOR);
    NMOS_ASSERT_EQ(Running.Patch, DTNMOS_VERSION_PATCH);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppCopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// CopyText() copies a text of 7 characters, with its null, into a char array of 8. It
// refuses a text of 8 characters with Result::InvalidArgument, names the field in the
// message, and leaves the array as it was.
//
NMOS_TEST(CppCopyText)
{
    char Field[8] = "old";
    NMOS_ASSERT(DtNmos::Detail::CopyText(Field, "1234567", "Field").has_value());
    NMOS_ASSERT_STR(Field, "1234567");
    const DtNmos::Status Refused = DtNmos::Detail::CopyText(Field, "12345678", "Field");
    NMOS_ASSERT(!Refused.has_value());
    NMOS_ASSERT(Refused.error().Code == DtNmos::Result::InvalidArgument);
    NMOS_ASSERT(Refused.error().Message.find("Field") != std::string::npos);
    NMOS_ASSERT_STR(Field, "1234567");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppLog -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C log callback of a LogFunction passes the level and the message on. A NULL
// message arrives as "".
//
NMOS_TEST(CppLog)
{
    std::vector<std::pair<DtNmos::LogLevel, std::string>> Logged;
    const DtNmos::LogFunction Log = [&](DtNmos::LogLevel Level, std::string_view Message)
    { Logged.emplace_back(Level, std::string(Message)); };
    DtNmos::Detail::LogTrampoline(const_cast<DtNmos::LogFunction*>(&Log),
                                  DTNMOS_LOG_WARNING, "the registry is gone");
    DtNmos::Detail::LogTrampoline(const_cast<DtNmos::LogFunction*>(&Log), DTNMOS_LOG_INFO,
                                  nullptr);
    NMOS_ASSERT_EQ(Logged.size(), 2);
    NMOS_ASSERT(Logged[0].first == DtNmos::LogLevel::Warning);
    NMOS_ASSERT_STR(Logged[0].second.c_str(), "the registry is gone");
    NMOS_ASSERT(Logged[1].first == DtNmos::LogLevel::Info);
    NMOS_ASSERT_STR(Logged[1].second.c_str(), "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppFailures -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A program makes a failed Expected or Status with DtNmos::Unexpected or, in place,
// with DtNmos::Unexpect, and catches what value() of a failure throws as a
// DtNmos::BadExpectedAccess, whose error() is the Error; the same names under C++20 and
// C++23.
//
NMOS_TEST(CppFailures)
{
    const DtNmos::Status Refused =
        DtNmos::Unexpected(DtNmos::Error{DtNmos::Result::State, "not open"});
    NMOS_ASSERT(!Refused && Refused.error().Code == DtNmos::Result::State);
    const DtNmos::Status InPlace(DtNmos::Unexpect,
                                 DtNmos::Error{DtNmos::Result::Timeout, "no answer"});
    NMOS_ASSERT(!InPlace && InPlace.error().Message == "no answer");
    const DtNmos::Expected<DtNmos::Id> NoId(DtNmos::Unexpect, DtNmos::Error{});
    NMOS_ASSERT(!NoId && NoId.error().Code == DtNmos::Result::Internal);
#if defined(__cpp_exceptions)
    bool Caught = false;
    try
    {
        (void)NoId.value();
    }
    catch (const DtNmos::BadExpectedAccess& Access)
    {
        Caught = Access.error().Code == DtNmos::Result::Internal;
    }
    NMOS_ASSERT(Caught);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppGuard -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Guard() passes on the Status of the function it calls. With exceptions, it catches what
// the function throws: a std::exception becomes an Error with the code given and its
// what(), anything else an Error with a message of its own. A log function that throws
// is called without effect.
//
NMOS_TEST(CppGuard)
{
    const DtNmos::Status Passed = DtNmos::Detail::Guard(
        DtNmos::Result::Internal, []() -> DtNmos::Status
        { return DtNmos::Unexpected(DtNmos::Error{DtNmos::Result::State, "not open"}); });
    NMOS_ASSERT(!Passed.has_value());
    NMOS_ASSERT(Passed.error().Code == DtNmos::Result::State);
    NMOS_ASSERT(DtNmos::Detail::Guard(DtNmos::Result::Internal, [] {}).has_value());
#if defined(__cpp_exceptions)
    const DtNmos::Status Thrown = DtNmos::Detail::Guard(
        DtNmos::Result::Http, [] { throw std::runtime_error("the card refused it"); });
    NMOS_ASSERT(!Thrown.has_value());
    NMOS_ASSERT(Thrown.error().Code == DtNmos::Result::Http);
    NMOS_ASSERT_STR(Thrown.error().Message.c_str(), "the card refused it");
    const DtNmos::Status Other =
        DtNmos::Detail::Guard(DtNmos::Result::Internal, [] { throw 42; });
    NMOS_ASSERT(!Other.has_value());
    NMOS_ASSERT_STR(Other.error().Message.c_str(), DtNmos::Detail::UnknownException);

    const DtNmos::LogFunction Throws = [](DtNmos::LogLevel, std::string_view)
    { throw std::runtime_error("the log is full"); };
    DtNmos::Detail::LogTrampoline(const_cast<DtNmos::LogFunction*>(&Throws),
                                  DTNMOS_LOG_ERROR, "dropped");
#endif
}

NMOS_TEST_MAIN("Cpp", NMOS_RUN(CppIdFromName), NMOS_RUN(CppIdFromText),
               NMOS_RUN(CppIdIsAKey), NMOS_RUN(CppResultsConvert), NMOS_RUN(CppCheck),
               NMOS_RUN(CppCopyText), NMOS_RUN(CppLog), NMOS_RUN(CppFailures),
               NMOS_RUN(CppGuard))
