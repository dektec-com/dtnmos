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
    struct mg_context* Context;
    NmosThread* Poller;
    NmosMutex* Mutex; // guards stopping
    int Stopping;
} NmosServer;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasServer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtNmos_HasServer(void)
{
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HandleRequest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int HandleRequest(struct mg_connection* Connection, void* User)
{
    DtNmosNode* Node = User;
    const struct mg_request_info* Info = mg_get_request_info(Connection);
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    NmosBuffer_Printf(&Url, "%s%s%s", Info->local_uri,
                      Info->query_string != NULL ? "?" : "",
                      Info->query_string != NULL ? Info->query_string : "");
    NmosBuffer Body;
    memset(&Body, 0, sizeof(Body));
    NmosBuffer_Append(&Body, "", 0);
    if (Info->content_length > 0 && Info->content_length <= DTNMOS_MAX_REQUEST_BODY)
    {
        char Chunk[4096];
        int Read = 0;
        while ((Read = mg_read(Connection, Chunk, sizeof(Chunk))) > 0)
        {
            NmosBuffer_Append(&Body, Chunk, (size_t)Read);
        }
    }
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    if (Url.Failed || Body.Failed || Response == NULL)
    {
        mg_send_http_error(Connection, 500, "%s", "Out of memory.");
        NmosBuffer_Free(&Url);
        NmosBuffer_Free(&Body);
        DtNmosHttpResponse_Free(Response);
        return 500;
    }
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = Info->request_method;
    Request.Url = Url.Data;
    Request.ContentType = mg_get_header(Connection, "Content-Type");
    Request.Body = Body.Data;
    Request.BodyLength = Body.Length;
    if (DtNmosNode_Handle(Node, &Request, Response) != DTNMOS_OK)
    {
        NmosNode_AnswerError(Response, 500, DtNmos_GetLastError());
    }
    const int Status = DtNmosHttpResponse_Status(Response);
    size_t Length = 0;
    const char* Answer = DtNmosHttpResponse_Body(Response, &Length);
    char ContentLength[32];
    snprintf(ContentLength, sizeof(ContentLength), "%zu", Length);
    mg_response_header_start(Connection, Status);
    const char* ContentType = DtNmosHttpResponse_ContentType(Response);
    if (ContentType != NULL && ContentType[0] != '\0')
    {
        mg_response_header_add(Connection, "Content-Type", ContentType, -1);
    }
    mg_response_header_add(Connection, "Content-Length", ContentLength, -1);
    // The headers of the answer of the node, those of CORS among them.
    for (size_t i = 0; i < DtNmosHttpResponse_HeaderCount(Response); ++i)
    {
        const DtNmosHttpHeader Header = DtNmosHttpResponse_Header(Response, i);
        mg_response_header_add(Connection, Header.Name, Header.Value, -1);
    }
    mg_response_header_send(Connection);
    if (strcmp(Info->request_method, "HEAD") != 0 && Length > 0)
    {
        mg_write(Connection, Answer, Length);
    }
    NmosBuffer_Free(&Url);
    NmosBuffer_Free(&Body);
    DtNmosHttpResponse_Free(Response);
    return Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Stopping -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int Stopping(NmosServer* s)
{
    NmosOs_MutexLock(s->Mutex);
    const int Result = s->Stopping;
    NmosOs_MutexUnlock(s->Mutex);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PollLoop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PollLoop(void* Argument)
{
    DtNmosNode* Node = Argument;
    NmosServer* s = Node->Server;
    while (!Stopping(s))
    {
        uint32_t NextMs = 1000;
        NmosError_Clear();
        if (DtNmosNode_Poll(Node, &NextMs) != DTNMOS_OK && Node->Log != NULL)
        {
            Node->Log(Node->LogUser, DTNMOS_LOG_WARNING, DtNmos_GetLastError());
        }
        // Sleep in steps, so that stopping does not wait for a heartbeat.
        for (uint32_t Slept = 0; Slept < NextMs && Slept < 5000 && !Stopping(s);
             Slept += 50)
        {
            NmosOs_SleepMs(50);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Serve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_Serve(DtNmosNode* Node)
{
    const DtNmosResult Open = NmosNode_CheckOpen(Node, "DtNmosNode_Serve");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Node->Server != NULL)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "The node serves already.");
    }
    if (Node->ApiPort == 0)
    {
        Node->ApiPort = NmosOs_FreePort(Node->ApiHost);
        if (Node->ApiPort == 0)
        {
            return NmosError_Fail(DTNMOS_E_HTTP, "No TCP port is free on %s.",
                                  Node->ApiHost);
        }
    }
    NmosServer* s = calloc(1, sizeof(*s));
    if (s == NULL || (s->Mutex = NmosOs_MutexCreate()) == NULL)
    {
        free(s);
        return NmosError_FailMemory();
    }
    char Ports[300];
    const int Ipv6 = strchr(Node->ApiHost, ':') != NULL;
    snprintf(Ports, sizeof(Ports), "%s%s%s:%u", Ipv6 ? "[" : "", Node->ApiHost,
             Ipv6 ? "]" : "", (unsigned)Node->ApiPort);
    // civetweb answers a browser's preflight itself, before the node sees it, and by
    // default allows only the method that was asked for. The node allows all of them.
    const char* Options[] = {"listening_ports",
                             Ports,
                             "num_threads",
                             "4",
                             "access_control_allow_methods",
                             "GET, PUT, POST, PATCH, HEAD, OPTIONS, DELETE",
                             "access_control_allow_headers",
                             "Content-Type, Accept",
                             NULL};
    mg_init_library(0);
    struct mg_callbacks Callbacks;
    memset(&Callbacks, 0, sizeof(Callbacks));
    s->Context = mg_start(&Callbacks, NULL, Options);
    if (s->Context == NULL)
    {
        mg_exit_library();
        NmosOs_MutexFree(s->Mutex);
        free(s);
        return NmosError_Fail(DTNMOS_E_HTTP, "The node cannot listen on %s.", Ports);
    }
    mg_set_request_handler(s->Context, "/", HandleRequest, Node);
    Node->Server = s;
    s->Poller = NmosOs_ThreadStart(PollLoop, Node);
    if (s->Poller == NULL)
    {
        NmosServer_Stop(Node);
        return NmosError_Fail(DTNMOS_E_INTERNAL, "The node cannot start its thread.");
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosServer_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosServer_Stop(DtNmosNode* Node)
{
    NmosServer* s = Node->Server;
    if (s == NULL)
    {
        return;
    }
    NmosOs_MutexLock(s->Mutex);
    s->Stopping = 1;
    NmosOs_MutexUnlock(s->Mutex);
    NmosOs_ThreadJoin(s->Poller);
    mg_stop(s->Context);
    mg_exit_library();
    NmosOs_MutexFree(s->Mutex);
    free(s);
    Node->Server = NULL;
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
DtNmosResult DtNmosNode_Serve(DtNmosNode* Node)
{
    (void)Node;
    return NmosError_Fail(
        DTNMOS_E_STATE,
        "dtnmos was built without its server; answer the requests of the node "
        "with DtNmosNode_Handle() and poll it with DtNmosNode_Poll().");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosServer_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosServer_Stop(DtNmosNode* Node)
{
    (void)Node;
}

#endif
