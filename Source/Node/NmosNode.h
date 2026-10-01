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

typedef struct node_device
{
    DtNmosId id;
    char* label;
    char* description;
    char version[32];
    int registered;     // the registry holds this version
    int was_registered; // the registry held a version, which a removal deletes
} node_device;

typedef struct node_sender
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
    dtnmos_store flow_store; // the strings and arrays of flow
    DtNmosSenderActivateFunc activate;
    void* user;
    char version[32];
    int registered;     // with its source and flow
    int was_registered; // the registry held a version, which a removal deletes
    int master_enable;
    uint64_t session_id; // of its SDP
    uint64_t session_version;
    void* connection; // the staged parameters of IS-05, which NmosConnection.c owns
} node_sender;

typedef struct node_receiver
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
} node_receiver;

typedef struct node_removal
{
    char type[16]; // the collection of the Registration API, e.g. "senders"
    DtNmosId id;
} node_removal;

// A node is allocated empty and closed; DtNmosNode_Open() fills it in, and
// DtNmosNode_Close() empties it again.
struct DtNmosNode
{
    int open;
    dtnmos_mutex* mutex;
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
    node_device* devices;
    size_t device_count;
    size_t device_capacity;
    node_sender* senders;
    size_t sender_count;
    size_t sender_capacity;
    node_receiver* receivers;
    size_t receiver_count;
    size_t receiver_capacity;
    node_removal* removals;
    size_t removal_count;
    size_t removal_capacity;
    void* server; // of NmosServer.c, when the node serves itself
};

void dtnmos_node_lock(DtNmosNode* node);
void dtnmos_node_unlock(DtNmosNode* node);
// Frees what node holds and leaves it empty and closed.
void dtnmos_node_release(DtNmosNode* node);
// Fails with DTNMOS_E_STATE, naming function, when node is not open.
DtNmosResult dtnmos_node_check_open(const DtNmosNode* node, const char* function);

node_device* dtnmos_node_find_device(DtNmosNode* node, const DtNmosId* id);
node_sender* dtnmos_node_find_sender(DtNmosNode* node, const DtNmosId* id);
node_receiver* dtnmos_node_find_receiver(DtNmosNode* node, const DtNmosId* id);

// Whether address is a multicast address of IPv4 or IPv6.
int dtnmos_is_multicast(const char* address);

// Write the JSON of the resources of IS-04 v1.3; the caller holds the lock.
void dtnmos_node_write_base_url(const DtNmosNode* node, dtnmos_buffer* b);
void dtnmos_node_write_self(const DtNmosNode* node, dtnmos_buffer* b);
void dtnmos_node_write_device(const DtNmosNode* node, const node_device* device,
                              dtnmos_buffer* b);
void dtnmos_node_write_source(const node_sender* sender, dtnmos_buffer* b);
void dtnmos_node_write_flow(const node_sender* sender, dtnmos_buffer* b);
void dtnmos_node_write_sender(const DtNmosNode* node, const node_sender* sender,
                              dtnmos_buffer* b);
void dtnmos_node_write_receiver(const node_receiver* receiver, dtnmos_buffer* b);

// Writes the SDP of sender, its transport file; the caller holds the lock.
DtNmosResult dtnmos_node_write_transport_file(const node_sender* sender,
                                              dtnmos_buffer* text);

void dtnmos_node_answer_error(DtNmosHttpResponse* response, int status,
                              const char* message);
void dtnmos_node_answer_json(DtNmosHttpResponse* response, dtnmos_buffer* b);

// The Connection API of NmosConnection.c: the state of a sender or receiver, and the
// answers to /x-nmos/connection/v1.1, whose further segments are segments. It takes the
// lock itself.
DtNmosResult dtnmos_connection_init_sender(node_sender* sender);
void dtnmos_connection_clear_sender(node_sender* sender);
DtNmosResult dtnmos_connection_init_receiver(node_receiver* receiver);
void dtnmos_connection_clear_receiver(node_receiver* receiver);
DtNmosResult dtnmos_connection_handle(DtNmosNode* node, const DtNmosHttpRequest* request,
                                      char** segments, size_t count,
                                      DtNmosHttpResponse* response);

// Stops the server of NmosServer.c and its polling, when the node serves itself.
void dtnmos_server_stop(DtNmosNode* node);
