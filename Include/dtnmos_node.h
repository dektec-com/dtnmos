// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_node.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - An NMOS node: registration (IS-04 v1.3) and connection management (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos.h"
#include "dtnmos_http.h"
#include "dtnmos_sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct DtNmosNode DtNmosNode;

// Called by DtNmosNode_Poll(), on its thread and without the lock of the node, when the
// registry has failed failures polls in a row: requests that got no answer, or an error
// status. A registry that answers a heartbeat with 404 has lost the node, which registers
// again with it, and that is no failure. The function returns 1 after setting next_url
// to the base URL of another registry, which the node then registers with from the start,
// or 0 to stay with the one it has.
typedef int (*DtNmosRegistryFailedFunc)(void* user, uint32_t failures,
                                        DtNmosString* next_url);

typedef struct DtNmosNodeConfig
{
    size_t Size; // sizeof(DtNmosNodeConfig)
    DtNmosId Id; // stable, e.g. from DtNmosId_FromName()
    const char* Label;
    const char* Description;
    const char* Hostname;
    // The address the Node and the Connection API are reached at; null finds the address
    // of this host on the way to the registry.
    const char* ApiHost;
    uint16_t ApiPort;            // 0 lets DtNmosNode_Serve() take any free port
    const char* RegistrationUrl; // base URL of the registry, e.g. "http://registry.local"
    const char* ApiVersion;      // of IS-04; "v1.3" when null, the only one accepted yet
    DtNmosHttpFunc Http;         // for the requests to the registry
    void* HttpUser;
    uint32_t TimeoutMs;   // of each request to the registry; 5000 when 0
    uint32_t HeartbeatMs; // 5000 when 0
    DtNmosLogFunc Log;    // optional
    void* LogUser;
    // Optional: moves the node to another registry when its registry fails.
    DtNmosRegistryFailedFunc RegistryFailed;
    void* RegistryFailedUser;
    uint32_t FailuresBeforeSwitch; // polls that fail in a row first; 3 when 0
} DtNmosNodeConfig;

typedef struct DtNmosDeviceConfig
{
    size_t Size;
    DtNmosId Id;
    const char* Label; // e.g. "DTA-2110 2110000076 port 1"
    const char* Description;
} DtNmosDeviceConfig;

typedef struct DtNmosSenderConfig
{
    size_t Size;
    DtNmosId Id;
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    const DtNmosFlow* Flow; // what it sends, video or audio, written as its SDP; copied
    const char* SourceIp;   // the address it sends from, in its SDP
} DtNmosSenderConfig;

typedef struct DtNmosReceiverConfig
{
    size_t Size;
    DtNmosId Id;
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    DtNmosMedia Media; // video or audio
} DtNmosReceiverConfig;

// What a controller activates on a receiver (IS-05): whether it receives, and the flow it
// receives: the flow of the media of the receiver in the transport file, with the
// multicast_ip, source_ip and destination_port of the transport parameters over it.
// HasFlow is 0 when the staged parameters hold no transport file, as the parameters
// alone do not describe a flow. The node owns what it passes to a callback, which copies
// what it keeps with DtNmosFlow_Copy().
typedef struct DtNmosReceiverActivation
{
    int MasterEnable;
    int HasFlow;
    DtNmosFlow Flow;
    DtNmosId SenderId; // empty when not given
} DtNmosReceiverActivation;

// What a controller activates on a sender: whether it sends, and where to, "auto"
// resolved to where it sends now; SourceIp is empty for "auto".
typedef struct DtNmosSenderActivation
{
    int MasterEnable;
    DtNmosString DestinationIp;
    uint16_t DestinationPort;
    DtNmosString SourceIp;
} DtNmosSenderActivation;

// Called when a controller activates a receiver or a sender. The callback applies it and
// returns DTNMOS_OK, or fails with DtNmos_SetLastError(), whose message the node answers
// the controller with. It is called on the thread that handles the request, without the
// lock of the node, and may block for as long as applying takes.
typedef DtNmosResult (*DtNmosReceiverActivateFunc)(
    void* user, const DtNmosId* receiver, const DtNmosReceiverActivation* activation);
