// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_node.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - An NMOS node: registration (IS-04 v1.3) and connection management (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos.h"
#include "dtnmos_http.h"
#include "dtnmos_query.h"
#include "dtnmos_sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct DtNmosNode DtNmosNode;

// The size of the buffer a DtNmosRegistryFailedFunc writes the URL of a registry into.
#define DTNMOS_MAX_URL_SIZE 2048

// Called by DtNmosNode_Poll(), on its thread and without the lock of the node, when the
// registry has failed failures polls in a row: requests that got no answer, or an error
// status. A registry that answers a heartbeat with 404 has lost the node, which registers
// again with it, and that is no failure. The function returns 1 after writing into
// NextUrl, of size bytes, DTNMOS_MAX_URL_SIZE, the base URL of another registry and its
// null character, which the node then registers with from the start, or 0 to stay with
// the one it has. A URL without its null character within size bytes is ignored.
typedef int (*DtNmosRegistryFailedFunc)(void* User, uint32_t Failures, char* NextUrl,
                                        size_t Size);

typedef struct DtNmosNodeConfig
{
    size_t Size; // sizeof(DtNmosNodeConfig)
    DtNmosId Id; // stable, e.g. from DtNmosId_FromName()
    const char* Label;
    const char* Description;
    const char* Hostname;
    // The address the Node and the Connection API are reached at; null finds the address
    // of this host on the way to the registry, or, when the node takes its registry from
    // a search, that of the default route.
    const char* ApiHost;
    uint16_t ApiPort; // 0 lets DtNmosNode_Serve() take any free port
    // The base URL of the registry, e.g. "http://registry.local"; null takes the
    // registries of Search.
    const char* RegistrationUrl;
    const char* ApiVersion; // of IS-04; "v1.3" when null, the only one accepted yet
    DtNmosHttpFunc Http;    // for the requests to the registry
    void* HttpUser;
    uint32_t TimeoutMs;   // of each request to the registry; 5000 when 0
    uint32_t HeartbeatMs; // 5000 when 0
    DtNmosLogFunc Log;    // optional
    void* LogUser;
    // Optional: moves the node to another registry when its registry fails.
    DtNmosRegistryFailedFunc RegistryFailed;
    void* RegistryFailedUser;
    // Polls that fail in a row first; when 0, 3, or 1 for a node that searches for its
    // registry, as IS-04 has such a node move on at the first failure.
    uint32_t FailuresBeforeSwitch;
    // The search of the application, which finds the Registration API, for a node without
    // a RegistrationUrl; the node borrows it, and is closed before it. The node registers
    // with the most preferred usable registry the search found; when one fails
    // FailuresBeforeSwitch polls in a row, it asks RegistryFailed, when that is set, and
    // else moves to the most preferred one that has not failed yet, with a heartbeat
    // first, as IS-04 asks of a node; once all have failed, it starts over from the most
    // preferred. While it has none, it has the search search sooner.
    DtNmosRegistrySearch* Search;
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
    // What it sends, video or audio, written as its SDP; copied. A reference clock of
    // localmac is written with the MAC address of the interface SourceIp is on.
    const DtNmosFlow* Flow;
    // The address it sends from, that of the network port of the card; required. Its
    // source_ip "auto" stands for it, and its SDP gives it as origin and source-filter.
    const char* SourceIp;
} DtNmosSenderConfig;

typedef struct DtNmosReceiverConfig
{
    size_t Size;
    DtNmosId Id;
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    DtNmosMedia Media; // video or audio
    // The address it receives on, that of the network port of the card; required. Its
    // interface_ip "auto" stands for it.
    const char* InterfaceIp;
} DtNmosReceiverConfig;

// What a controller activates on a receiver (IS-05): whether it receives, and the flow it
// receives: the flow of the media of the receiver in the transport file, with the
// multicast_ip, source_ip and destination_port of the transport parameters over it.
// HasFlow is 0 when the staged parameters hold no transport file, as the parameters
// alone do not describe a flow. The node owns the strings of the flow it passes to a
// callback, valid during the callback; the callback copies what it keeps.
typedef struct DtNmosReceiverActivation
{
    int MasterEnable;
    int HasFlow;
    DtNmosFlow Flow;
    DtNmosId SenderId; // empty when not given
} DtNmosReceiverActivation;

// What a controller activates on a sender: whether it sends, where to and from where,
// with "auto" resolved: to where it sends now, and to the SourceIp of its config.
typedef struct DtNmosSenderActivation
{
    int MasterEnable;
    char DestinationIp[DTNMOS_MAX_ADDRESS_SIZE];
    uint16_t DestinationPort;
    char SourceIp[DTNMOS_MAX_ADDRESS_SIZE];
} DtNmosSenderActivation;

// Called when a controller activates a receiver or a sender. The callback applies it and
// returns DTNMOS_OK, or fails with DtNmos_SetLastError(), whose message the node answers
// the controller with. It is called without the lock of the node, and may block for as
// long as applying takes: on the thread that handles the request for an immediate
// activation, and in DtNmosNode_Poll() for a scheduled one, when it is due; the failure
// of a scheduled one goes to the log.
typedef DtNmosResult (*DtNmosReceiverActivateFunc)(
    void* User, const DtNmosId* Receiver, const DtNmosReceiverActivation* Activation);

typedef DtNmosResult (*DtNmosSenderActivateFunc)(
    void* User, const DtNmosId* Sender, const DtNmosSenderActivation* Activation);

