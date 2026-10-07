// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The C++ API: results, errors, IDs and logging, which every C++ header uses
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API wraps the C API. It consists of headers only, and the program compiles
// them with its own compiler; all the work is done by the C library.
//
// A program sees only C++ types. Each struct of the C API has a C++ struct that owns
// its strings and lists, and each enum has an enum class with the same values. A call
// that can fail returns an Expected, which holds either the value or an Error, and the
// compiler warns when a program ignores it. The names are those of the C API, with the
// prefix as the namespace: DtNmos::Node::AddSender() is DtNmosNode_AddSender().
//
// The headers need C++20. Where the standard library has std::expected, as in C++23,
// Expected is a std::expected; elsewhere it is the C++ API's own, from
// dtnmos_expected.hpp, which behaves the same. So a program compiles every source that
// includes these headers with the same standard. Link the CMake target dtnmos::cpp.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
    #include <expected>
    #define DTNMOS_DETAIL_STD_EXPECTED 1
#else
    #include "dtnmos_expected.hpp"
#endif

#include <array>
#include <compare>
#include <cstddef>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "dtnmos.h"

namespace DtNmos
{

class Id;

namespace Detail
{
struct Access;
} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Results +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A call that can fail returns an Expected<T>. It holds the value when the call
// succeeds, and an Error that says what went wrong when it fails. A call that gives no
// value returns a Status, which is an Expected<void>.
//
// A program checks a result with if (!Result), and then reads Result.error().Message.
// A program that prefers exceptions calls Result.value() instead. That throws a
// BadExpectedAccess when the call failed, and calls std::terminate() in a program built
// without exceptions. A function of the program that fails returns an Unexpected, e.g.
// return DtNmos::Unexpected(DtNmos::Error{DtNmos::Result::State, "not open"});
//
// These are the types of std::expected where the standard library has it, and those of
// dtnmos_expected.hpp elsewhere, under the same names, so that a program uses them the
// same way under C++20 and C++23.
//

// The result codes of the C API, with the same numbers as DtNmosResult. Ok is success;
// Failure and every code after it are failures.
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

// A failure of a call: what went wrong, and why.
struct Error
{
    Result Code = Result::Internal; // The code the call failed with
    std::string Message;            // What went wrong and why, in English
};

#if defined(DTNMOS_DETAIL_STD_EXPECTED)
// What value() of an Expected throws when it holds an Error. Its error() is that Error.
using BadExpectedAccess = std::bad_expected_access<Error>;

// The result of a call that gives a value: the value, or an Error.
template <typename T> using Expected = std::expected<T, Error>;

// Asks a constructor of an Expected to make the Error in place from what follows it,
// e.g. Status(DtNmos::Unexpect, Error{Result::State, "not open"}).
inline constexpr std::unexpect_t Unexpect{};

// An Error that a function returns as its failure, to make an Expected or a Status that
// holds it.
using Unexpected = std::unexpected<Error>;
#else
// What value() of an Expected throws when it holds an Error. Its error() is that Error.
using BadExpectedAccess = Detail::OwnBadExpectedAccess<Error>;

// The result of a call that gives a value: the value, or an Error.
template <typename T> using Expected = Detail::OwnExpected<T, Error>;

// Asks a constructor of an Expected to make the Error in place from what follows it,
// e.g. Status(DtNmos::Unexpect, Error{Result::State, "not open"}).
inline constexpr Detail::OwnUnexpectTag Unexpect{};

// An Error that a function returns as its failure, to make an Expected or a Status that
// holds it.
using Unexpected = Detail::OwnUnexpected<Error>;
#endif

// The result of a call that gives no value: nothing, or an Error.
using Status = Expected<void>;

// Makes the linker of MSVC refuse a program whose sources include these headers under
// two standards, one with std::expected and one without, as their Expecteds differ.
#if defined(_MSC_VER)
    #if defined(DTNMOS_DETAIL_STD_EXPECTED)
        #pragma detect_mismatch("dtnmos_expected", "std")
    #else
        #pragma detect_mismatch("dtnmos_expected", "own")
    #endif
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IDs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The ID of an NMOS resource, such as a node, device, sender, receiver, flow or source.
// It is a UUID in text, in lower case, e.g. "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01". An
// Id that is made without a UUID is empty, and means "no ID". Ids compare as their texts
// do, so they can be keys of a std::map or a std::unordered_map.
class Id
{
  public:
    // Makes an ID from a name, so that a resource gets the same ID each time the program
    // runs. The same Namespace and Name always give the same ID, a version 5 UUID (RFC
    // 9562). A program makes e.g. a device's ID from the node's ID and the name
    // "device/<serial>:<port>". Fails with Result::InvalidArgument when Namespace is
    // empty.
    [[nodiscard]] static Expected<Id> FromName(const Id& Namespace,
                                               std::string_view Name);

