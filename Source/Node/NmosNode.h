// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosNode.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The node's state, and the functions its sources share
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdint.h>

#include "NmosFlow.h"
#include "NmosInternal.h"
#include "NmosOs.h"
#include "dtnmos_node.h"

// A device of the node.
typedef struct NmosNodeDevice
{
    DtNmosId Id;
    char* Label;
    char* Description;
    char Version[32];   // its IS-04 version stamp, renewed on each change
    bool Registered;    // the registry has this version
    bool WasRegistered; // the registry has some version, which removing it must delete
} NmosNodeDevice;

// A sender of the node. In IS-04 a sender comes with a source and a flow; the node
// registers those too.
typedef struct NmosNodeSender
{
    DtNmosId Id;
    DtNmosId DeviceId;   // the device it belongs to
    DtNmosId SourceId;   // its source, an ID made from the sender's ID
    DtNmosId FlowId;     // its flow, an ID made from the sender's ID
    DtNmosId ReceiverId; // the receiver a controller connected it to, or empty
    char* Label;
    char* Description;
    char* SourceIp; // the address it sends from, which source_ip "auto" means
    // The source_ip now active. Its SDP gives it as origin and in the source filter.
    char ActiveSourceIp[DTNMOS_MAX_ADDRESS_SIZE];
    DtNmosFlow Flow;                   // the stream it sends
    NmosStore FlowStore;               // owns the strings and arrays of Flow
    DtNmosSenderActivateFunc Activate; // the program's callback on an activation
    void* User;                        // the program's argument to Activate
    char Version[32];                  // its IS-04 version stamp, renewed on each change
    bool Registered;    // the registry has this version, with its source and flow
    bool WasRegistered; // the registry has some version, which removing it must delete
    bool MasterEnable;  // a controller has enabled it
    uint64_t SessionId; // the session ID in its SDP
    uint64_t SessionVersion; // the session version in its SDP, raised on each change
    uint64_t LeadNs;  // how long before a scheduled activation its callback is called
    void* Connection; // its IS-05 parameters, staged and active; NmosConnection.c owns it
} NmosNodeSender;

// A receiver of the node.
typedef struct NmosNodeReceiver
{
    DtNmosId Id;
    DtNmosId DeviceId; // the device it belongs to
    DtNmosId SenderId; // the sender a controller connected it to, or empty
    char* Label;
    char* Description;
    DtNmosMedia Media; // the kind of stream it receives
    char* InterfaceIp; // the address it receives on, which interface_ip "auto" means
    DtNmosReceiverActivateFunc Activate; // the program's callback on an activation
    void* User;                          // the program's argument to Activate
    char Version[32];   // its IS-04 version stamp, renewed on each change
    bool Registered;    // the registry has this version
    bool WasRegistered; // the registry has some version, which removing it must delete
    bool MasterEnable;  // a controller has enabled it
    uint64_t LeadNs;    // how long before a scheduled activation its callback is called
    void* Connection; // its IS-05 parameters, staged and active; NmosConnection.c owns it
} NmosNodeReceiver;

// A resource the program removed, which the next poll deletes from the registry.
typedef struct NmosNodeRemoval
{
    char Type[16]; // its collection in the Registration API, e.g. "senders"
    DtNmosId Id;
} NmosNodeRemoval;

// The node. DtNmosNode_Alloc() returns it empty and closed, DtNmosNode_Open() fills it
// in, and DtNmosNode_Close() empties it again.
//
// Mutex guards the node, except the fields marked as the poll thread's own.
struct DtNmosNode
{
    bool Open;
    NmosMutex* Mutex;
    DtNmosId Id;
    char* Label;
    char* Description;
    char* Hostname;
    char* ApiHost;    // the address of the node's own APIs
    uint16_t ApiPort; // the port of the node's own APIs
    // The base URL of the registry's Registration API, ending in /. Null while the node
    // is looking for a registry. The poll thread changes it, holding the lock.
    char* Registration;
    DtNmosHttpFunc Http;    // sends the node's requests to the registry
    void* HttpUser;         // the argument to Http
    uint32_t TimeoutMs;     // how long a request to the registry may take
    uint32_t HeartbeatMs;   // the time between heartbeats
    DtNmosLogFunc Log;      // the program's log callback, or null
    void* LogUser;          // the argument to Log
    char Version[32];       // the node's IS-04 version stamp, renewed on each change
    uint64_t LastVersion;   // the newest version stamp handed out, in nanoseconds
    bool NodeRegistered;    // the registry has this version of the node
    bool NodeWasRegistered; // the registry has some version, which closing must delete
    bool Closing; // the node is closing: it deletes what it registered and adds nothing
    uint64_t NextHeartbeatMs; // when the next heartbeat is due, in NmosOs_MonotonicMs()
    bool Wake; // an activation was scheduled, which the next poll may have to apply
    DtNmosRegistryFailedFunc RegistryFailed; // chooses another registry, or null
    void* RegistryFailedUser;                // the argument to RegistryFailed
    uint32_t FailuresBeforeSwitch; // failed polls in a row before it switches registry
    uint32_t Failures;             // failed polls in a row so far; the poll thread's own
    // The node was opened without a registry, and takes its registries from the
    // application's search. The search is borrowed, not owned.
    bool Searches;
    DtNmosRegistrySearch* Search;
    // The base URLs of the registries that failed since the node last went back to the
    // most preferred one. The poll thread's own.
    char* Failed[16];
    size_t FailedCount;
    // The node has not yet registered with this registry. When the first registration is
    // answered with 200 rather than 201, the registry still has an old node with this ID.
    // The poll thread's own.
    bool FirstRegistration;
    // The node's clock, clk0; a PTP clock's grandmaster in lower case.
    DtNmosClock Clock;
    NmosNodeDevice* Devices;
    size_t DeviceCount;
    size_t DeviceCapacity;
    NmosNodeSender* Senders;
    size_t SenderCount;
    size_t SenderCapacity;
    NmosNodeReceiver* Receivers;
    size_t ReceiverCount;
    size_t ReceiverCapacity;
    NmosNodeRemoval* Removals; // what the next poll deletes from the registry
    size_t RemovalCount;
    size_t RemovalCapacity;
    void* Server; // the node's own HTTP server (NmosServer.c), or null when it has none
};

