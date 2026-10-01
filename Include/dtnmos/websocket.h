// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# websocket.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The WebSocket of a client, as functions the caller passes in
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "dtnmos/dtnmos.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The WebSocket of a client, which a subscription receives its messages over: the
// functions of a transport, with user passed to each. dtnmos calls them on the thread
// of its caller, one connection at a time.
typedef struct dtnmos_websocket_transport
{
    size_t size; // sizeof(dtnmos_websocket_transport)
    void* user;

    // Connects to url, "ws://" or "wss://", within timeout_ms, and sets *connection to
    // what the other functions get. Fails with DTNMOS_E_TIMEOUT or DTNMOS_E_NETWORK.
    dtnmos_result (*connect)(void* user, const char* url, uint32_t timeout_ms,
                             void** connection, dtnmos_error* error);

    // Waits at most timeout_ms for a whole text message and sets message to it. Fails
    // with DTNMOS_E_TIMEOUT when none came, keeping a part that did for the next call,
    // and with DTNMOS_E_NETWORK when the connection closed or failed.
    dtnmos_result (*receive)(void* user, void* connection, uint32_t timeout_ms,
                             dtnmos_string* message, dtnmos_error* error);

    // Closes connection and frees it.
    void (*close)(void* user, void* connection);
} dtnmos_websocket_transport;

// Whether the library was built with the WebSocket on libcurl, and the libcurl it runs
// with carries WebSockets.
DTNMOS_API int dtnmos_has_curl_websocket(void);

// The WebSocket on libcurl, for ws:// and wss://; it answers the pings of the server
// itself. Without it, its connect fails with DTNMOS_E_STATE.
DTNMOS_API const dtnmos_websocket_transport* dtnmos_curl_websocket(void);

#ifdef __cplusplus
}
#endif