    // Reads an ID from Text, which must be a UUID of 36 characters. Hex digits in upper
    // case are accepted, and the ID holds them in lower case. Fails with
    // Result::InvalidArgument for any other text, also for "".
    [[nodiscard]] static Expected<Id> FromText(std::string_view Text);

    // Makes an empty ID, which means "no ID".
    Id() = default;

    // Returns true when the ID is empty.
    bool IsEmpty() const;

    // Returns the ID as text, or "" when it is empty.
    std::string ToString() const;

    friend auto operator<=>(const Id&, const Id&) = default;
    friend bool operator==(const Id&, const Id&) = default;

  private:
    friend struct Detail::Access;
    std::array<char, sizeof(DtNmosId::Text)> Uuid{}; // The text; zeros after it
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Logging +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// How important a log message is. The values are those of DtNmosLogLevel.
enum class LogLevel : int
{
    Debug = DTNMOS_LOG_DEBUG,
    Info = DTNMOS_LOG_INFO,
    Warning = DTNMOS_LOG_WARNING,
    Error = DTNMOS_LOG_ERROR
};

// The program's function that receives the log messages of the library. Message is
// valid only during the call. The library calls the function on its own threads. If the
// function throws an exception, the library ignores it.
using LogFunction = std::function<void(LogLevel Level, std::string_view Message)>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The library +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// A version number of the library, as major, minor and patch.
struct Version
{
    int Major = 0;
    int Minor = 0;
    int Patch = 0;
};

// Returns the version of the library that the program runs with. It may differ from the
// version that the program was built against, which is DTNMOS_VERSION in
// dtnmos_version.h.
Version GetVersion();

// Returns the name of a result code, e.g. "DTNMOS_E_NOT_FOUND", for messages.
std::string_view Name(Result Code);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// This part is not for programs. The C++ headers use it to convert their types to and
// from the types of the C API, and to call the C API.
//

namespace Detail
{

// Checks at compile time that Field is the last field of the C struct Type, that is, that
// fewer than alignof(Type) bytes follow it. A conversion of Type puts this check next to
// the last field it converts. The C API adds new fields at the end of a struct, so a new
// field makes the build fail until the conversion converts it too.
#define DTNMOS_DETAIL_LAST_FIELD(Type, Field)                                            \
    static_assert(                                                                       \
        sizeof(Type) - offsetof(Type, Field) - sizeof(Type::Field) < alignof(Type),      \
        #Type " has a field after " #Field ", which the C++ API does not have")

// Converts an Id to and from the DtNmosId of the C API. It reaches the private text of
// the Id.
struct Access
{
    static Id FromNative(const DtNmosId& Native);
    static DtNmosId ToNative(const Id& Value);
    static std::string_view View(const Id& Value);
};

// Converts a result code of the C API to a Result. The switch has a case for each value
// of DtNmosResult, so the compiler warns when the C API adds a value. A value that a
// newer library returns, and that this header does not know, is kept as it is.
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

// Converts a log level of the C API to a LogLevel, in the way FromNative(DtNmosResult)
// converts a result code.
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

// Returns the text in a char array, Field, as a std::string. The text ends at the first
// null, or at the end of the array when it has none.
template <std::size_t N> std::string FromArray(const char (&Field)[N])
{
    std::size_t Length = 0;
    while (Length < N && Field[Length] != '\0')
    {
        ++Length;
    }
    return std::string(Field, Length);
}

// Returns Text for a C function, or NULL when Text is empty. Use it for a field where the
// C API reads NULL as "none" or "the default".
inline const char* NullIfEmpty(const std::string& Text)
{
    return Text.empty() ? nullptr : Text.c_str();
}

// Returns a C string as a std::string, or "" when Native is NULL.
inline std::string FromNative(const char* Native)
{
    return Native != nullptr ? std::string(Native) : std::string();
}

// Converts a Result to the result code of the C API.
inline DtNmosResult ToNative(Result Code)
{
    return static_cast<DtNmosResult>(Code);
}

// Converts a LogLevel to the log level of the C API.
inline DtNmosLogLevel ToNative(LogLevel Level)
{
    return static_cast<DtNmosLogLevel>(Level);
}

// Returns the Error of a C call that has just failed with the code Native. The Error
// holds the code and the message that the C call left on this thread.
inline Error LastError(DtNmosResult Native)
{
    return Error{FromNative(Native), FromNative(DtNmos_GetLastError())};
}

// Turns the result code of a C call into a Status. When the call failed, the Status holds
// the Error, with the message the call left.
[[nodiscard]] inline Status Check(DtNmosResult Native)
{
    if (Native == DTNMOS_OK)
    {
        return {};
    }
    return DtNmos::Unexpected(LastError(Native));
}

// Copies Text into a char array of a C struct, Field, and ends it with a null. When Text
// does not fit, it fails with Result::InvalidArgument and leaves Field unchanged; it does
// not shorten the text. What is the name of the field, for the message.
template <std::size_t N>
[[nodiscard]] Status CopyText(char (&Field)[N], std::string_view Text,
                              std::string_view What)
{
    if (Text.size() >= N)
    {
        return DtNmos::Unexpected(
            Error{Result::InvalidArgument, std::string(What) + " is longer than " +
                                               std::to_string(N - 1) +
                                               " characters: " + std::string(Text)});
    }
    Text.copy(Field, Text.size());
    Field[Text.size()] = '\0';
    return {};
}

// The message that Guard() gives for an exception that is not a std::exception.
inline constexpr const char* UnknownException =
    "The program's callback threw an exception.";

// Calls a function of the program from a callback of the C library, and catches every
// exception, as no exception may pass through C code. Function returns a Status or
// nothing. When it throws, Guard() returns an Error with Code and the message of the
// exception. In a program built without exceptions, Guard() only calls Function.
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
        return DtNmos::Unexpected(Error{Code, Exception.what()});
    }
    catch (...)
    {
        return DtNmos::Unexpected(Error{Code, UnknownException});
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

// Passes a log message of the C library to the program's LogFunction, which User points
// to. The C library calls it as its log callback. If the LogFunction throws, the
// exception is ignored, as the C callback cannot report it.
inline void LogTrampoline(void* User, DtNmosLogLevel Level, const char* Message) noexcept
{
    const LogFunction& Log = *static_cast<const LogFunction*>(User);
    (void)Guard(Result::Internal,
                [&] { Log(FromNative(Level), Message != nullptr ? Message : ""); });
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Access::FromNative -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The library ends an ID with a null. A DtNmosId without one is read up to its last
// character.
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

} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Id::FromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<Id> Id::FromName(const Id& Namespace, std::string_view Name)
{
    const DtNmosId NativeNamespace = Detail::Access::ToNative(Namespace);
    const std::string NameText(Name);
    DtNmosId Made{};
    const Status Checked =
        Detail::Check(DtNmosId_FromName(&NativeNamespace, NameText.c_str(), &Made));
    if (!Checked)
    {
        return DtNmos::Unexpected(Checked.error());
    }
    return Detail::Access::FromNative(Made);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Id::FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A UUID has groups of 8, 4, 4, 4 and 12 hex digits, with a hyphen between two groups.
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
        return DtNmos::Unexpected(
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
    return std::string(Detail::Access::View(*this));
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
    return DtNmosResult_Name(Detail::ToNative(Code));
}

} // namespace DtNmos

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- std::hash<DtNmos::Id> -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Hashes an ID by its text, so that an Id can be a key of a std::unordered_map.
//
template <> struct std::hash<DtNmos::Id>
{
    std::size_t operator()(const DtNmos::Id& Value) const noexcept
    {
        return std::hash<std::string_view>{}(DtNmos::Detail::Access::View(Value));
    }
};
