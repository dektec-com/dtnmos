// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosNode.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The node inside dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdint.h>

#include "NmosFlow.h"
#include "NmosInternal.h"
#include "NmosOs.h"
#include "dtnmos_node.h"

typedef struct NmosNodeDevice
{
    DtNmosId Id;
    char* Label;
    char* Description;
    char Version[32];
    bool Registered;    // the registry holds this version
    bool WasRegistered; // the registry held a version, which a removal deletes
} NmosNodeDevice;

typedef struct NmosNodeSender
{
    DtNmosId Id;
    DtNmosId DeviceId;
    DtNmosId SourceId; // derived from id: the source and flow the sender brings along
    DtNmosId FlowId;
    DtNmosId ReceiverId; // the receiver a controller connected it to, or empty
    char* Label;
    char* Description;
    char* SourceIp; // of its config, which source_ip "auto" stands for
    // The source_ip that is active, which its SDP gives as origin and source-filter.
    char ActiveSourceIp[DTNMOS_MAX_ADDRESS_SIZE];
    DtNmosFlow Flow;
    NmosStore FlowStore; // the strings and arrays of flow
    DtNmosSenderActivateFunc Activate;
    void* User;
    char Version[32];
    bool Registered;    // with its source and flow
    bool WasRegistered; // the registry held a version, which a removal deletes
    bool MasterEnable;
    uint64_t SessionId; // of its SDP
    uint64_t SessionVersion;
    uint64_t LeadNs;  // how long before a scheduled activation its callback is called
    void* Connection; // the staged parameters of IS-05, which NmosConnection.c owns
} NmosNodeSender;

typedef struct NmosNodeReceiver
{
    DtNmosId Id;
    DtNmosId DeviceId;
    DtNmosId SenderId; // the sender a controller connected it to, or empty
    char* Label;
    char* Description;
    DtNmosMedia Media;
    char* InterfaceIp; // of its config, which interface_ip "auto" stands for
    DtNmosReceiverActivateFunc Activate;
    void* User;
    char Version[32];
    bool Registered;
    bool WasRegistered;
    bool MasterEnable;
    uint64_t LeadNs;  // how long before a scheduled activation its callback is called
    void* Connection; // the staged and active parameters of IS-05, of NmosConnection.c
} NmosNodeReceiver;

typedef struct NmosNodeRemoval
{
    char Type[16]; // the collection of the Registration API, e.g. "senders"
    DtNmosId Id;
} NmosNodeRemoval;

// A node is allocated empty and closed; DtNmosNode_Open() fills it in, and
// DtNmosNode_Close() empties it again.
struct DtNmosNode
{
    bool Open;
    NmosMutex* Mutex;
    DtNmosId Id;
    char* Label;
    char* Description;
    char* Hostname;
    char* ApiHost;
    uint16_t ApiPort;
    // The base URL of the Registration API, ending in /; null while the node searches
    // for its registry. The poll thread changes it, under the lock.
    char* Registration;
    DtNmosHttpFunc Http;
    void* HttpUser;
    uint32_t TimeoutMs;
    uint32_t HeartbeatMs;
    DtNmosLogFunc Log;
    void* LogUser;
    char Version[32];
    uint64_t LastVersion;
    bool NodeRegistered;    // the registry holds this version
    bool NodeWasRegistered; // the registry held a version, which closing deletes
    bool Closing; // the node deletes what it registered and registers nothing more
    uint64_t NextHeartbeatMs;
    bool Wake; // an activation was scheduled, which the next poll may have to apply
    DtNmosRegistryFailedFunc RegistryFailed;
    void* RegistryFailedUser;
    uint32_t FailuresBeforeSwitch;
    uint32_t Failures; // polls that failed in a row; the poll thread's own
    // A node opened without a registry takes its registries from the search of the
    // application, which it borrows.
    bool Searches;
    DtNmosRegistrySearch* Search;
    // The base URLs of the registries that failed since the node last started over from
    // the most preferred; the poll thread's own.
    char* Failed[16];
    size_t FailedCount;
    // The node has not registered with its registry yet; a 200 to that first registration
    // says the registry holds an old node of its ID. The poll thread's own.
    bool FirstRegistration;
    NmosNodeDevice* Devices;
    size_t DeviceCount;
    size_t DeviceCapacity;
    NmosNodeSender* Senders;
    size_t SenderCount;
    size_t SenderCapacity;
    NmosNodeReceiver* Receivers;
    size_t ReceiverCount;
    size_t ReceiverCapacity;
    NmosNodeRemoval* Removals;
    size_t RemovalCount;
    size_t RemovalCapacity;
    void* Server; // of NmosServer.c, when the node serves itself
};

