// #*#*#*#*#*#*#*#*#*#*#*#*# NmosCurlWebSocket.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The WebSocket on libcurl, when the library is built with DTNMOS_WITH_CURL
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_http.h"

#include <string.h>

#include "NmosInternal.h"
#include "NmosOs.h"

#ifdef DTNMOS_WITH_CURL
    #include <curl/curl.h>
    #include <stdlib.h>
    #ifndef _WIN32
        #include <sys/select.h>
    #endif

// A connection: the handle of libcurl, and the part of a message that has come so far.
typedef struct curl_connection
{
    CURL* curl;
    dtnmos_buffer partial;
} curl_connection;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- has_ws_protocol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the libcurl the library runs with carries the ws protocol.
//
static int has_ws_protocol(void)
{
    const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    for (const char* const* protocol = info->protocols;
         protocol != NULL && *protocol != NULL; ++protocol)
    {
        if (strcmp(*protocol, "ws") == 0)
        {
            return 1;
        }
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasCurlWebSocket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtNmos_HasCurlWebSocket(void)
{
    return has_ws_protocol();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ws_connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ws_connect(void* user, const char* url, uint32_t timeout_ms,
                               void** connection)
{
    (void)user;
    if (url == NULL || connection == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "A WebSocket needs a URL and a place for its connection.");
    }
    *connection = NULL;
    if (!has_ws_protocol())
    {
        return dtnmos_fail(DTNMOS_E_STATE,
                           "The libcurl of dtnmos carries no WebSockets; build it with "
                           "them, or pass a WebSocket of your own.");
    }
    curl_connection* made = calloc(1, sizeof(*made));
    if (made == NULL)
    {
        return dtnmos_fail_memory();
    }
    made->curl = curl_easy_init();
    if (made->curl == NULL)
    {
        free(made);
        return dtnmos_fail(DTNMOS_E_INTERNAL, "libcurl could not create a handle.");
    }
    // Connect only: libcurl makes the handshake, and the messages are read with
    // curl_ws_recv().
    curl_easy_setopt(made->curl, CURLOPT_URL, url);
    curl_easy_setopt(made->curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(made->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(made->curl, CURLOPT_CONNECTTIMEOUT_MS,
                     (long)(timeout_ms == 0 ? 5000 : timeout_ms));
    const CURLcode code = curl_easy_perform(made->curl);
    if (code != CURLE_OK)
    {
        curl_easy_cleanup(made->curl);
        free(made);
        return dtnmos_fail(
            code == CURLE_OPERATION_TIMEDOUT ? DTNMOS_E_TIMEOUT : DTNMOS_E_NETWORK,
            "The WebSocket %s could not be opened: %s.", url, curl_easy_strerror(code));
    }
    // A server that answered without switching gives no WebSocket to receive on.
    long status = 0;
    curl_easy_getinfo(made->curl, CURLINFO_RESPONSE_CODE, &status);
    if (status != 101)
    {
        curl_easy_cleanup(made->curl);
        free(made);
        return dtnmos_fail(DTNMOS_E_NETWORK,
                           "The server of %s answered with %ld, not with a WebSocket.",
                           url, status);
    }
    *connection = made;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- wait_readable -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits at most timeout_ms for the socket of curl to have something to read; returns
// whether it has.
//
static int wait_readable(CURL* curl, uint32_t timeout_ms)
{
    curl_socket_t socket = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket) != CURLE_OK ||
        socket == CURL_SOCKET_BAD)
    {
        return 0;
    }
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    struct timeval wait;
    wait.tv_sec = (long)(timeout_ms / 1000);
    wait.tv_usec = (long)(timeout_ms % 1000) * 1000;
    #ifdef _WIN32
    return select(0, &readable, NULL, NULL, &wait) > 0;
    #else
    return select(socket + 1, &readable, NULL, NULL, &wait) > 0;
    #endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ws_receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ws_receive(void* user, void* connection, uint32_t timeout_ms,
                               DtNmosString* message)
{
    (void)user;
    curl_connection* c = connection;
    if (c == NULL || message == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "A WebSocket receives on a connection into a message.");
    }
    const uint64_t deadline = dtnmos_monotonic_ms() + timeout_ms;
    for (;;)
    {
        char data[16384];
        size_t received = 0;
        const struct curl_ws_frame* frame = NULL;
        const CURLcode code =
            curl_ws_recv(c->curl, data, sizeof(data), &received, &frame);
        if (code == CURLE_AGAIN)
        {
            const uint64_t now = dtnmos_monotonic_ms();
            if (now >= deadline)
            {
                return dtnmos_fail(DTNMOS_E_TIMEOUT, "No message came within %u ms.",
                                   (unsigned)timeout_ms);
            }
            wait_readable(c->curl, (uint32_t)(deadline - now));
            continue;
        }
        if (code != CURLE_OK)
        {
            return dtnmos_fail(DTNMOS_E_NETWORK, "The WebSocket failed: %s.",
                               curl_easy_strerror(code));
        }
        if ((frame->flags & CURLWS_CLOSE) != 0)
        {
            return dtnmos_fail(DTNMOS_E_NETWORK, "The server closed the WebSocket.");
        }
        if ((frame->flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT)) == 0)
        {
            // A ping or a pong, which libcurl answers itself.
            continue;
        }
        dtnmos_buffer_append(&c->partial, data, received);
        if (c->partial.failed)
        {
            dtnmos_buffer_free(&c->partial);
            return dtnmos_fail_memory();
        }
        // The message is whole at the end of a frame that is not followed by another.
        if (frame->bytesleft == 0 && (frame->flags & CURLWS_CONT) == 0)
        {
            const DtNmosResult result =
                DtNmosString_Set(message, c->partial.data == NULL ? "" : c->partial.data,
                                 c->partial.length);
            dtnmos_buffer_free(&c->partial);
            return result == DTNMOS_OK ? DTNMOS_OK : dtnmos_fail_memory();
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ws_close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ws_close(void* user, void* connection)
{
    (void)user;
    curl_connection* c = connection;
    if (c == NULL)
    {
        return;
    }
    size_t sent = 0;
    curl_ws_send(c->curl, "", 0, &sent, 0, CURLWS_CLOSE);
    curl_easy_cleanup(c->curl);
    dtnmos_buffer_free(&c->partial);
    free(c);
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasCurlWebSocket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtNmos_HasCurlWebSocket(void)
{
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ws_connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ws_connect(void* user, const char* url, uint32_t timeout_ms,
                               void** connection)
{
    (void)user;
    (void)url;
    (void)timeout_ms;
    if (connection != NULL)
    {
        *connection = NULL;
    }
    return dtnmos_fail(DTNMOS_E_STATE,
                       "dtnmos was built without libcurl, so it has no WebSocket of its "
                       "own; pass one.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ws_receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ws_receive(void* user, void* connection, uint32_t timeout_ms,
                               DtNmosString* message)
{
    (void)user;
    (void)connection;
    (void)timeout_ms;
    (void)message;
    return dtnmos_fail(DTNMOS_E_STATE, "dtnmos was built without libcurl.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ws_close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ws_close(void* user, void* connection)
{
    (void)user;
    (void)connection;
}

#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_CurlWebSocket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtNmosWebSocketTransport* DtNmos_CurlWebSocket(void)
{
    static const DtNmosWebSocketTransport transport = {
        sizeof(DtNmosWebSocketTransport), NULL, ws_connect, ws_receive, ws_close};
    return &transport;
}
