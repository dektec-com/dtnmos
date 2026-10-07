// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_http.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API of HTTP and WebSockets, through which dtnmos reaches the network
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos_http.h. dtnmos does not open network connections itself. The
// program gives it a function that sends HTTP requests: an HttpFunction of its own, or
// CurlHttp, which uses libcurl. A program that runs an HTTP server of its own passes each
// request it receives to Node::Handle(), as an HttpRequest, and sends back the
// HttpResponse that Node::Handle() returns.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <chrono>
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
// A body is a std::string of bytes, which may hold nulls. An empty string means that
// there is no body.
//

// A header of an HTTP response: its name and its value.
struct HttpHeader
{
    std::string Name;
    std::string Value;

    friend bool operator==(const HttpHeader&, const HttpHeader&) = default;
};

// An HTTP request. The library gives one to the program's HttpFunction when it asks a
// server, such as a registry. A program with an HTTP server of its own fills one in for
// each request it receives, and passes it to Node::Handle().
struct HttpRequest
{
    std::string Method; // "GET", "POST", "PUT", "PATCH" or "DELETE"
    // The URL. A request to a server has the whole URL; a request to Node::Handle() has
    // only the path and the query.
    std::string Url;
    std::string ContentType; // The type of the body; "" when there is no body
    std::string Body;
    uint32_t TimeoutMs = 0; // For a request to a server: how long it may take in all

    friend bool operator==(const HttpRequest&, const HttpRequest&) = default;
};

// An HTTP response: its status, its headers and its body.
struct HttpResponse
{
    int Status = 0; // The HTTP status, e.g. 200
    std::vector<HttpHeader> Headers;
    std::string ContentType; // The type of the body; "" when there is no body
    std::string Body;

    // Returns the value of the first header whose name is Name, ignoring case. Returns
    // nullptr when there is no such header.
    const std::string* FindHeader(std::string_view Name) const;

    friend bool operator==(const HttpResponse&, const HttpResponse&) = default;
};

// The program's function that sends an HTTP request to a server. It returns the
// response whenever the server answers, also with an error status. It fails with
// Result::Timeout when no answer came in time, and with Result::Http when it could not
// reach the server or the exchange broke off. The library calls it on the thread that
// called the library. If it throws, the library takes that as a failure with
// Result::Http.
using HttpFunction = std::function<Expected<HttpResponse>(const HttpRequest& Request)>;

// The type of CurlHttp, the HttpFunction that sends requests with libcurl, over HTTP and
// HTTPS. In a library built without libcurl, it fails with Result::State. A node or a
// query that is given CurlHttp calls libcurl directly, without converting each request.
struct CurlHttpFunction
{
    Expected<HttpResponse> operator()(const HttpRequest& Request) const;
};
inline constexpr CurlHttpFunction CurlHttp{};

// Returns true when the library was built with libcurl, so that CurlHttp works.
bool HasCurl();

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The WebSocket of a client +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A subscription to a registry (dtnmos_query.hpp) receives the changes over a WebSocket.
// The program can give a WebSocketConnect, a function that opens the WebSocket. Without
// one, the library uses libcurl. The library calls the function on the thread that
// called the library, and opens one connection at a time.
//

// A WebSocket connection that the program's WebSocketConnect opened. The library owns
// it, and destroying it closes the connection.
class WebSocketConnection
{
  public:
    virtual ~WebSocketConnection() = default;

    // Waits up to Timeout for a whole text message, and returns it. Fails with
    // Result::Timeout when no whole message came; a part that did come is kept for the
    // next call. Fails with Result::Network when the connection closed or broke.
    virtual Expected<std::string> Receive(std::chrono::milliseconds Timeout) = 0;
};

// The program's function that opens a WebSocket connection to Url, which starts with
// "ws://" or "wss://". It must connect within Timeout, and returns the connection. It
// fails with Result::Timeout or Result::Network.
using WebSocketConnect = std::function<Expected<std::unique_ptr<WebSocketConnection>>(
    const std::string& Url, std::chrono::milliseconds Timeout)>;

// Returns true when the WebSocket on libcurl works: the library was built with it, and
// the libcurl it runs with supports WebSockets.
bool HasCurlWebSocket();

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

