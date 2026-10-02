// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_node.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - An NMOS node: registration (IS-04 v1.3) and connection management (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A node is the program as NMOS sees it. It holds devices, and each device holds senders
// and receivers. The node registers them with an NMOS registry, where controllers find
// them, and serves the Node API and the Connection API, through which a controller
// connects a sender to a receiver. A program uses a node in these steps:
//
// 1. DtNmosNode_Alloc, then DtNmosNode_Open with the registry and the node's ID.
// 2. DtNmosNode_AddDevice, then DtNmosNode_AddSender and DtNmosNode_AddReceiver, each
//    with a callback that is called when a controller activates it.
// 3. DtNmosNode_Serve, which serves the APIs and keeps the registration up to date on a
//    thread of its own. A program with an HTTP server of its own calls
//    DtNmosNode_Handle for each request and DtNmosNode_Poll regularly instead.
// 4. DtNmosNode_Close, which also unregisters everything, and DtNmosNode_Free.

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

// The size of the buffer a DtNmosRegistryFailedFunc writes a registry's URL into.
#define DTNMOS_MAX_URL_SIZE 2048

// The program's function that chooses another registry when the node's registry keeps
// failing: Failures polls in a row got no answer, or an error. (A registry that answers
// a heartbeat with 404 has lost the node; the node registers again, which is not a
// failure.) The node calls it from DtNmosNode_Poll(), on that thread.
//
// To move the node, write the base URL of another registry, with its null, into NextUrl
// (Size bytes, DTNMOS_MAX_URL_SIZE) and return true; the node then registers with it from
// scratch. Return false to stay with the current registry.
typedef bool (*DtNmosRegistryFailedFunc)(void* User, uint32_t Failures, char* NextUrl,
                                         size_t Size);

// How a node is opened. Strings are copied.
typedef struct DtNmosNodeConfig
{
    size_t Size; // sizeof(DtNmosNodeConfig)
    DtNmosId Id; // The node's ID; keep it the same across runs, e.g. with
                 // DtNmosId_FromName()
    const char* Label;
    const char* Description;
    const char* Hostname;
    // The address at which controllers reach the node's APIs. NULL: the address of this
    // host on the route to the registry, or, with Search, on the default route.
    const char* ApiHost;
    uint16_t ApiPort; // The port of the APIs; 0 lets DtNmosNode_Serve() pick a free one
    // The base URL of the registry, e.g. "http://registry.local". NULL: use Search.
    const char* RegistrationUrl;
    const char* ApiVersion; // The IS-04 version; NULL for "v1.3", the only one supported
    DtNmosHttpFunc Http;    // Sends the requests to the registry, e.g. DtNmos_CurlHttp
    void* HttpUser;         // Passed to Http
    uint32_t TimeoutMs;     // How long a request to the registry may take; 5000 when 0
    uint32_t HeartbeatMs;   // Time between heartbeats to the registry; 5000 when 0
    DtNmosLogFunc Log;      // Receives log messages; may be NULL
    void* LogUser;          // Passed to Log
    // Chooses another registry when the registry fails; may be NULL.
    DtNmosRegistryFailedFunc RegistryFailed;
    void* RegistryFailedUser; // Passed to RegistryFailed
    // How many polls in a row must fail before the node moves on. 0: 3, or 1 for a node
    // that uses Search, as IS-04 asks.
    uint32_t FailuresBeforeSwitch;
    // A search for registries (DtNmosRegistrySearch_Open) the node uses when it has no
    // RegistrationUrl. The program keeps it open while the node is open. The node uses
    // the most preferred registry found. When that one fails, the node asks
    // RegistryFailed if set, and otherwise moves to the next registry that has not failed
    // yet; after all have failed, it starts again from the first.
    DtNmosRegistrySearch* Search;
} DtNmosNodeConfig;

// How a device is added. Strings are copied.
typedef struct DtNmosDeviceConfig
{
    size_t Size;       // sizeof(DtNmosDeviceConfig)
    DtNmosId Id;       // The device's ID; keep it the same across runs
    const char* Label; // e.g. "DTA-2110 2110000076 port 1"
    const char* Description;
} DtNmosDeviceConfig;

// How a sender is added. Strings and the flow are copied.
typedef struct DtNmosSenderConfig
{
    size_t Size;       // sizeof(DtNmosSenderConfig)
    DtNmosId Id;       // The sender's ID; keep it the same across runs
    DtNmosId DeviceId; // The device it belongs to
    const char* Label;
    const char* Description;
    // What it sends, video or audio. The node serves it as the sender's SDP. A reference
    // clock of DTNMOS_REFCLOCK_LOCALMAC gets the MAC address of SourceIp's interface.
    const DtNmosFlow* Flow;
    // The address it sends from, e.g. that of a card's network port. Required. The SDP
    // gives it as the origin and the source filter.
    const char* SourceIp;
    // How long the program needs to apply an activation, in milliseconds. The callback
    // of a scheduled activation is called this much early. 0: at the time itself.
    uint32_t ActivationLeadMs;
} DtNmosSenderConfig;

