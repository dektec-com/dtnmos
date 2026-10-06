// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The C++ API: results, errors, IDs and logging, which every C++ header uses
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos is a wrapper over its C API that is compiled into the program,
// header only, with the program's compiler; the C library stays the one implementation.
// A program sees C++ types only: every struct of the C API has a value type that owns
// what it holds, and every enum an enum class with the same values. A call that can fail
// returns an Expected, which holds its value or the Error of the call, and the compiler
// warns when a program ignores one. The names are those of the C API without the prefix,
// e.g. dtnmos::Node::AddSender() for DtNmosNode_AddSender().
//
// It needs C++23, for std::expected: GCC 12, Clang 16 or Visual Studio 2022 17.3 or
// newer. Link the CMake target dtnmos::cpp, which asks for it.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <version>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
    #error "dtnmos.hpp needs C++23 with std::expected: GCC 12, Clang 16 or VS 2022 17.3"
#endif

#include <array>
#include <compare>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "dtnmos.h"

namespace dtnmos
{

class Id;

namespace detail
{
struct Access;
} // namespace detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Results +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A call that can fail returns an Expected<T>: its value, or the Error that says what
// failed. A call without a value returns a Status, an Expected<void>. A program checks
// one with if (!Result) and reads Result.error().Message; one that wants an exception
// calls Result.value(), which throws std::bad_expected_access<Error> on a failure.
//

// What a call of the C API returned: the values of DtNmosResult, with the same numbers.
// Failure and the values after it are failures.
enum class Result : int
{
    Ok = DTNMOS_OK,
    Failure = DTNMOS_E,                          // A failure without a code of its own
    InvalidArgument = DTNMOS_E_INVALID_ARGUMENT, // A parameter is empty or out of range
    Parse = DTNMOS_E_PARSE,                      // An SDP or JSON document is malformed
    NotFound = DTNMOS_E_NOT_FOUND,               // The registry has no such resource
    Ambiguous = DTNMOS_E_AMBIGUOUS,              // A label names more than one resource
    Http = DTNMOS_E_HTTP,          // A request failed, or was answered with an error
    Timeout = DTNMOS_E_TIMEOUT,    // A request got no answer in time
    State = DTNMOS_E_STATE,        // The object is not in a state for the call
    NoMemory = DTNMOS_E_NO_MEMORY, // Not enough memory
    Internal = DTNMOS_E_INTERNAL,  // An error inside the library
    Network = DTNMOS_E_NETWORK,    // A socket could not be opened, or could not send
    BufferTooSmall = DTNMOS_E_BUFFER_TOO_SMALL // A text does not fit the C API's buffer
};

// What failed, and why.
struct Error
{
    Result Code = Result::Internal; // The result of the call that failed
    std::string Message;            // What failed and why, in English
};

// The value of a call, or the Error of the call that did not give it.
template <typename T> using Expected = std::expected<T, Error>;

// The outcome of a call that gives no value.
using Status = Expected<void>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IDs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The ID of an NMOS resource (a node, device, sender, receiver, flow or source): a UUID
// in text form, lower case, e.g. "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01". An Id made
// without one is empty, which is no ID. Ids compare as their texts do, and are keys of a
// std::map or a std::unordered_map.
class Id
{
  public:
    // Makes an ID from a name, so that a resource gets the same ID every time the program
    // runs: the same Namespace and Name always give the same ID (a version 5 UUID, RFC
    // 9562), e.g. from a node's ID and "device/<serial>:<port>". Fails with
    // Result::InvalidArgument when Namespace is empty.
    [[nodiscard]] static Expected<Id> FromName(const Id& Namespace,
                                               std::string_view Name);

    // Reads an ID from its text, a UUID of 36 characters; hex digits in upper case are
    // taken, and written in lower case. Fails with Result::InvalidArgument for any other
    // text, "" included.
    [[nodiscard]] static Expected<Id> FromText(std::string_view Text);

    // Makes an empty ID.
    Id() = default;

    // Returns whether this is no ID.
    bool IsEmpty() const;

    // Returns the ID's text, "" for no ID.
    std::string ToString() const;

    friend auto operator<=>(const Id&, const Id&) = default;
    friend bool operator==(const Id&, const Id&) = default;

