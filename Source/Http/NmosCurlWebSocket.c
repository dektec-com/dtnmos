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

// A connection: the handle of libcurl, the part of a message that has come so far, and
// the last whole message, which Receive hands out.
typedef struct NmosCurlConnection
{
    CURL* Curl;
    NmosBuffer Partial;
    NmosBuffer Message;
} NmosCurlConnection;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasWsProtocol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the libcurl the library runs with carries the ws protocol.
//
static int HasWsProtocol(void)
{
    const curl_version_info_data* Info = curl_version_info(CURLVERSION_NOW);
    for (const char* const* Protocol = Info->protocols;
         Protocol != NULL && *Protocol != NULL; ++Protocol)
    {
        if (strcmp(*Protocol, "ws") == 0)
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
    return HasWsProtocol();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WsConnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult WsConnect(void* User, const char* Url, uint32_t TimeoutMs,
                              void** Connection)
{
    (void)User;
    if (Url == NULL || Connection == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A WebSocket needs a URL and a place for its connection.");
    }
    *Connection = NULL;
    if (!HasWsProtocol())
    {
        return NmosError_Fail(
            DTNMOS_E_STATE, "The libcurl of dtnmos carries no WebSockets; build it with "
                            "them, or pass a WebSocket of your own.");
    }
    NmosCurlConnection* Made = calloc(1, sizeof(*Made));
    if (Made == NULL)
    {
        return NmosError_FailMemory();
    }
    Made->Curl = curl_easy_init();
    if (Made->Curl == NULL)
    {
        free(Made);
        return NmosError_Fail(DTNMOS_E_INTERNAL, "libcurl could not create a handle.");
    }
    // Connect only: libcurl makes the handshake, and the messages are read with
    // curl_ws_recv().
    curl_easy_setopt(Made->Curl, CURLOPT_URL, Url);
    curl_easy_setopt(Made->Curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(Made->Curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(Made->Curl, CURLOPT_CONNECTTIMEOUT_MS,
                     (long)(TimeoutMs == 0 ? 5000 : TimeoutMs));
    const CURLcode Code = curl_easy_perform(Made->Curl);
    if (Code != CURLE_OK)
    {
        curl_easy_cleanup(Made->Curl);
        free(Made);
        return NmosError_Fail(
            Code == CURLE_OPERATION_TIMEDOUT ? DTNMOS_E_TIMEOUT : DTNMOS_E_NETWORK,
            "The WebSocket %s could not be opened: %s.", Url, curl_easy_strerror(Code));
    }
    // A server that answered without switching gives no WebSocket to receive on.
    long Status = 0;
    curl_easy_getinfo(Made->Curl, CURLINFO_RESPONSE_CODE, &Status);
    if (Status != 101)
    {
        curl_easy_cleanup(Made->Curl);
        free(Made);
        return NmosError_Fail(DTNMOS_E_NETWORK,
                              "The server of %s answered with %ld, not with a WebSocket.",
                              Url, Status);
    }
    *Connection = Made;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitReadable -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits at most timeout_ms for the socket of curl to have something to read; returns
// whether it has.
//
static int WaitReadable(CURL* Curl, uint32_t TimeoutMs)
{
    curl_socket_t Socket = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(Curl, CURLINFO_ACTIVESOCKET, &Socket) != CURLE_OK ||
        Socket == CURL_SOCKET_BAD)
    {
        return 0;
    }
    fd_set Readable;
    FD_ZERO(&Readable);
    FD_SET(Socket, &Readable);
    struct timeval Wait;
    Wait.tv_sec = (long)(TimeoutMs / 1000);
    Wait.tv_usec = (long)(TimeoutMs % 1000) * 1000;
    #ifdef _WIN32
    return select(0, &Readable, NULL, NULL, &Wait) > 0;
    #else
    return select(Socket + 1, &Readable, NULL, NULL, &Wait) > 0;
    #endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WsReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult WsReceive(void* User, void* Connection, uint32_t TimeoutMs,
                              const char** Message, size_t* Length)
{
    (void)User;
    NmosCurlConnection* c = Connection;
    if (c == NULL || Message == NULL || Length == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A WebSocket receives on a connection into a message.");
    }
    // The message handed out before is valid until this call.
    NmosBuffer_Free(&c->Message);
    const uint64_t Deadline = NmosOs_MonotonicMs() + TimeoutMs;
    for (;;)
    {
        char Data[16384];
        size_t Received = 0;
        const struct curl_ws_frame* Frame = NULL;
        const CURLcode Code =
            curl_ws_recv(c->Curl, Data, sizeof(Data), &Received, &Frame);
        if (Code == CURLE_AGAIN)
        {
            const uint64_t Now = NmosOs_MonotonicMs();
            if (Now >= Deadline)
            {
                return NmosError_Fail(DTNMOS_E_TIMEOUT, "No message came within %u ms.",
                                      (unsigned)TimeoutMs);
            }
            WaitReadable(c->Curl, (uint32_t)(Deadline - Now));
            continue;
        }
        if (Code != CURLE_OK)
        {
            return NmosError_Fail(DTNMOS_E_NETWORK, "The WebSocket failed: %s.",
                                  curl_easy_strerror(Code));
        }
        if ((Frame->flags & CURLWS_CLOSE) != 0)
        {
            return NmosError_Fail(DTNMOS_E_NETWORK, "The server closed the WebSocket.");
        }
        if ((Frame->flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT)) == 0)
        {
            // A ping or a pong, which libcurl answers itself.
            continue;
        }
        NmosBuffer_Append(&c->Partial, Data, Received);
        if (c->Partial.Failed)
        {
            NmosBuffer_Free(&c->Partial);
            return NmosError_FailMemory();
        }
        // The message is whole at the end of a frame that is not followed by another.
        if (Frame->bytesleft == 0 && (Frame->flags & CURLWS_CONT) == 0)
        {
            c->Message = c->Partial;
            memset(&c->Partial, 0, sizeof(c->Partial));
            *Message = c->Message.Data == NULL ? "" : c->Message.Data;
            *Length = c->Message.Length;
            return DTNMOS_OK;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WsClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WsClose(void* User, void* Connection)
{
    (void)User;
    NmosCurlConnection* c = Connection;
    if (c == NULL)
    {
        return;
    }
    size_t Sent = 0;
    curl_ws_send(c->Curl, "", 0, &Sent, 0, CURLWS_CLOSE);
    curl_easy_cleanup(c->Curl);
    NmosBuffer_Free(&c->Partial);
    NmosBuffer_Free(&c->Message);
    free(c);
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasCurlWebSocket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtNmos_HasCurlWebSocket(void)
{
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WsConnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult WsConnect(void* User, const char* Url, uint32_t TimeoutMs,
                              void** Connection)
{
    (void)User;
    (void)Url;
    (void)TimeoutMs;
    if (Connection != NULL)
    {
        *Connection = NULL;
    }
    return NmosError_Fail(
        DTNMOS_E_STATE, "dtnmos was built without libcurl, so it has no WebSocket of its "
                        "own; pass one.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WsReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult WsReceive(void* User, void* Connection, uint32_t TimeoutMs,
                              const char** Message, size_t* Length)
{
    (void)User;
    (void)Connection;
    (void)TimeoutMs;
    (void)Message;
    (void)Length;
    return NmosError_Fail(DTNMOS_E_STATE, "dtnmos was built without libcurl.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WsClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WsClose(void* User, void* Connection)
{
    (void)User;
    (void)Connection;
}

#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_CurlWebSocket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtNmosWebSocketTransport* DtNmos_CurlWebSocket(void)
{
    static const DtNmosWebSocketTransport Transport = {
        sizeof(DtNmosWebSocketTransport), NULL, WsConnect, WsReceive, WsClose};
    return &Transport;
}