typedef DtNmosResult (*DtNmosSenderActivateFunc)(
    void* user, const DtNmosId* sender, const DtNmosSenderActivation* activation);

// Creates a node of config, whose strings it copies. Fails with DTNMOS_E_INVALID_ARGUMENT
// without an ID, a registration URL or an HTTP function.
DTNMOS_API DtNmosResult DtNmosNode_Create(const DtNmosNodeConfig* config,
                                          DtNmosNode** node);

// Stops serving, deletes what the node registered from the registry, and frees it.
DTNMOS_API void DtNmosNode_Destroy(DtNmosNode* node);

// Adds a device, sender or receiver; the next poll registers it. Fails with
// DTNMOS_E_INVALID_ARGUMENT for an ID the node has, an unknown device, or a sender of
// neither video nor audio.
DTNMOS_API DtNmosResult DtNmosNode_AddDevice(DtNmosNode* node,
                                             const DtNmosDeviceConfig* device);
DTNMOS_API DtNmosResult DtNmosNode_AddSender(DtNmosNode* node,
                                             const DtNmosSenderConfig* sender,
                                             DtNmosSenderActivateFunc activate,
                                             void* user);
DTNMOS_API DtNmosResult DtNmosNode_AddReceiver(DtNmosNode* node,
                                               const DtNmosReceiverConfig* receiver,
                                               DtNmosReceiverActivateFunc activate,
                                               void* user);

// Removes a device, sender or receiver, and the senders and receivers of a device; the
// next poll deletes them from the registry. Fails with DTNMOS_E_NOT_FOUND for an unknown
// ID.
DTNMOS_API DtNmosResult DtNmosNode_Remove(DtNmosNode* node, const DtNmosId* id);

// Changes the flow a sender sends, e.g. after its format changed, and its SDP with it;
// the next poll registers the new version.
DTNMOS_API DtNmosResult DtNmosNode_UpdateSender(DtNmosNode* node, const DtNmosId* id,
                                                const DtNmosFlow* flow);

// Registers what is not registered yet, deletes what was removed, and sends a heartbeat
// when one is due, registering everything again when the registry has lost the node. Sets
// next_ms, when it is not null, to when it wants to be called again. Fails with the first
// request that failed; the next poll tries again.
DTNMOS_API DtNmosResult DtNmosNode_Poll(DtNmosNode* node, uint32_t* next_ms);

// Whether the registry holds the node and everything it has.
DTNMOS_API int DtNmosNode_IsRegistered(const DtNmosNode* node);

// Answers a request to the Node API or the Connection API, for a caller with an HTTP
// server of its own; request->url is the path and query of the request. Fills response,
// which is empty, with the answer, an error status included.
DTNMOS_API DtNmosResult DtNmosNode_Handle(DtNmosNode* node,
                                          const DtNmosHttpRequest* request,
                                          DtNmosHttpResponse* response);

// Serves the Node API and the Connection API on api_host and api_port on a civetweb
// server, and polls the node on a thread of its own, until DtNmosNode_Destroy(). Fails
// with DTNMOS_E_STATE when the library is built without DTNMOS_WITH_SERVER, and with
// DTNMOS_E_HTTP when the server cannot listen.
DTNMOS_API DtNmosResult DtNmosNode_Serve(DtNmosNode* node);

// Whether the library was built with the server, DTNMOS_WITH_SERVER.
DTNMOS_API int DtNmos_HasServer(void);

// Returns the port the node is reached at: api_port, or the one DtNmosNode_Serve() took.
DTNMOS_API uint16_t DtNmosNode_ApiPort(const DtNmosNode* node);

// Writes the base URL of the APIs of the node into url, e.g. "http://192.168.1.5:8080".
DTNMOS_API DtNmosResult DtNmosNode_ApiUrl(const DtNmosNode* node, DtNmosString* url);

#ifdef __cplusplus
}
#endif