// A function of a node other than _Alloc(), _Open(), _Free() and _Freep() needs an open
// node, and fails with DTNMOS_E_STATE on another; one that returns no result returns 0.

// Whether the library was built with the server, DTNMOS_WITH_SERVER.
DTNMOS_API int DtNmos_HasServer(void);

// Adds a device; the next poll registers it. Fails with DTNMOS_E_INVALID_ARGUMENT for an
// ID the node has.
DTNMOS_API DtNmosResult DtNmosNode_AddDevice(DtNmosNode* Node,
                                             const DtNmosDeviceConfig* Device);

// Adds a receiver of a device the node has, which Activate is called for when a
// controller activates it; the next poll registers it. Fails as DtNmosNode_AddDevice()
// does, for an unknown device, and without an InterfaceIp.
DTNMOS_API DtNmosResult DtNmosNode_AddReceiver(DtNmosNode* Node,
                                               const DtNmosReceiverConfig* Receiver,
                                               DtNmosReceiverActivateFunc Activate,
                                               void* User);

// Adds a sender of a device the node has, which Activate is called for when a controller
// activates it; the next poll registers it. Fails as DtNmosNode_AddDevice() does, for an
// unknown device, for a sender of neither video nor audio, and without a SourceIp.
DTNMOS_API DtNmosResult DtNmosNode_AddSender(DtNmosNode* Node,
                                             const DtNmosSenderConfig* Sender,
                                             DtNmosSenderActivateFunc Activate,
                                             void* User);

// Allocates a node, closed. Returns null when the memory ran out.
DTNMOS_API DtNmosNode* DtNmosNode_Alloc(void);

// Returns the port the node is reached at: ApiPort, or the one DtNmosNode_Serve() took.
DTNMOS_API uint16_t DtNmosNode_ApiPort(const DtNmosNode* Node);

// Writes the base URL of the APIs of the node, e.g. "http://192.168.1.5:8080", into the
// caller's buffer of *Size bytes, with a terminating null; *Size is then its length. A
// buffer too small fails with DTNMOS_E_BUFFER_TOO_SMALL, *Size then giving the bytes
// needed.
DTNMOS_API DtNmosResult DtNmosNode_ApiUrl(const DtNmosNode* Node, char* Buffer,
                                          size_t* Size);

// Stops serving, deletes what the node registered from the registry, and forgets it all,
// leaving the node closed. Fails with DTNMOS_E_STATE when the node is not open.
DTNMOS_API DtNmosResult DtNmosNode_Close(DtNmosNode* Node);

// Closes the node when it is open, and frees it. Null is allowed.
DTNMOS_API void DtNmosNode_Free(DtNmosNode* Node);

// Frees *Node as DtNmosNode_Free() does and sets *Node to null. Null is allowed.
DTNMOS_API void DtNmosNode_Freep(DtNmosNode** Node);

// Answers a request to the Node API or the Connection API, for a caller with an HTTP
// server of its own; Request->Url is the path and query of the request. Fills response,
// which is empty, with the answer, an error status included.
DTNMOS_API DtNmosResult DtNmosNode_Handle(DtNmosNode* Node,
                                          const DtNmosHttpRequest* Request,
                                          DtNmosHttpResponse* Response);

// Whether the registry holds the node and everything it has.
DTNMOS_API int DtNmosNode_IsRegistered(const DtNmosNode* Node);

// Opens node with config, whose strings it copies; it registers nothing until it is
// polled. Fails with DTNMOS_E_INVALID_ARGUMENT without an ID or an HTTP function, or with
// neither a RegistrationUrl nor a Search, and with DTNMOS_E_STATE when the node is open.
// A node closed can be opened again, with another config, keeping its handle.
DTNMOS_API DtNmosResult DtNmosNode_Open(DtNmosNode* Node, const DtNmosNodeConfig* Config);

// Applies the scheduled activations that are due, takes a registry the search found when
// the node has none, registers what is not registered yet, deletes what was removed, and
// sends a heartbeat when one is due, registering everything again when the registry has
// lost the node. A registry that answers the first registration of the node with 200
// holds an old node of its ID, which the node deletes and registers again. Having no
// registry is no failure. Sets NextMs, when it is not null, to when it wants to be called
// again, which a scheduled activation or a move to another registry brings forward.
// Fails with the first request that failed; the next poll tries again.
DTNMOS_API DtNmosResult DtNmosNode_Poll(DtNmosNode* Node, uint32_t* NextMs);

// Removes a device, sender or receiver, and the senders and receivers of a device; the
// next poll deletes them from the registry. Fails with DTNMOS_E_NOT_FOUND for an unknown
// ID.
DTNMOS_API DtNmosResult DtNmosNode_Remove(DtNmosNode* Node, const DtNmosId* Id);

// Serves the Node API and the Connection API on ApiHost and ApiPort on a civetweb
// server, and polls the node on a thread of its own, until DtNmosNode_Close(). Fails
// with DTNMOS_E_STATE when the library is built without DTNMOS_WITH_SERVER, and with
// DTNMOS_E_HTTP when the server cannot listen.
DTNMOS_API DtNmosResult DtNmosNode_Serve(DtNmosNode* Node);

// Changes the flow a sender sends, e.g. after its format changed, and its SDP with it;
// the next poll registers the new version.
DTNMOS_API DtNmosResult DtNmosNode_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                                const DtNmosFlow* Flow);

#ifdef __cplusplus
}
#endif