  private:
    friend struct detail::Access;
    std::array<char, sizeof(DtNmosId::Text)> Uuid{}; // The text and its null, zero after
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Logging +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// How important a log message is: the values of DtNmosLogLevel.
enum class LogLevel : int
{
    Debug = DTNMOS_LOG_DEBUG,
    Info = DTNMOS_LOG_INFO,
    Warning = DTNMOS_LOG_WARNING,
    Error = DTNMOS_LOG_ERROR
};

// A program's function that receives the library's log messages. Message is valid only
// during the call. It is called on the library's threads; an exception it throws is
// dropped.
using LogFunction = std::function<void(LogLevel Level, std::string_view Message)>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The library +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// A version of the library.
struct Version
{
    int Major = 0;
    int Minor = 0;
    int Patch = 0;
};

// Returns the version of the library the program runs with, which may differ from the
// version it was built against (DTNMOS_VERSION in dtnmos_version.h).
Version GetVersion();

// Returns the name of a result, e.g. "DTNMOS_E_NOT_FOUND", for messages.
std::string_view Name(Result Code);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Not for programs: what the C++ headers of dtnmos use to convert between their types and
// those of the C API, and to call the C API.
//

namespace detail
{

// Asserts that Field is the last field of the C struct Type, as the conversion of Type
// knows it: fewer than alignof(Type) bytes follow it. A field added at the end of the
// struct, as the C API adds them, fails the build until the conversion has it.
#define DTNMOS_DETAIL_LAST_FIELD(Type, Field)                                            \
    static_assert(                                                                       \
        sizeof(Type) - offsetof(Type, Field) - sizeof(Type::Field) < alignof(Type),      \
        #Type " has a field after " #Field ", which the C++ API does not have")

// Converts the C struct inside a type of the wrapper to and from that type.
struct Access
{
    static Id FromNative(const DtNmosId& Native);
    static DtNmosId ToNative(const Id& Value);
    static std::string_view View(const Id& Value);
};

// Converts a result of the C API. Each value of DtNmosResult has a case, so that the
// compiler warns of one it does not have; a value of a newer library is passed on.
inline Result FromNative(DtNmosResult Native)
{
    switch (Native)
    {
    case DTNMOS_OK:
        return Result::Ok;
    case DTNMOS_E:
        return Result::Failure;
    case DTNMOS_E_INVALID_ARGUMENT:
        return Result::InvalidArgument;
    case DTNMOS_E_PARSE:
        return Result::Parse;
    case DTNMOS_E_NOT_FOUND:
        return Result::NotFound;
    case DTNMOS_E_AMBIGUOUS:
        return Result::Ambiguous;
    case DTNMOS_E_HTTP:
        return Result::Http;
    case DTNMOS_E_TIMEOUT:
        return Result::Timeout;
    case DTNMOS_E_STATE:
        return Result::State;
    case DTNMOS_E_NO_MEMORY:
        return Result::NoMemory;
    case DTNMOS_E_INTERNAL:
        return Result::Internal;
    case DTNMOS_E_NETWORK:
        return Result::Network;
    case DTNMOS_E_BUFFER_TOO_SMALL:
        return Result::BufferTooSmall;
    }
    return static_cast<Result>(Native);
}

// Converts a log level of the C API, as FromNative(DtNmosResult) does.
inline LogLevel FromNative(DtNmosLogLevel Native)
{
    switch (Native)
    {
    case DTNMOS_LOG_DEBUG:
        return LogLevel::Debug;
    case DTNMOS_LOG_INFO:
        return LogLevel::Info;
    case DTNMOS_LOG_WARNING:
        return LogLevel::Warning;
    case DTNMOS_LOG_ERROR:
        return LogLevel::Error;
    }
    return static_cast<LogLevel>(Native);
}

// Returns the text of a char array of a C struct, up to its null or its end.
template <std::size_t N> std::string FromArray(const char (&Field)[N])
{
    std::size_t Length = 0;
    while (Length < N && Field[Length] != '\0')
    {
        ++Length;
    }
    return std::string(Field, Length);
}

// Returns a text of the C API as a std::string, "" for NULL.
inline std::string FromNative(const char* Native)
{
    return Native != nullptr ? std::string(Native) : std::string();
}

// Converts a result to the C API.
inline DtNmosResult ToNative(Result Code)
{
    return static_cast<DtNmosResult>(Code);
}

// Converts a log level to the C API.
inline DtNmosLogLevel ToNative(LogLevel Level)
{
    return static_cast<DtNmosLogLevel>(Level);
}

// Returns the Error of the C call that just failed with Native: its result, and the
// message it left on this thread.
inline Error LastError(DtNmosResult Native)
{
    return Error{FromNative(Native), FromNative(DtNmos_GetLastError())};
}

// Returns the Status of a C call's result, with the message of a failure.
[[nodiscard]] inline Status Check(DtNmosResult Native)
{
    if (Native == DTNMOS_OK)
    {
        return {};
    }
    return std::unexpected(LastError(Native));
}

// Copies Text, with its null, into the char array Field of a C struct. Fails with
// Result::InvalidArgument, and leaves Field as it was, when Text does not fit, rather
// than cutting it; What names the field in the message.
template <std::size_t N>
[[nodiscard]] Status CopyText(char (&Field)[N], std::string_view Text,
                              std::string_view What)
{
    if (Text.size() >= N)
    {
        return std::unexpected(
            Error{Result::InvalidArgument, std::string(What) + " is longer than " +
                                               std::to_string(N - 1) +
                                               " characters: " + std::string(Text)});
    }
    Text.copy(Field, Text.size());
    Field[Text.size()] = '\0';
    return {};
}

// The message of an exception that is no std::exception.
inline constexpr const char* UnknownException =
    "The program's callback threw an exception.";

// Calls Function, the program's, from a callback of the C library, which no exception
// may leave. Function returns a Status or nothing. An exception it throws becomes an
// Error with Code and the exception's what(). Built without exceptions, it calls
// Function alone.
template <typename F> [[nodiscard]] Status Guard(Result Code, F&& Function) noexcept
{
#if defined(__cpp_exceptions)
    try
    {
        if constexpr (std::is_void_v<std::invoke_result_t<F>>)
        {
            std::forward<F>(Function)();
            return {};
        }
        else
        {
            return std::forward<F>(Function)();
        }
    }
    catch (const std::exception& Exception)
    {
        return std::unexpected(Error{Code, Exception.what()});
    }
    catch (...)
    {
        return std::unexpected(Error{Code, UnknownException});
    }
#else
    (void)Code;
    if constexpr (std::is_void_v<std::invoke_result_t<F>>)
    {
        std::forward<F>(Function)();
        return {};
    }
    else
    {
        return std::forward<F>(Function)();
    }
#endif
}

// The C log callback of a LogFunction, which User points to. An exception the function
// throws is dropped, as the C callback returns nothing.
inline void LogTrampoline(void* User, DtNmosLogLevel Level, const char* Message) noexcept
{
    const LogFunction& Log = *static_cast<const LogFunction*>(User);
    (void)Guard(Result::Internal,
                [&] { Log(FromNative(Level), Message != nullptr ? Message : ""); });
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Access::FromNative -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An ID the library gives has its null; one without is read up to its last character.
//
inline Id Access::FromNative(const DtNmosId& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosId, Text);
    Id Value;
    for (std::size_t i = 0; i + 1 < Value.Uuid.size() && Native.Text[i] != '\0'; ++i)
    {
        Value.Uuid[i] = Native.Text[i];
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Access::ToNative -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline DtNmosId Access::ToNative(const Id& Value)
{
    DtNmosId Native{};
    for (std::size_t i = 0; i < Value.Uuid.size(); ++i)
    {
        Native.Text[i] = Value.Uuid[i];
    }
    return Native;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Access::View -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline std::string_view Access::View(const Id& Value)
{
    return std::string_view(Value.Uuid.data());
}

} // namespace detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Id::FromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<Id> Id::FromName(const Id& Namespace, std::string_view Name)
{
    const DtNmosId NativeNamespace = detail::Access::ToNative(Namespace);
    const std::string NameText(Name);
    DtNmosId Made{};
    const Status Checked =
        detail::Check(DtNmosId_FromName(&NativeNamespace, NameText.c_str(), &Made));
    if (!Checked)
    {
        return std::unexpected(Checked.error());
    }
    return detail::Access::FromNative(Made);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Id::FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A UUID is 8, 4, 4, 4 and 12 hex digits, with a hyphen between each group.
//
inline Expected<Id> Id::FromText(std::string_view Text)
{
    Id Value;
    bool Valid = Text.size() + 1 == Value.Uuid.size();
    for (std::size_t i = 0; Valid && i < Text.size(); ++i)
    {
        const char Given = Text[i];
        const char c =
            Given >= 'A' && Given <= 'F' ? static_cast<char>(Given - 'A' + 'a') : Given;
        const bool Hyphen = i == 8 || i == 13 || i == 18 || i == 23;
        Valid = Hyphen ? c == '-' : (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        Value.Uuid[i] = c;
    }
    if (!Valid)
    {
        return std::unexpected(
            Error{Result::InvalidArgument, "Not a UUID: \"" + std::string(Text) + "\"."});
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Id::IsEmpty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline bool Id::IsEmpty() const
{
    return Uuid[0] == '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Id::ToString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline std::string Id::ToString() const
{
    return std::string(detail::Access::View(*this));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Version GetVersion()
{
    Version Running;
    DtNmos_Version(&Running.Major, &Running.Minor, &Running.Patch);
    return Running;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline std::string_view Name(Result Code)
{
    return DtNmosResult_Name(detail::ToNative(Code));
}

} // namespace dtnmos

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- std::hash<dtnmos::Id> -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Hashes an ID as its text, so that it is a key of a std::unordered_map.
//
template <> struct std::hash<dtnmos::Id>
{
    std::size_t operator()(const dtnmos::Id& Value) const noexcept
    {
        return std::hash<std::string_view>{}(dtnmos::detail::Access::View(Value));
    }
};
