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

// Performs a client request and fills response, which is empty. Returns DTNMOS_OK when
// an answer came, whatever its status; DTNMOS_E_TIMEOUT when none came in time, and
// DTNMOS_E_HTTP when the server could not be reached or the exchange failed, its message
// set with DtNmos_SetLastError(). Called on the thread of the caller of the library.
typedef DtNmosResult (*DtNmosHttpFunc)(void* User, const DtNmosHttpRequest* Request,
                                       DtNmosHttpResponse* Response);

// A DtNmosHttpFunc on libcurl, for HTTP and HTTPS; user is unused. Without libcurl it
// fails with DTNMOS_E_STATE.
DTNMOS_API DtNmosResult DtNmos_CurlHttp(void* User, const DtNmosHttpRequest* Request,
                                        DtNmosHttpResponse* Response);

// Whether the library was built with the transport on libcurl.
DTNMOS_API bool DtNmos_HasCurl(void);

// Adds a header, whose name and value it copies.
DTNMOS_API DtNmosResult DtNmosHttpResponse_AddHeader(DtNmosHttpResponse* Response,
                                                     const char* Name, const char* Value);

// Allocates an empty response, with status 0, for a caller that answers requests of its
// own with the library; returns null when out of memory.
DTNMOS_API DtNmosHttpResponse* DtNmosHttpResponse_Alloc(void);

// Appends length bytes of body, which it copies, as a transport receives it in pieces.
DTNMOS_API DtNmosResult DtNmosHttpResponse_AppendBody(DtNmosHttpResponse* Response,
                                                      const char* Body, size_t Length);

// Returns the body, ending in a null character, and its length when length is not null.
DTNMOS_API const char* DtNmosHttpResponse_Body(const DtNmosHttpResponse* Response,
                                               size_t* Length);

// Returns the content type of the body; "" when it has none.
DTNMOS_API const char* DtNmosHttpResponse_ContentType(const DtNmosHttpResponse* Response);

// Returns the value of the first header called name, compared without regard to case,
// or null when there is none.
DTNMOS_API const char* DtNmosHttpResponse_FindHeader(const DtNmosHttpResponse* Response,
                                                     const char* Name);

// Frees response. Null is allowed.
DTNMOS_API void DtNmosHttpResponse_Free(DtNmosHttpResponse* Response);

// Frees *Response as DtNmosHttpResponse_Free() does and sets *Response to null. Null is
// allowed.
DTNMOS_API void DtNmosHttpResponse_Freep(DtNmosHttpResponse** Response);

// Returns the header at Index, from 0, whose strings the response owns; one of null
// strings past the last.
DTNMOS_API DtNmosHttpHeader DtNmosHttpResponse_Header(const DtNmosHttpResponse* Response,
                                                      size_t Index);

// Returns the number of headers.
DTNMOS_API size_t DtNmosHttpResponse_HeaderCount(const DtNmosHttpResponse* Response);

// Makes the body length bytes of body, which it copies, of ContentType, which may be
// null.
DTNMOS_API DtNmosResult DtNmosHttpResponse_SetBody(DtNmosHttpResponse* Response,
                                                   const char* ContentType,
                                                   const char* Body, size_t Length);

// Sets the status of the response, e.g. 200.
DTNMOS_API void DtNmosHttpResponse_SetStatus(DtNmosHttpResponse* Response, int Status);

// Returns the status of the response; 0 before one is set.
DTNMOS_API int DtNmosHttpResponse_Status(const DtNmosHttpResponse* Response);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The WebSocket of a client +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The WebSocket of a client, which a subscription receives its messages over: the
// functions of a transport, with user passed to each. dtnmos calls them on the thread
// of its caller, one connection at a time.
typedef struct DtNmosWebSocketTransport
{
    size_t Size; // sizeof(DtNmosWebSocketTransport)
    void* User;

    // Connects to url, "ws://" or "wss://", within TimeoutMs, and sets *Connection to
    // what the other functions get. Fails with DTNMOS_E_TIMEOUT or DTNMOS_E_NETWORK.
    DtNmosResult (*Connect)(void* User, const char* Url, uint32_t TimeoutMs,
                            void** Connection);

    // Waits at most TimeoutMs for a whole text message and sets *Message to it, ending
    // in a null character, and *Length to its length. The message belongs to connection
    // and stays valid until the next Receive or Close of it. Fails with DTNMOS_E_TIMEOUT
    // when none came, keeping a part that did for the next call, and with
    // DTNMOS_E_NETWORK when the connection closed or failed.
    DtNmosResult (*Receive)(void* User, void* Connection, uint32_t TimeoutMs,
                            const char** Message, size_t* Length);

    // Closes connection and frees it.
    void (*Close)(void* User, void* Connection);
} DtNmosWebSocketTransport;

// The WebSocket on libcurl, for ws:// and wss://; it answers the pings of the server
// itself. Without it, its connect fails with DTNMOS_E_STATE.
DTNMOS_API const DtNmosWebSocketTransport* DtNmos_CurlWebSocket(void);

// Whether the library was built with the WebSocket on libcurl, and the libcurl it runs
// with carries WebSockets.
DTNMOS_API bool DtNmos_HasCurlWebSocket(void);

#ifdef __cplusplus
}
#endif