namespace Detail
{

// Turns the Status of a function of the program into the result code that a C callback
// returns. When Done holds an Error, it sets the message with DtNmos_SetLastError(), so
// that the C library passes it on, and returns the code. An Error whose code is
// Result::Ok is not a failure, so Fallback is returned for it instead.
inline DtNmosResult Fail(const Status& Done, Result Fallback)
{
    if (Done)
    {
        return DTNMOS_OK;
    }
    const Result Code = Done.error().Code == Result::Ok ? Fallback : Done.error().Code;
    return DtNmos_SetLastError(ToNative(Code), Done.error().Message.c_str());
}

// Frees a DtNmosHttpResponse. A std::unique_ptr uses it as its deleter.
struct HttpResponseFree
{
    void operator()(DtNmosHttpResponse* Response) const
    {
        DtNmosHttpResponse_Free(Response);
    }
};

// Converts a request of the C API to an HttpRequest. A text that is NULL becomes "".
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

// Converts a response of the C API to an HttpResponse.
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

// Converts an HttpRequest to a request of the C API. The result points into the strings
// of Value, so Value must live as long as the result is used. An empty content type and
// an empty body become NULL.
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

// Copies the status, headers and body of Value into Native, an empty response of the C
// API. Fails when the C API has no memory for them.
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

// Sends a request of the C library through the program's HttpFunction, which User points
// to, and fills Response with its answer. The C library calls it as its HTTP function.
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

// An HTTP function of the C API, with the User that it is called with.
struct NativeHttp
{
    DtNmosHttpFunc Function = nullptr;
    void* User = nullptr;
};

// Returns the HTTP function of the C API that calls Http. For CurlHttp it returns
// libcurl's own C function, so that no request is converted. For any other function it
// returns HttpTrampoline() with a pointer to Http, so Http must live as long as the C
// library uses it. For an empty Http it returns NULL, which the C API refuses.
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

// A WebSocket connection that the C library holds. The C library sees a pointer to it as
// its connection handle.
struct OpenWebSocket
{
    std::unique_ptr<WebSocketConnection> Connection; // The program's connection
    // The message that Connection received last. The C library reads it until it calls
    // Receive again.
    std::string Message;
};

// The WebSocket functions of the C API that call the program's WebSocketConnect. The C
// library keeps the address of this struct, so the struct is not copied or moved.
struct NativeWebSocket
{
    WebSocketConnect Connect;             // The program's function
    DtNmosWebSocketTransport Transport{}; // The C functions, which Get() fills in

    NativeWebSocket() = default;
    NativeWebSocket(const NativeWebSocket&) = delete;
    NativeWebSocket& operator=(const NativeWebSocket&) = delete;

    // Returns the C functions that call Connect. Returns NULL when Connect is empty; the
    // C library then uses libcurl.
    const DtNmosWebSocketTransport* Get();
};

// Opens a WebSocket connection with the program's WebSocketConnect, in the
// NativeWebSocket that User points to. The C library calls it as its Connect function.
// The C library owns the connection it gets in *Connection, until it closes it.
inline DtNmosResult WebSocketConnectTrampoline(void* User, const char* Url,
                                               uint32_t TimeoutMs,
                                               void** Connection) noexcept
{
    const NativeWebSocket& WebSocket = *static_cast<const NativeWebSocket*>(User);
    std::unique_ptr<OpenWebSocket> Opened;
    const Status Done = Guard(
        Result::Network,
        [&]() -> Status
        {
            auto Made =
                WebSocket.Connect(FromNative(Url), std::chrono::milliseconds(TimeoutMs));
            if (!Made)
            {
                return std::unexpected(Made.error());
            }
            if (*Made == nullptr)
            {
                return std::unexpected(Error{Result::Network, "No connection was made."});
            }
            Opened = std::make_unique<OpenWebSocket>();
            Opened->Connection = std::move(*Made);
            return {};
        });
    if (Done)
    {
        *Connection = Opened.release();
    }
    return Fail(Done, Result::Network);
}

// Receives a message on a connection that WebSocketConnectTrampoline() opened. The C
// library calls it as its Receive function.
inline DtNmosResult WebSocketReceiveTrampoline(void* User, void* Connection,
                                               uint32_t TimeoutMs, const char** Message,
                                               size_t* Length) noexcept
{
    (void)User;
    OpenWebSocket& Open = *static_cast<OpenWebSocket*>(Connection);
    const Status Done = Guard(Result::Network,
                              [&]() -> Status
                              {
                                  auto Received = Open.Connection->Receive(
                                      std::chrono::milliseconds(TimeoutMs));
                                  if (!Received)
                                  {
                                      return std::unexpected(Received.error());
                                  }
                                  Open.Message = std::move(*Received);
                                  return {};
                              });
    if (Done)
    {
        *Message = Open.Message.c_str();
        *Length = Open.Message.size();
    }
    return Fail(Done, Result::Network);
}

// Closes and frees a connection that WebSocketConnectTrampoline() opened. The C library
// calls it as its Close function.
inline void WebSocketCloseTrampoline(void* User, void* Connection) noexcept
{
    (void)User;
    const std::unique_ptr<OpenWebSocket> Closed(static_cast<OpenWebSocket*>(Connection));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasCurlWebSocket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline bool HasCurlWebSocket()
{
    return DtNmos_HasCurlWebSocket();
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- Detail::NativeWebSocket::Get -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline const DtNmosWebSocketTransport* Detail::NativeWebSocket::Get()
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosWebSocketTransport, Close);
    if (!Connect)
    {
        return nullptr;
    }
    Transport.Size = sizeof(Transport);
    Transport.User = this;
    Transport.Connect = WebSocketConnectTrampoline;
    Transport.Receive = WebSocketReceiveTrampoline;
    Transport.Close = WebSocketCloseTrampoline;
    return &Transport;
}

} // namespace DtNmos