// How a receiver is added. Strings are copied.
typedef struct DtNmosReceiverConfig
{
    size_t Size;       // sizeof(DtNmosReceiverConfig)
    DtNmosId Id;       // The receiver's ID; keep it the same across runs
    DtNmosId DeviceId; // The device it belongs to
    const char* Label;
    const char* Description;
    DtNmosMedia Media; // What it receives: DTNMOS_MEDIA_VIDEO or DTNMOS_MEDIA_AUDIO
    // The address it receives on, e.g. that of a card's network port. Required.
    const char* InterfaceIp;
    // As in DtNmosSenderConfig.
    uint32_t ActivationLeadMs;
    // The stream the receiver receives from the start, until a controller connects it,
    // which the node gives as its active transport parameters. NULL or empty: any
    // source, or no group, which is unicast to InterfaceIp.
    const char* SourceIp;     // The one source it takes
    const char* MulticastIp;  // The group it has joined
    uint16_t DestinationPort; // The UDP port it receives on; 0 for 5004
} DtNmosReceiverConfig;

// What a controller asks of a receiver: whether to receive, and which stream.
//
// With a transport file (an SDP), Flow is the flow in it of the receiver's media, with
// the address, source and port the controller set. Without one, HasFlow is false and
// Flow gives only the transport: Media is the receiver's, the destination, source and
// port are those the controller set, and Format is empty, so that the receiver keeps its
// format and only moves to the new stream.
//
// The flow's strings belong to the node and are valid only during the callback.
typedef struct DtNmosReceiverActivation
{
    bool MasterEnable; // False: stop receiving
    bool HasFlow;      // True: Flow comes from a transport file and has a format
    DtNmosFlow Flow;   // The stream to receive
    DtNmosId SenderId; // The sender the controller connects, or empty
    // When the change takes effect, in nanoseconds of TAI since the PTP epoch: the time a
    // scheduled activation asks for, or now for an immediate one. A callback called
    // ActivationLeadMs early may wait until then.
    uint64_t AtNs;
} DtNmosReceiverActivation;

// What a controller asks of a sender: whether to send, and where to. "auto" in the
// controller's request is already replaced by the current destination and the
// SourceIp of the sender's config.
typedef struct DtNmosSenderActivation
{
    bool MasterEnable;                           // False: stop sending
    char DestinationIp[DTNMOS_MAX_ADDRESS_SIZE]; // Where to send to
    uint16_t DestinationPort;                    // UDP port to send to
    char SourceIp[DTNMOS_MAX_ADDRESS_SIZE];      // Where to send from
    uint64_t AtNs;                               // As in DtNmosReceiverActivation
} DtNmosSenderActivation;

// The program's function that applies a controller's activation of a receiver or sender.
// It returns DTNMOS_OK when the activation is applied, or fails with
// DtNmos_SetLastError(), and the node passes that message to the controller.
//
// The node calls it for an immediate activation on the thread that handles the request,
// and for a scheduled one in DtNmosNode_Poll(), ActivationLeadMs before it is due. The
// callback may block for as long as applying takes; the node's lock is not held.
// Meanwhile, another request for the same sender or receiver is answered with 423
// (locked), and the callback is never called twice at once for one sender or receiver.
// The new parameters become active at AtNs, or when the callback returns if that is
// later. A failed scheduled activation is logged.
typedef DtNmosResult (*DtNmosReceiverActivateFunc)(
    void* User, const DtNmosId* Receiver, const DtNmosReceiverActivation* Activation);

typedef DtNmosResult (*DtNmosSenderActivateFunc)(
    void* User, const DtNmosId* Sender, const DtNmosSenderActivation* Activation);

// Every function of a node except _Alloc, _Open, _Free and _Freep needs an open node, and
// returns DTNMOS_E_STATE (or 0 when it returns no result) for one that is not open.

// Returns whether the library was built with the HTTP server (DTNMOS_WITH_SERVER), so
// that DtNmosNode_Serve() works.
DTNMOS_API bool DtNmos_HasServer(void);

// Adds a device to Node. The next poll registers it.
//
// Returns DTNMOS_OK, or DTNMOS_E_INVALID_ARGUMENT when the node already has the ID.
DTNMOS_API DtNmosResult DtNmosNode_AddDevice(DtNmosNode* Node,
                                             const DtNmosDeviceConfig* Device);

// Adds a receiver to a device of Node. The node calls Activate, with User, when a
// controller activates it. The next poll registers it.
//
// Returns DTNMOS_OK, or DTNMOS_E_INVALID_ARGUMENT when the node already has the ID, does
// not have the device, or the config has no InterfaceIp.
DTNMOS_API DtNmosResult DtNmosNode_AddReceiver(DtNmosNode* Node,
                                               const DtNmosReceiverConfig* Receiver,
                                               DtNmosReceiverActivateFunc Activate,
                                               void* User);