// Takes the node's lock.
void NmosNode_Lock(DtNmosNode* Node);
// Releases the node's lock.
void NmosNode_Unlock(DtNmosNode* Node);
// Frees everything the node holds, and leaves it empty and closed.
void NmosNode_Release(DtNmosNode* Node);
// Gives the node a new version, so that the next poll registers it again. Called when
// its list of senders and receivers changed. The caller holds the lock.
void NmosNode_Touch(DtNmosNode* Node);
// Checks that the node is open. Fails with DTNMOS_E_STATE when it is not; the message
// names Function.
DtNmosResult NmosNode_CheckOpen(const DtNmosNode* Node, const char* Function);

// Return the device, sender or receiver with ID Id, or null when the node has none.
NmosNodeDevice* NmosNode_FindDevice(DtNmosNode* Node, const DtNmosId* Id);
NmosNodeSender* NmosNode_FindSender(DtNmosNode* Node, const DtNmosId* Id);
NmosNodeReceiver* NmosNode_FindReceiver(DtNmosNode* Node, const DtNmosId* Id);

// Returns whether Address is an IPv4 or IPv6 address that fits DTNMOS_MAX_ADDRESS_SIZE.
bool NmosNode_IsAddress(const char* Address);
// Returns whether Address is an IPv4 or IPv6 multicast address.
bool NmosNode_IsMulticast(const char* Address);

// Append the IS-04 v1.3 JSON of a resource to b: the node's base URL, the node itself,
// a device, a sender's source, flow and sender, and a receiver. The caller holds the
// lock.
void NmosNode_WriteBaseUrl(const DtNmosNode* Node, NmosBuffer* b);
void NmosNode_WriteSelf(const DtNmosNode* Node, NmosBuffer* b);
void NmosNode_WriteDevice(const DtNmosNode* Node, const NmosNodeDevice* Device,
                          NmosBuffer* b);
void NmosNode_WriteSource(const NmosNodeSender* Sender, NmosBuffer* b);
void NmosNode_WriteFlow(const NmosNodeSender* Sender, NmosBuffer* b);
void NmosNode_WriteSender(const DtNmosNode* Node, const NmosNodeSender* Sender,
                          NmosBuffer* b);
void NmosNode_WriteReceiver(const NmosNodeReceiver* Receiver, NmosBuffer* b);

// Writes a sender's transport file: the SDP that describes its stream. The caller holds
// the lock.
DtNmosResult NmosNode_WriteTransportFile(const NmosNodeSender* Sender, NmosBuffer* Text);

// Answers an HTTP request with Status and a JSON error that holds Message.
void NmosNode_AnswerError(DtNmosHttpResponse* Response, int Status, const char* Message);
// Answers an HTTP request with 200 and the JSON in b.
void NmosNode_AnswerJson(DtNmosHttpResponse* Response, NmosBuffer* b);

// The Connection API (IS-05), in NmosConnection.c.
//
// The Init functions give a sender or receiver its IS-05 parameters, and the Clear
// functions free them. NmosConnection_Handle() answers a request under
// /x-nmos/connection/v1.1; Segments are the parts of the path after it. It takes the
// lock itself.
DtNmosResult NmosConnection_InitSender(NmosNodeSender* Sender);
void NmosConnection_ClearSender(NmosNodeSender* Sender);
DtNmosResult NmosConnection_InitReceiver(NmosNodeReceiver* Receiver,
                                         const DtNmosReceiverConfig* Config);
void NmosConnection_ClearReceiver(NmosNodeReceiver* Receiver);
DtNmosResult NmosConnection_Handle(DtNmosNode* Node, const DtNmosHttpRequest* Request,
                                   char** Segments, size_t Count,
                                   DtNmosHttpResponse* Response);
// Carries out the scheduled activations that are due, calling each callback without
// holding the lock. Lowers *WaitMs to the time until the next scheduled one, in
// milliseconds, when that is sooner.
void NmosConnection_Poll(DtNmosNode* Node, uint32_t* WaitMs);

// Stops the node's own HTTP server and the thread that polls the node, when it has them.
void NmosServer_Stop(DtNmosNode* Node);
