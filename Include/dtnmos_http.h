// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_http.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The HTTP and WebSocket clients dtnmos works through
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "dtnmos.h"

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= HTTP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A header of a request or a response.
typedef struct DtNmosHttpHeader
{
    const char* Name;
    const char* Value;
} DtNmosHttpHeader;

// A request: of the library to a server for a client request, or of a client to the
// library for a request the library answers.
typedef struct DtNmosHttpRequest
{
    size_t Size;        // sizeof(DtNmosHttpRequest)
    const char* Method; // "GET", "POST", "PUT", "PATCH" or "DELETE"
    const char* Url;    // absolute URL of a client request; path and query of another
    const char* ContentType; // of body; null without body
    const char* Body;
    size_t BodyLength;
    uint32_t TimeoutMs; // of a client request: how long it may take in all
} DtNmosHttpRequest;

// A response: its status, headers and body, which it owns.
typedef struct DtNmosHttpResponse DtNmosHttpResponse;

// Allocates an empty response, with status 0, for a caller that answers requests of its
// own with the library; returns null when out of memory.
DTNMOS_API DtNmosHttpResponse* DtNmosHttpResponse_Alloc(void);

// Frees response. Null is allowed.
DTNMOS_API void DtNmosHttpResponse_Free(DtNmosHttpResponse* response);

// Frees *response as DtNmosHttpResponse_Free() does and sets *response to null. Null is
// allowed.
DTNMOS_API void DtNmosHttpResponse_Freep(DtNmosHttpResponse** response);

DTNMOS_API void DtNmosHttpResponse_SetStatus(DtNmosHttpResponse* response, int status);

// Makes the body length bytes of body, which it copies, of content_type, which may be
// null.
DTNMOS_API DtNmosResult DtNmosHttpResponse_SetBody(DtNmosHttpResponse* response,
                                                   const char* content_type,
                                                   const char* body, size_t length);

// Appends length bytes of body, which it copies, as a transport receives it in pieces.
DTNMOS_API DtNmosResult DtNmosHttpResponse_AppendBody(DtNmosHttpResponse* response,
                                                      const char* body, size_t length);

// Adds a header, whose name and value it copies.
DTNMOS_API DtNmosResult DtNmosHttpResponse_AddHeader(DtNmosHttpResponse* response,
                                                     const char* name, const char* value);

DTNMOS_API int DtNmosHttpResponse_Status(const DtNmosHttpResponse* response);

// Returns the body, ending in a null character, and its length when length is not null.
DTNMOS_API const char* DtNmosHttpResponse_Body(const DtNmosHttpResponse* response,
                                               size_t* length);

// Returns the content type of the body; "" when it has none.
DTNMOS_API const char* DtNmosHttpResponse_ContentType(const DtNmosHttpResponse* response);

// Returns the number of headers, and the header at index, whose strings the response
// owns.
DTNMOS_API size_t DtNmosHttpResponse_HeaderCount(const DtNmosHttpResponse* response);
DTNMOS_API DtNmosHttpHeader DtNmosHttpResponse_Header(const DtNmosHttpResponse* response,
                                                      size_t index);

// Returns the value of the first header called name, compared without regard to case,
// or null when there is none.
DTNMOS_API const char* DtNmosHttpResponse_FindHeader(const DtNmosHttpResponse* response,
                                                     const char* name);

// Performs a client request and fills response, which is empty. Returns DTNMOS_OK when
// an answer came, whatever its status; DTNMOS_E_TIMEOUT when none came in time, and
// DTNMOS_E_HTTP when the server could not be reached or the exchange failed, its message
// set with DtNmos_SetLastError(). Called on the thread of the caller of the library.
typedef DtNmosResult (*DtNmosHttpFunc)(void* user, const DtNmosHttpRequest* request,
                                       DtNmosHttpResponse* response);

// Whether the library was built with the transport on libcurl.
DTNMOS_API int DtNmos_HasCurl(void);

// A DtNmosHttpFunc on libcurl, for HTTP and HTTPS; user is unused. Without libcurl it
// fails with DTNMOS_E_STATE.
DTNMOS_API DtNmosResult DtNmos_CurlHttp(void* user, const DtNmosHttpRequest* request,
                                        DtNmosHttpResponse* response);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The WebSocket of a client +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The WebSocket of a client, which a subscription receives its messages over: the
// functions of a transport, with user passed to each. dtnmos calls them on the thread
// of its caller, one connection at a time.
typedef struct DtNmosWebSocketTransport
{
    size_t Size; // sizeof(DtNmosWebSocketTransport)
    void* User;

    // Connects to url, "ws://" or "wss://", within timeout_ms, and sets *connection to
    // what the other functions get. Fails with DTNMOS_E_TIMEOUT or DTNMOS_E_NETWORK.
    DtNmosResult (*Connect)(void* user, const char* url, uint32_t timeout_ms,
                            void** connection);

    // Waits at most timeout_ms for a whole text message and sets *message to it, ending
    // in a null character, and *length to its length. The message belongs to connection
    // and stays valid until the next Receive or Close of it. Fails with DTNMOS_E_TIMEOUT
    // when none came, keeping a part that did for the next call, and with
    // DTNMOS_E_NETWORK when the connection closed or failed.
    DtNmosResult (*Receive)(void* user, void* connection, uint32_t timeout_ms,
                            const char** message, size_t* length);

    // Closes connection and frees it.
    void (*Close)(void* user, void* connection);
} DtNmosWebSocketTransport;

// Whether the library was built with the WebSocket on libcurl, and the libcurl it runs
// with carries WebSockets.
DTNMOS_API int DtNmos_HasCurlWebSocket(void);

// The WebSocket on libcurl, for ws:// and wss://; it answers the pings of the server
// itself. Without it, its connect fails with DTNMOS_E_STATE.
DTNMOS_API const DtNmosWebSocketTransport* DtNmos_CurlWebSocket(void);

#ifdef __cplusplus
}
#endif
