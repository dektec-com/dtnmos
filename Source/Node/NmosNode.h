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
    int Registered;    // the registry holds this version
    int WasRegistered; // the registry held a version, which a removal deletes
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
    char* SourceIp;
    DtNmosFlow Flow;
    NmosStore FlowStore; // the strings and arrays of flow
    DtNmosSenderActivateFunc Activate;
    void* User;
    char Version[32];
    int Registered;    // with its source and flow
    int WasRegistered; // the registry held a version, which a removal deletes
    int MasterEnable;
    uint64_t SessionId; // of its SDP
    uint64_t SessionVersion;
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
    DtNmosReceiverActivateFunc Activate;
    void* User;
    char Version[32];
    int Registered;
    int WasRegistered;
    int MasterEnable;
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
    int Open;
    NmosMutex* Mutex;
    DtNmosId Id;
    char* Label;
    char* Description;
    char* Hostname;
    char* ApiHost;
    uint16_t ApiPort;
    char* Registration; // base URL of the Registration API, ending in /
    DtNmosHttpFunc Http;
    void* HttpUser;
    uint32_t TimeoutMs;
    uint32_t HeartbeatMs;
    DtNmosLogFunc Log;
    void* LogUser;
    char Version[32];
    uint64_t LastVersion;
    int NodeRegistered;
    int Closing; // the node deletes what it registered and registers nothing more
    uint64_t NextHeartbeatMs;
    DtNmosRegistryFailedFunc RegistryFailed;
    void* RegistryFailedUser;
    uint32_t FailuresBeforeSwitch;
    uint32_t Failures; // polls that failed in a row; the poll thread's own
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
// Fails with DTNMOS_E_STATE, naming function, when node is not open.
DtNmosResult NmosNode_CheckOpen(const DtNmosNode* Node, const char* Function);

NmosNodeDevice* NmosNode_FindDevice(DtNmosNode* Node, const DtNmosId* Id);
NmosNodeSender* NmosNode_FindSender(DtNmosNode* Node, const DtNmosId* Id);
NmosNodeReceiver* NmosNode_FindReceiver(DtNmosNode* Node, const DtNmosId* Id);

// Whether address is a multicast address of IPv4 or IPv6.
int NmosNode_IsMulticast(const char* Address);

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

// Stops the server of NmosServer.c and its polling, when the node serves itself.
void NmosServer_Stop(DtNmosNode* Node);