// Adds a sender to a device of Node. The node calls Activate, with User, when a
// controller activates it. The next poll registers it.
//
// Returns DTNMOS_OK, or DTNMOS_E_INVALID_ARGUMENT when the node already has the ID, does
// not have the device, the flow is neither video nor audio, or the config has no
// SourceIp.
DTNMOS_API DtNmosResult DtNmosNode_AddSender(DtNmosNode* Node,
                                             const DtNmosSenderConfig* Sender,
                                             DtNmosSenderActivateFunc Activate,
                                             void* User);

// Creates a node, not yet open. Returns NULL when there is not enough memory.
DTNMOS_API DtNmosNode* DtNmosNode_Alloc(void);

// Returns the port at which the node's APIs are reached: ApiPort, or the port
// DtNmosNode_Serve() picked.
DTNMOS_API uint16_t DtNmosNode_ApiPort(const DtNmosNode* Node);

// Writes the base URL of the node's APIs, e.g. "http://192.168.1.5:8080", with its null,
// into Buffer of *Size bytes, and sets *Size to its length.
//
// Returns DTNMOS_OK, or DTNMOS_E_BUFFER_TOO_SMALL; *Size is then the size needed.
DTNMOS_API DtNmosResult DtNmosNode_ApiUrl(const DtNmosNode* Node, char* Buffer,
                                          size_t* Size);

// Closes Node: stops serving, unregisters everything from the registry, and forgets all
// devices, senders and receivers. The node may be opened again.
DTNMOS_API DtNmosResult DtNmosNode_Close(DtNmosNode* Node);

// Closes Node if it is open, and frees it. NULL does nothing.
DTNMOS_API void DtNmosNode_Free(DtNmosNode* Node);

// Frees *Node, as DtNmosNode_Free() does, and sets *Node to NULL. NULL does nothing.
DTNMOS_API void DtNmosNode_Freep(DtNmosNode** Node);

// Answers a request to the Node API or the Connection API, for a program that runs an
// HTTP server of its own instead of DtNmosNode_Serve(). Request->Url is the path and
// query. Fills Response, which is empty, with the answer, also when that is an error
// status.
DTNMOS_API DtNmosResult DtNmosNode_Handle(DtNmosNode* Node,
                                          const DtNmosHttpRequest* Request,
                                          DtNmosHttpResponse* Response);

// Writes the node's ID, from its config, into *Id. A program can make the IDs of its
// devices, senders and receivers from it with DtNmosId_FromName().
//
// Returns DTNMOS_OK, or DTNMOS_E_INVALID_ARGUMENT when Id is NULL.
DTNMOS_API DtNmosResult DtNmosNode_Id(const DtNmosNode* Node, DtNmosId* Id);

// Returns whether the registry has the node and all its devices, senders and receivers.
DTNMOS_API bool DtNmosNode_IsRegistered(const DtNmosNode* Node);

// Opens Node with Config. Nothing is registered until the node is polled. A closed node
// may be opened again with another config.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_INVALID_ARGUMENT  Config has no ID or no Http, or neither a
//                              RegistrationUrl nor a Search
//   DTNMOS_E_STATE             the node is already open
DTNMOS_API DtNmosResult DtNmosNode_Open(DtNmosNode* Node, const DtNmosNodeConfig* Config);

// Does the node's periodic work: applies scheduled activations that are due, registers
// what is not registered yet, unregisters what was removed, and sends a heartbeat when
// one is due. It also finds a registry through the search when the node has none, and
// registers everything again when the registry has lost the node. DtNmosNode_Serve()
// calls it on its own thread; a program without it calls it itself.
//
// Sets *NextMs, when NextMs is not NULL, to how many milliseconds until the node wants
// to be polled again. Having no registry is not a failure.
//
// Returns DTNMOS_OK, or the error of the first request that failed; the next poll tries
// again.
DTNMOS_API DtNmosResult DtNmosNode_Poll(DtNmosNode* Node, uint32_t* NextMs);

// Removes a device, sender or receiver from Node; removing a device also removes its
// senders and receivers. The next poll unregisters them.
//
// Returns DTNMOS_OK, or DTNMOS_E_NOT_FOUND when the node has no such ID.
DTNMOS_API DtNmosResult DtNmosNode_Remove(DtNmosNode* Node, const DtNmosId* Id);

// Serves the Node API and the Connection API at ApiHost and ApiPort, and polls the node
// on a thread of its own, until DtNmosNode_Close().
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_STATE  the library was built without the server (see DtNmos_HasServer())
//   DTNMOS_E_HTTP   the server cannot listen at the address and port
DTNMOS_API DtNmosResult DtNmosNode_Serve(DtNmosNode* Node);

// Replaces the flow of sender Id, e.g. after its format changed. Its SDP changes with it,
// and the next poll registers the new version.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_NOT_FOUND         the node has no sender Id
//   DTNMOS_E_INVALID_ARGUMENT  Id or Flow is NULL, Flow->Size is too small, or Flow is
//                              of another media than the sender's
//   DTNMOS_E_NO_MEMORY         not enough memory
DTNMOS_API DtNmosResult DtNmosNode_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                                const DtNmosFlow* Flow);

#ifdef __cplusplus
}
#endif