void NmosNode_Lock(DtNmosNode* Node);
void NmosNode_Unlock(DtNmosNode* Node);
// Frees what node holds and leaves it empty and closed.
void NmosNode_Release(DtNmosNode* Node);
// Gives the node a new version, which the next poll registers, after what it lists of
// its senders and receivers changed; the caller holds the lock.
void NmosNode_Touch(DtNmosNode* Node);
// Fails with DTNMOS_E_STATE, naming function, when node is not open.
DtNmosResult NmosNode_CheckOpen(const DtNmosNode* Node, const char* Function);

NmosNodeDevice* NmosNode_FindDevice(DtNmosNode* Node, const DtNmosId* Id);
NmosNodeSender* NmosNode_FindSender(DtNmosNode* Node, const DtNmosId* Id);
NmosNodeReceiver* NmosNode_FindReceiver(DtNmosNode* Node, const DtNmosId* Id);

// Whether address is an address of IPv4 or IPv6 that fits DTNMOS_MAX_ADDRESS_SIZE.
bool NmosNode_IsAddress(const char* Address);
// Whether address is a multicast address of IPv4 or IPv6.
bool NmosNode_IsMulticast(const char* Address);

// Write the JSON of the resources of IS-04 v1.3; the caller holds the lock.
void NmosNode_WriteBaseUrl(const DtNmosNode* Node, NmosBuffer* b);
void NmosNode_WriteSelf(const DtNmosNode* Node, NmosBuffer* b);
void NmosNode_WriteDevice(const DtNmosNode* Node, const NmosNodeDevice* Device,
                          NmosBuffer* b);
void NmosNode_WriteSource(const NmosNodeSender* Sender, NmosBuffer* b);
void NmosNode_WriteFlow(const NmosNodeSender* Sender, NmosBuffer* b);
void NmosNode_WriteSender(const DtNmosNode* Node, const NmosNodeSender* Sender,
                          NmosBuffer* b);
void NmosNode_WriteReceiver(const NmosNodeReceiver* Receiver, NmosBuffer* b);

// Writes the SDP of sender, its transport file; the caller holds the lock.
DtNmosResult NmosNode_WriteTransportFile(const NmosNodeSender* Sender, NmosBuffer* Text);

void NmosNode_AnswerError(DtNmosHttpResponse* Response, int Status, const char* Message);
void NmosNode_AnswerJson(DtNmosHttpResponse* Response, NmosBuffer* b);

// The Connection API of NmosConnection.c: the state of a sender or receiver, and the
// answers to /x-nmos/connection/v1.1, whose further segments are segments. It takes the
// lock itself.
DtNmosResult NmosConnection_InitSender(NmosNodeSender* Sender);
void NmosConnection_ClearSender(NmosNodeSender* Sender);
DtNmosResult NmosConnection_InitReceiver(NmosNodeReceiver* Receiver);
void NmosConnection_ClearReceiver(NmosNodeReceiver* Receiver);
DtNmosResult NmosConnection_Handle(DtNmosNode* Node, const DtNmosHttpRequest* Request,
                                   char** Segments, size_t Count,
                                   DtNmosHttpResponse* Response);
// Applies the scheduled activations that are due, each through its callback without the
// lock, and lowers *WaitMs to the milliseconds until the next one, when it is sooner.
void NmosConnection_Poll(DtNmosNode* Node, uint32_t* WaitMs);

// Stops the server of NmosServer.c and its polling, when the node serves itself.
void NmosServer_Stop(DtNmosNode* Node);
