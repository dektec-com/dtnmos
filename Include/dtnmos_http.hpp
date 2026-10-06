// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_http.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API of the HTTP client dtnmos works through
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos_http.h. dtnmos does not talk to the network itself: a program
// gives it the function that sends its HTTP requests, an HttpFunction, or CurlHttp, the
// one on libcurl. A program with an HTTP server of its own hands each request it receives
// to Node::Handle(), as an HttpRequest, and sends back the HttpResponse.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dtnmos.hpp"
#include "dtnmos_http.h"

namespace DtNmos
{

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= HTTP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A body is a std::string of bytes; "" is no body.
//

// One header of an HTTP response.
struct HttpHeader
{
    std::string Name;
    std::string Value;

    friend bool operator==(const HttpHeader&, const HttpHeader&) = default;
};

// An HTTP request: one the library sends to a server, e.g. a registry, through the
// program's HttpFunction; or one a program's server received, for Node::Handle().
struct HttpRequest
{
    std::string Method; // "GET", "POST", "PUT", "PATCH" or "DELETE"
    // To a server: the whole URL. To Node::Handle(): the path and query only.
    std::string Url;
    std::string ContentType; // Of the body; "" without one
    std::string Body;
    uint32_t TimeoutMs = 0; // To a server: how long the whole request may take

    friend bool operator==(const HttpRequest&, const HttpRequest&) = default;
};

// An HTTP response: its status, headers and body.
struct HttpResponse
{
    int Status = 0; // e.g. 200
    std::vector<HttpHeader> Headers;
    std::string ContentType; // Of the body; "" without one
    std::string Body;

    // Returns the value of the first header called Name, ignoring case, or nullptr when
    // there is none.
    const std::string* FindHeader(std::string_view Name) const;

