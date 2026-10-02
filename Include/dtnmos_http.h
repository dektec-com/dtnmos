// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_http.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The HTTP and WebSocket clients dtnmos works through
//
// SPDX-License-Identifier: BSD-3-Clause
//
// dtnmos does not talk to the network itself: a program gives it the functions that send
// HTTP requests and receive WebSocket messages. The library has ready ones on libcurl,
// DtNmos_CurlHttp() and DtNmos_CurlWebSocket(); a program may give its own instead, e.g.
// to use its own HTTP stack or to test without a network.

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

// One header of an HTTP request or response.
typedef struct DtNmosHttpHeader
{
    const char* Name;
    const char* Value;
} DtNmosHttpHeader;

// An HTTP request. The library fills one in when it asks a server, e.g. a registry, and
// passes it to the program's DtNmosHttpFunc. A program with an HTTP server of its own
// fills one in for a request it received, and passes it to DtNmosNode_Handle().
typedef struct DtNmosHttpRequest
{
    size_t Size;        // sizeof(DtNmosHttpRequest)
    const char* Method; // "GET", "POST", "PUT", "PATCH" or "DELETE"
    const char* Url; // To a server: the whole URL. To DtNmosNode_Handle(): the path and
                     // query only
    const char* ContentType; // Of the body; NULL without a body
    const char* Body;        // NULL without a body
    size_t BodyLength;       // Bytes in Body
    uint32_t TimeoutMs;      // To a server: how long the whole request may take
} DtNmosHttpRequest;

// An HTTP response: its status, headers and body. It owns its strings.
typedef struct DtNmosHttpResponse DtNmosHttpResponse;

// The program's function that sends Request to a server and fills Response, which is
// empty, with the answer. The library calls it on the thread that called the library.
//
// Returns DTNMOS_OK when an answer came, whatever its status, or, with a reason set by
// DtNmos_SetLastError():
//   DTNMOS_E_TIMEOUT  no answer came in time
//   DTNMOS_E_HTTP     the server could not be reached, or the exchange failed
typedef DtNmosResult (*DtNmosHttpFunc)(void* User, const DtNmosHttpRequest* Request,
                                       DtNmosHttpResponse* Response);

// A DtNmosHttpFunc that uses libcurl, for HTTP and HTTPS. User is not used. In a library
// built without libcurl it returns DTNMOS_E_STATE.
DTNMOS_API DtNmosResult DtNmos_CurlHttp(void* User, const DtNmosHttpRequest* Request,
                                        DtNmosHttpResponse* Response);

// Returns whether the library was built with libcurl, so that DtNmos_CurlHttp() works.
DTNMOS_API bool DtNmos_HasCurl(void);

// Adds a header to Response. Name and Value are copied.
DTNMOS_API DtNmosResult DtNmosHttpResponse_AddHeader(DtNmosHttpResponse* Response,
                                                     const char* Name, const char* Value);

// Creates an empty response, with status 0. Returns NULL when there is not enough memory.
DTNMOS_API DtNmosHttpResponse* DtNmosHttpResponse_Alloc(void);

// Adds Length bytes of Body to the end of the response's body. Body is copied. An HTTP
// function uses it to add the body as it arrives, piece by piece.
DTNMOS_API DtNmosResult DtNmosHttpResponse_AppendBody(DtNmosHttpResponse* Response,
                                                      const char* Body, size_t Length);

// Returns the body of Response, with a null at its end, and sets *Length to its length
// when Length is not NULL.
DTNMOS_API const char* DtNmosHttpResponse_Body(const DtNmosHttpResponse* Response,
                                               size_t* Length);

// Returns the content type of the body, or "" when it has none.
DTNMOS_API const char* DtNmosHttpResponse_ContentType(const DtNmosHttpResponse* Response);

// Returns the value of the first header called Name, ignoring case, or NULL when there
// is none.
DTNMOS_API const char* DtNmosHttpResponse_FindHeader(const DtNmosHttpResponse* Response,
                                                     const char* Name);

// Frees Response. NULL does nothing.
DTNMOS_API void DtNmosHttpResponse_Free(DtNmosHttpResponse* Response);

// Frees *Response, as DtNmosHttpResponse_Free() does, and sets *Response to NULL. NULL
// does nothing.
DTNMOS_API void DtNmosHttpResponse_Freep(DtNmosHttpResponse** Response);

// Returns header Index (from 0) of Response. Its strings belong to the response. Past
// the last header, both strings are NULL.
DTNMOS_API DtNmosHttpHeader DtNmosHttpResponse_Header(const DtNmosHttpResponse* Response,
                                                      size_t Index);

// Returns how many headers Response has.
DTNMOS_API size_t DtNmosHttpResponse_HeaderCount(const DtNmosHttpResponse* Response);

// Replaces the body of Response with Length bytes of Body, of type ContentType, which
// may be NULL. Body is copied.
DTNMOS_API DtNmosResult DtNmosHttpResponse_SetBody(DtNmosHttpResponse* Response,
                                                   const char* ContentType,
                                                   const char* Body, size_t Length);

// Sets the status of Response, e.g. 200.
DTNMOS_API void DtNmosHttpResponse_SetStatus(DtNmosHttpResponse* Response, int Status);

// Returns the status of Response, or 0 before one is set.
DTNMOS_API int DtNmosHttpResponse_Status(const DtNmosHttpResponse* Response);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The WebSocket of a client +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A subscription to a registry (dtnmos_query.h) receives its changes over a WebSocket.
// The program gives the functions for it in a DtNmosWebSocketTransport, or uses the one
// on libcurl, DtNmos_CurlWebSocket().
//

// The functions that open, read and close a WebSocket. The library calls them on the
// thread that called the library, for one connection at a time, and passes User to each.
typedef struct DtNmosWebSocketTransport
{
    size_t Size; // sizeof(DtNmosWebSocketTransport)
    void* User;  // Passed to each function

    // Connects to Url ("ws://" or "wss://") within TimeoutMs milliseconds, and sets
    // *Connection to a handle for the other functions. Returns DTNMOS_OK, or
    // DTNMOS_E_TIMEOUT or DTNMOS_E_NETWORK.
    DtNmosResult (*Connect)(void* User, const char* Url, uint32_t TimeoutMs,
                            void** Connection);

    // Waits up to TimeoutMs milliseconds for a whole text message, and sets *Message to
    // it, with a null at its end, and *Length to its length. The message is valid until
    // the next Receive or Close of the connection. Returns DTNMOS_OK, DTNMOS_E_TIMEOUT
    // when no whole message came (a part that did is kept for the next call), or
    // DTNMOS_E_NETWORK when the connection closed or failed.
    DtNmosResult (*Receive)(void* User, void* Connection, uint32_t TimeoutMs,
                            const char** Message, size_t* Length);

    // Closes the connection and frees its handle.
    void (*Close)(void* User, void* Connection);
} DtNmosWebSocketTransport;

// Returns the WebSocket functions on libcurl, for ws:// and wss://. They answer the
// server's pings themselves. In a library without them, Connect returns DTNMOS_E_STATE.
DTNMOS_API const DtNmosWebSocketTransport* DtNmos_CurlWebSocket(void);

// Returns whether DtNmos_CurlWebSocket() works: the library was built with it, and the
// libcurl it runs with supports WebSockets.
DTNMOS_API bool DtNmos_HasCurlWebSocket(void);

#ifdef __cplusplus
}
#endif
