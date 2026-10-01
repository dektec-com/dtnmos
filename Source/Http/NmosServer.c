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

typedef struct server
{
    struct mg_context* context;
    dtnmos_thread* poller;
    dtnmos_mutex* mutex; // guards stopping
    int stopping;
} server;

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
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%s%s%s", info->local_uri,
                         info->query_string != NULL ? "?" : "",
                         info->query_string != NULL ? info->query_string : "");
    dtnmos_buffer body;
    memset(&body, 0, sizeof(body));
    dtnmos_buffer_append(&body, "", 0);
    if (info->content_length > 0 && info->content_length <= DTNMOS_MAX_REQUEST_BODY)
    {
        char chunk[4096];
        int read = 0;
        while ((read = mg_read(connection, chunk, sizeof(chunk))) > 0)
        {
            dtnmos_buffer_append(&body, chunk, (size_t)read);
        }
    }
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    if (url.failed || body.failed || response == NULL)
    {
        mg_send_http_error(connection, 500, "%s", "Out of memory.");
        dtnmos_buffer_free(&url);
        dtnmos_buffer_free(&body);
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
        dtnmos_node_answer_error(response, 500, DtNmos_GetLastError());
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
    dtnmos_buffer_free(&url);
    dtnmos_buffer_free(&body);
    DtNmosHttpResponse_Free(response);
    return status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- stopping -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int stopping(server* s)
{
    dtnmos_mutex_lock(s->mutex);
    const int result = s->stopping;
    dtnmos_mutex_unlock(s->mutex);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- poll_loop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void poll_loop(void* argument)
{
    DtNmosNode* node = argument;
    server* s = node->server;
    while (!stopping(s))
    {
        uint32_t next_ms = 1000;
        dtnmos_clear_error();
        if (DtNmosNode_Poll(node, &next_ms) != DTNMOS_OK && node->log != NULL)
        {
            node->log(node->log_user, DTNMOS_LOG_WARNING, DtNmos_GetLastError());
        }
        // Sleep in steps, so that stopping does not wait for a heartbeat.
        for (uint32_t slept = 0; slept < next_ms && slept < 5000 && !stopping(s);
             slept += 50)
        {
            dtnmos_sleep_ms(50);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Serve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_Serve(DtNmosNode* node)
{
    if (node == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT, "DtNmosNode_Serve() needs a node.");
    }
    if (node->server != NULL)
    {
        return dtnmos_fail(DTNMOS_E_STATE, "The node serves already.");
    }
    if (node->api_port == 0)
    {
        node->api_port = dtnmos_free_port(node->api_host);
        if (node->api_port == 0)
        {
            return dtnmos_fail(DTNMOS_E_HTTP, "No TCP port is free on %s.",
                               node->api_host);
        }
    }
    server* s = calloc(1, sizeof(*s));
    if (s == NULL || (s->mutex = dtnmos_mutex_create()) == NULL)
    {
        free(s);
        return dtnmos_fail_memory();
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
        dtnmos_mutex_free(s->mutex);
        free(s);
        return dtnmos_fail(DTNMOS_E_HTTP, "The node cannot listen on %s.", ports);
    }
    mg_set_request_handler(s->context, "/", handle_request, node);
    node->server = s;
    s->poller = dtnmos_thread_start(poll_loop, node);
    if (s->poller == NULL)
    {
        dtnmos_server_stop(node);
        return dtnmos_fail(DTNMOS_E_INTERNAL, "The node cannot start its thread.");
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_server_stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_server_stop(DtNmosNode* node)
{
    server* s = node->server;
    if (s == NULL)
    {
        return;
    }
    dtnmos_mutex_lock(s->mutex);
    s->stopping = 1;
    dtnmos_mutex_unlock(s->mutex);
    dtnmos_thread_join(s->poller);
    mg_stop(s->context);
    mg_exit_library();
    dtnmos_mutex_free(s->mutex);
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
    return dtnmos_fail(
        DTNMOS_E_STATE,
        "dtnmos was built without its server; answer the requests of the node "
        "with DtNmosNode_Handle() and poll it with DtNmosNode_Poll().");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_server_stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_server_stop(DtNmosNode* node)
{
    (void)node;
}

#endif