    friend bool operator==(const HttpResponse&, const HttpResponse&) = default;
};

// The program's function that sends Request to a server, and returns the answer,
// whatever its status. It fails with Result::Timeout when no answer came in time, and
// Result::Http when the server could not be reached or the exchange failed. The library
// calls it on the thread that called the library; an exception it throws is a failure
// with Result::Http.
using HttpFunction = std::function<Expected<HttpResponse>(const HttpRequest& Request)>;

// The HttpFunction on libcurl, for HTTP and HTTPS: CurlHttp. In a library built without
// libcurl it fails with Result::State. A node given it calls libcurl directly.
struct CurlHttpFunction
{
    Expected<HttpResponse> operator()(const HttpRequest& Request) const;
};
inline constexpr CurlHttpFunction CurlHttp{};

// Returns whether the library was built with libcurl, so that CurlHttp works.
bool HasCurl();

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

namespace Detail
{

// Returns the result of a callback of the C library: DTNMOS_OK, or the code of Done's
// Error with its message, left for the library with DtNmos_SetLastError(). An Error with
// Result::Ok, which is no failure, fails with Fallback.
inline DtNmosResult Fail(const Status& Done, Result Fallback)
{
    if (Done)
    {
        return DTNMOS_OK;
    }
    const Result Code = Done.error().Code == Result::Ok ? Fallback : Done.error().Code;
    return DtNmos_SetLastError(ToNative(Code), Done.error().Message.c_str());
}

// Frees a DtNmosHttpResponse, for a std::unique_ptr.
struct HttpResponseFree
{
    void operator()(DtNmosHttpResponse* Response) const
    {
        DtNmosHttpResponse_Free(Response);
    }
};

// Converts a request of the C API: a missing text is "".
inline HttpRequest FromNative(const DtNmosHttpRequest& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosHttpRequest, TimeoutMs);
    HttpRequest Value;
    Value.Method = FromNative(Native.Method);
    Value.Url = FromNative(Native.Url);
    Value.ContentType = FromNative(Native.ContentType);
    if (Native.Body != nullptr)
    {
        Value.Body.assign(Native.Body, Native.BodyLength);
    }
    Value.TimeoutMs = Native.TimeoutMs;
    return Value;
}

// Converts a response of the C API.
inline HttpResponse FromNative(const DtNmosHttpResponse* Native)
{
    HttpResponse Value;
    Value.Status = DtNmosHttpResponse_Status(Native);
    for (std::size_t i = 0; i < DtNmosHttpResponse_HeaderCount(Native); ++i)
    {
        const DtNmosHttpHeader Header = DtNmosHttpResponse_Header(Native, i);
        Value.Headers.push_back({FromNative(Header.Name), FromNative(Header.Value)});
    }
    Value.ContentType = FromNative(DtNmosHttpResponse_ContentType(Native));
    std::size_t Length = 0;
    const char* Body = DtNmosHttpResponse_Body(Native, &Length);
    if (Body != nullptr)
    {
        Value.Body.assign(Body, Length);
    }
    return Value;
}

// Returns a request as the C API takes it, pointing into Value, which must outlive it:
// "" is NULL, but for the method and the URL.
inline DtNmosHttpRequest ToNative(const HttpRequest& Value)
{
    DtNmosHttpRequest Native{};
    Native.Size = sizeof(Native);
    Native.Method = Value.Method.c_str();
    Native.Url = Value.Url.c_str();
    Native.ContentType = Value.ContentType.empty() ? nullptr : Value.ContentType.c_str();
    Native.Body = Value.Body.empty() ? nullptr : Value.Body.data();
    Native.BodyLength = Value.Body.size();
    Native.TimeoutMs = Value.TimeoutMs;
    return Native;
}

// Fills Native, an empty response of the C API, with Value.
[[nodiscard]] inline Status ToNative(const HttpResponse& Value,
                                     DtNmosHttpResponse* Native)
{
    DtNmosHttpResponse_SetStatus(Native, Value.Status);
    for (const HttpHeader& Header : Value.Headers)
    {
        const Status Added = Check(DtNmosHttpResponse_AddHeader(
            Native, Header.Name.c_str(), Header.Value.c_str()));
        if (!Added)
        {
            return Added;
        }
    }
    if (Value.Body.empty() && Value.ContentType.empty())
    {
        return {};
    }
    return Check(DtNmosHttpResponse_SetBody(
        Native, Value.ContentType.empty() ? nullptr : Value.ContentType.c_str(),
        Value.Body.data(), Value.Body.size()));
}

// The C HTTP callback of an HttpFunction, which User points to.
inline DtNmosResult HttpTrampoline(void* User, const DtNmosHttpRequest* Request,
                                   DtNmosHttpResponse* Response) noexcept
{
    const HttpFunction& Http = *static_cast<const HttpFunction*>(User);
    const Status Done = Guard(Result::Http,
                              [&]() -> Status
                              {
                                  const Expected<HttpResponse> Answer =
                                      Http(FromNative(*Request));
                                  if (!Answer)
                                  {
                                      return std::unexpected(Answer.error());
                                  }
                                  return ToNative(*Answer, Response);
                              });
    return Fail(Done, Result::Http);
}

// The C HTTP function and its User for Http: libcurl's own for CurlHttp, so that a
// request goes to it without being converted, and the trampoline for any other. A
// missing function is NULL, which the C API refuses.
struct NativeHttp
{
    DtNmosHttpFunc Function = nullptr;
    void* User = nullptr;
};

inline NativeHttp ToNative(const HttpFunction& Http)
{
    if (!Http)
    {
        return {};
    }
    if (Http.target<CurlHttpFunction>() != nullptr)
    {
        return {DtNmos_CurlHttp, nullptr};
    }
    return {HttpTrampoline, const_cast<HttpFunction*>(&Http)};
}

} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CurlHttpFunction::() -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<HttpResponse>
CurlHttpFunction::operator()(const HttpRequest& Request) const
{
    const std::unique_ptr<DtNmosHttpResponse, Detail::HttpResponseFree> Response(
        DtNmosHttpResponse_Alloc());
    if (Response == nullptr)
    {
        return std::unexpected(Error{Result::NoMemory, "Out of memory."});
    }
    const DtNmosHttpRequest Native = Detail::ToNative(Request);
    const Status Sent = Detail::Check(DtNmos_CurlHttp(nullptr, &Native, Response.get()));
    if (!Sent)
    {
        return std::unexpected(Sent.error());
    }
    return Detail::FromNative(Response.get());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasCurl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline bool HasCurl()
{
    return DtNmos_HasCurl();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HttpResponse::FindHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline const std::string* HttpResponse::FindHeader(std::string_view Name) const
{
    const auto Lower = [](char c)
    { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
    for (const HttpHeader& Header : Headers)
    {
        bool Same = Header.Name.size() == Name.size();
        for (std::size_t i = 0; Same && i < Name.size(); ++i)
        {
            Same = Lower(Header.Name[i]) == Lower(Name[i]);
        }
        if (Same)
        {
            return &Header.Value;
        }
    }
    return nullptr;
}

} // namespace DtNmos
