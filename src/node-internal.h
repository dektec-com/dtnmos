// SPDX-License-Identifier: BSD-3-Clause
//
// The node inside dtnmos: its devices, senders and receivers, what they have registered,
// and the functions that node.c, connection.c and server.c share. Not exported.

#pragma once

#include <stdint.h>

#include "dtnmos/node.h"
#include "internal.h"
#include "platform.h"

typedef struct node_device
{
    dtnmos_id id;
    char* label;
    char* description;
    char version[32];
    int registered;     // the registry holds this version
    int was_registered; // the registry held a version, which a removal deletes
} node_device;

typedef struct node_sender
{
    dtnmos_id id;
    dtnmos_id device_id;
    dtnmos_id source_id; // derived from id: the source and flow the sender brings along
    dtnmos_id flow_id;
    dtnmos_id receiver_id; // the receiver a controller connected it to, or empty
    char* label;
    char* description;
    char* source_ip;
    dtnmos_flow flow;
    dtnmos_sender_activate_fn activate;
    void* user;
    char version[32];
    int registered;     // with its source and flow
    int was_registered; // the registry held a version, which a removal deletes
    int master_enable;
    uint64_t session_id; // of its SDP
    uint64_t session_version;
    void* connection; // the staged parameters of IS-05, which connection.c owns
} node_sender;

typedef struct node_receiver
{
    dtnmos_id id;
    dtnmos_id device_id;
    dtnmos_id sender_id; // the sender a controller connected it to, or empty
    char* label;
    char* description;
    dtnmos_media media;
    dtnmos_receiver_activate_fn activate;
    void* user;
    char version[32];
    int registered;
    int was_registered;
    int master_enable;
    void* connection; // the staged and active parameters of IS-05, of connection.c
} node_receiver;

typedef struct node_removal
{
    char type[16]; // the collection of the Registration API, e.g. "senders"
    dtnmos_id id;
} node_removal;

struct dtnmos_node
{
    dtnmos_mutex* mutex;
    dtnmos_id id;
    char* label;
    char* description;
    char* hostname;
    char* api_host;
    uint16_t api_port;
    char* registration; // base URL of the Registration API, ending in /
    dtnmos_http_fn http;
    void* http_user;
    uint32_t timeout_ms;
    uint32_t heartbeat_ms;
    dtnmos_log_fn log;
    void* log_user;
    char version[32];
    uint64_t last_version;
    int node_registered;
    int closing; // the node deletes what it registered and registers nothing more
    uint64_t next_heartbeat_ms;
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
    void* server; // of server.c, when the node serves itself
};

void dtnmos_node_lock(dtnmos_node* node);
void dtnmos_node_unlock(dtnmos_node* node);
void dtnmos_node_free(dtnmos_node* node);

node_device* dtnmos_node_find_device(dtnmos_node* node, const dtnmos_id* id);
node_sender* dtnmos_node_find_sender(dtnmos_node* node, const dtnmos_id* id);
node_receiver* dtnmos_node_find_receiver(dtnmos_node* node, const dtnmos_id* id);

// Whether address is a multicast address of IPv4 or IPv6.
int dtnmos_is_multicast(const char* address);

// Write the JSON of the resources of IS-04 v1.3; the caller holds the lock.
void dtnmos_node_write_base_url(const dtnmos_node* node, dtnmos_buffer* b);
void dtnmos_node_write_self(const dtnmos_node* node, dtnmos_buffer* b);
void dtnmos_node_write_device(const dtnmos_node* node, const node_device* device,
                              dtnmos_buffer* b);
void dtnmos_node_write_source(const node_sender* sender, dtnmos_buffer* b);
void dtnmos_node_write_flow(const node_sender* sender, dtnmos_buffer* b);
void dtnmos_node_write_sender(const dtnmos_node* node, const node_sender* sender,
                              dtnmos_buffer* b);
void dtnmos_node_write_receiver(const node_receiver* receiver, dtnmos_buffer* b);

// Writes the SDP of sender, its transport file; the caller holds the lock.
dtnmos_result dtnmos_node_write_transport_file(const node_sender* sender,
                                               dtnmos_string* text, dtnmos_error* error);

void dtnmos_node_answer_error(dtnmos_http_response* response, int status,
                              const char* message);
void dtnmos_node_answer_json(dtnmos_http_response* response, dtnmos_buffer* b);

// The Connection API of connection.c: the state of a sender or receiver, and the answers
// to /x-nmos/connection/v1.1, whose further segments are segments. It takes the lock
// itself.
dtnmos_result dtnmos_connection_init_sender(node_sender* sender);
void dtnmos_connection_clear_sender(node_sender* sender);
dtnmos_result dtnmos_connection_init_receiver(node_receiver* receiver);
void dtnmos_connection_clear_receiver(node_receiver* receiver);
dtnmos_result dtnmos_connection_handle(dtnmos_node* node,
                                       const dtnmos_http_request* request,
                                       char** segments, size_t count,
                                       dtnmos_http_response* response,
                                       dtnmos_error* error);

// Stops the server of server.c and its polling, when the node serves itself.
void dtnmos_server_stop(dtnmos_node* node);
