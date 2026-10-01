// #*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosServer.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The node serving itself when the library is built with DTNMOS_WITH_SERVER
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "NmosNode.h"

#ifdef DTNMOS_WITH_SERVER

    #include <civetweb.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    // The largest body of a request the node reads, far above what IS-05 sends.
    #define DTNMOS_MAX_REQUEST_BODY (1024 * 1024)

typedef struct NmosServer
{
    struct mg_context* context;
    NmosThread* poller;
    NmosMutex* mutex; // guards stopping
    int stopping;
} NmosServer;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasServer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtNmos_HasServer(void)
{
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- handle_request -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int handle_request(struct mg_connection* connection, void* user)
{
    DtNmosNode* node = user;
    const struct mg_request_info* info = mg_get_request_info(connection);
    NmosBuffer url;
    memset(&url, 0, sizeof(url));
    NmosBuffer_Printf(&url, "%s%s%s", info->local_uri,
                      info->query_string != NULL ? "?" : "",
                      info->query_string != NULL ? info->query_string : "");
    NmosBuffer body;
    memset(&body, 0, sizeof(body));
    NmosBuffer_Append(&body, "", 0);
    if (info->content_length > 0 && info->content_length <= DTNMOS_MAX_REQUEST_BODY)
    {
        char chunk[4096];
        int read = 0;
        while ((read = mg_read(connection, chunk, sizeof(chunk))) > 0)
        {
            NmosBuffer_Append(&body, chunk, (size_t)read);
        }
    }
    DtNmosHttpResponse* response = DtNmosHttpResponse_Alloc();
    if (url.failed || body.failed || response == NULL)
    {
        mg_send_http_error(connection, 500, "%s", "Out of memory.");
        NmosBuffer_Free(&url);
        NmosBuffer_Free(&body);
        DtNmosHttpResponse_Free(response);
        return 500;
    }
    DtNmosHttpRequest request;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Method = info->request_method;
    request.Url = url.data;
    request.ContentType = mg_get_header(connection, "Content-Type");
    request.Body = body.data;
    request.BodyLength = body.length;
    if (DtNmosNode_Handle(node, &request, response) != DTNMOS_OK)
    {
        NmosNode_AnswerError(response, 500, DtNmos_GetLastError());
    }
    const int status = DtNmosHttpResponse_Status(response);
    size_t length = 0;
    const char* answer = DtNmosHttpResponse_Body(response, &length);
    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", length);
    mg_response_header_start(connection, status);
    mg_response_header_add(connection, "Content-Type",
                           DtNmosHttpResponse_ContentType(response), -1);
    mg_response_header_add(connection, "Content-Length", content_length, -1);
    // Controllers that run in a browser ask the APIs from pages of other origins.
    mg_response_header_add(connection, "Access-Control-Allow-Origin", "*", -1);
    mg_response_header_send(connection);
    if (strcmp(info->request_method, "HEAD") != 0 && length > 0)
    {
        mg_write(connection, answer, length);
    }
    NmosBuffer_Free(&url);
    NmosBuffer_Free(&body);
    DtNmosHttpResponse_Free(response);
    return status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- stopping -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int stopping(NmosServer* s)
{
    NmosOs_MutexLock(s->mutex);
    const int result = s->stopping;
    NmosOs_MutexUnlock(s->mutex);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- poll_loop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void poll_loop(void* argument)
{
    DtNmosNode* node = argument;
    NmosServer* s = node->server;
    while (!stopping(s))
    {
        uint32_t next_ms = 1000;
        NmosError_Clear();
        if (DtNmosNode_Poll(node, &next_ms) != DTNMOS_OK && node->log != NULL)
        {
            node->log(node->log_user, DTNMOS_LOG_WARNING, DtNmos_GetLastError());
        }
        // Sleep in steps, so that stopping does not wait for a heartbeat.
        for (uint32_t slept = 0; slept < next_ms && slept < 5000 && !stopping(s);
             slept += 50)
        {
            NmosOs_SleepMs(50);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Serve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_Serve(DtNmosNode* node)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_Serve");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (node->server != NULL)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "The node serves already.");
    }
    if (node->api_port == 0)
    {
        node->api_port = NmosOs_FreePort(node->api_host);
        if (node->api_port == 0)
        {
            return NmosError_Fail(DTNMOS_E_HTTP, "No TCP port is free on %s.",
                                  node->api_host);
        }
    }
    NmosServer* s = calloc(1, sizeof(*s));
    if (s == NULL || (s->mutex = NmosOs_MutexCreate()) == NULL)
    {
        free(s);
        return NmosError_FailMemory();
    }
    char ports[300];
    const int ipv6 = strchr(node->api_host, ':') != NULL;
    snprintf(ports, sizeof(ports), "%s%s%s:%u", ipv6 ? "[" : "", node->api_host,
             ipv6 ? "]" : "", (unsigned)node->api_port);
    const char* options[] = {"listening_ports", ports, "num_threads", "4", NULL};
    mg_init_library(0);
    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    s->context = mg_start(&callbacks, NULL, options);
    if (s->context == NULL)
    {
        mg_exit_library();
        NmosOs_MutexFree(s->mutex);
        free(s);
        return NmosError_Fail(DTNMOS_E_HTTP, "The node cannot listen on %s.", ports);
    }
    mg_set_request_handler(s->context, "/", handle_request, node);
    node->server = s;
    s->poller = NmosOs_ThreadStart(poll_loop, node);
    if (s->poller == NULL)
    {
        NmosServer_Stop(node);
        return NmosError_Fail(DTNMOS_E_INTERNAL, "The node cannot start its thread.");
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosServer_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosServer_Stop(DtNmosNode* node)
{
    NmosServer* s = node->server;
    if (s == NULL)
    {
        return;
    }
    NmosOs_MutexLock(s->mutex);
    s->stopping = 1;
    NmosOs_MutexUnlock(s->mutex);
    NmosOs_ThreadJoin(s->poller);
    mg_stop(s->context);
    mg_exit_library();
    NmosOs_MutexFree(s->mutex);
    free(s);
    node->server = NULL;
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasServer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtNmos_HasServer(void)
{
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Serve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_Serve(DtNmosNode* node)
{
    (void)node;
    return NmosError_Fail(
        DTNMOS_E_STATE,
        "dtnmos was built without its server; answer the requests of the node "
        "with DtNmosNode_Handle() and poll it with DtNmosNode_Poll().");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosServer_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosServer_Stop(DtNmosNode* node)
{
    (void)node;
}

#endif
