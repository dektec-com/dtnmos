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
    DtNmosId id;
    char* label;
    char* description;
    char version[32];
    int registered;     // the registry holds this version
    int was_registered; // the registry held a version, which a removal deletes
} NmosNodeDevice;

typedef struct NmosNodeSender
{
    DtNmosId id;
    DtNmosId device_id;
    DtNmosId source_id; // derived from id: the source and flow the sender brings along
    DtNmosId flow_id;
    DtNmosId receiver_id; // the receiver a controller connected it to, or empty
    char* label;
    char* description;
    char* source_ip;
    DtNmosFlow flow;
    NmosStore flow_store; // the strings and arrays of flow
    DtNmosSenderActivateFunc activate;
    void* user;
    char version[32];
    int registered;     // with its source and flow
    int was_registered; // the registry held a version, which a removal deletes
    int master_enable;
    uint64_t session_id; // of its SDP
    uint64_t session_version;
    void* connection; // the staged parameters of IS-05, which NmosConnection.c owns
} NmosNodeSender;

typedef struct NmosNodeReceiver
{
    DtNmosId id;
    DtNmosId device_id;
    DtNmosId sender_id; // the sender a controller connected it to, or empty
    char* label;
    char* description;
    DtNmosMedia media;
    DtNmosReceiverActivateFunc activate;
    void* user;
    char version[32];
    int registered;
    int was_registered;
    int master_enable;
    void* connection; // the staged and active parameters of IS-05, of NmosConnection.c
} NmosNodeReceiver;

typedef struct NmosNodeRemoval
{
    char type[16]; // the collection of the Registration API, e.g. "senders"
    DtNmosId id;
} NmosNodeRemoval;

// A node is allocated empty and closed; DtNmosNode_Open() fills it in, and
// DtNmosNode_Close() empties it again.
struct DtNmosNode
{
    int open;
    NmosMutex* mutex;
    DtNmosId id;
    char* label;
    char* description;
    char* hostname;
    char* api_host;
    uint16_t api_port;
    char* registration; // base URL of the Registration API, ending in /
    DtNmosHttpFunc http;
    void* http_user;
    uint32_t timeout_ms;
    uint32_t heartbeat_ms;
    DtNmosLogFunc log;
    void* log_user;
    char version[32];
    uint64_t last_version;
    int node_registered;
    int closing; // the node deletes what it registered and registers nothing more
    uint64_t next_heartbeat_ms;
    DtNmosRegistryFailedFunc registry_failed;
    void* registry_failed_user;
    uint32_t failures_before_switch;
    uint32_t failures; // polls that failed in a row; the poll thread's own
    NmosNodeDevice* devices;
    size_t device_count;
    size_t device_capacity;
    NmosNodeSender* senders;
    size_t sender_count;
    size_t sender_capacity;
    NmosNodeReceiver* receivers;
    size_t receiver_count;
    size_t receiver_capacity;
    NmosNodeRemoval* removals;
    size_t removal_count;
    size_t removal_capacity;
    void* server; // of NmosServer.c, when the node serves itself
};

void NmosNode_Lock(DtNmosNode* node);
void NmosNode_Unlock(DtNmosNode* node);
// Frees what node holds and leaves it empty and closed.
void NmosNode_Release(DtNmosNode* node);
// Fails with DTNMOS_E_STATE, naming function, when node is not open.
DtNmosResult NmosNode_CheckOpen(const DtNmosNode* node, const char* function);

NmosNodeDevice* NmosNode_FindDevice(DtNmosNode* node, const DtNmosId* id);
NmosNodeSender* NmosNode_FindSender(DtNmosNode* node, const DtNmosId* id);
NmosNodeReceiver* NmosNode_FindReceiver(DtNmosNode* node, const DtNmosId* id);

// Whether address is a multicast address of IPv4 or IPv6.
int NmosNode_IsMulticast(const char* address);

// Write the JSON of the resources of IS-04 v1.3; the caller holds the lock.
void NmosNode_WriteBaseUrl(const DtNmosNode* node, NmosBuffer* b);
void NmosNode_WriteSelf(const DtNmosNode* node, NmosBuffer* b);
void NmosNode_WriteDevice(const DtNmosNode* node, const NmosNodeDevice* device,
                          NmosBuffer* b);
void NmosNode_WriteSource(const NmosNodeSender* sender, NmosBuffer* b);
void NmosNode_WriteFlow(const NmosNodeSender* sender, NmosBuffer* b);
void NmosNode_WriteSender(const DtNmosNode* node, const NmosNodeSender* sender,
                          NmosBuffer* b);
void NmosNode_WriteReceiver(const NmosNodeReceiver* receiver, NmosBuffer* b);

// Writes the SDP of sender, its transport file; the caller holds the lock.
DtNmosResult NmosNode_WriteTransportFile(const NmosNodeSender* sender, NmosBuffer* text);

void NmosNode_AnswerError(DtNmosHttpResponse* response, int status, const char* message);
void NmosNode_AnswerJson(DtNmosHttpResponse* response, NmosBuffer* b);

// The Connection API of NmosConnection.c: the state of a sender or receiver, and the
// answers to /x-nmos/connection/v1.1, whose further segments are segments. It takes the
// lock itself.
DtNmosResult NmosConnection_InitSender(NmosNodeSender* sender);
void NmosConnection_ClearSender(NmosNodeSender* sender);
DtNmosResult NmosConnection_InitReceiver(NmosNodeReceiver* receiver);
void NmosConnection_ClearReceiver(NmosNodeReceiver* receiver);
DtNmosResult NmosConnection_Handle(DtNmosNode* node, const DtNmosHttpRequest* request,
                                   char** segments, size_t count,
                                   DtNmosHttpResponse* response);

// Stops the server of NmosServer.c and its polling, when the node serves itself.
void NmosServer_Stop(DtNmosNode* node);
