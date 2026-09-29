// SPDX-License-Identifier: BSD-3-Clause
//
// dtnmos: an NMOS node of senders and receivers (AMWA IS-04 v1.3 and IS-05 v1.1). The
// node registers itself, a device per card, and its senders and receivers with the
// Registration API of a registry, keeps them registered with heartbeats, and answers the
// Node API and the Connection API, through which a controller connects its receivers and
// moves its senders.
//
// The core starts no threads: dtnmos_node_poll() registers and sends heartbeats when the
// caller calls it, and dtnmos_node_handle() answers a request the caller's own HTTP
// server received. dtnmos_node_serve() does both on threads of its own, with a server on
// civetweb, when the library is built with DTNMOS_WITH_SERVER.

#pragma once

#include "dtnmos/dtnmos.h"
#include "dtnmos/http.h"
#include "dtnmos/sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct dtnmos_node dtnmos_node;

typedef struct dtnmos_node_config
{
    size_t size;  // sizeof(dtnmos_node_config)
    dtnmos_id id; // stable, e.g. from dtnmos_id_from_name()
    const char* label;
    const char* description;
    const char* hostname;
    // The address the Node and the Connection API are reached at; null finds the address
    // of this host on the way to the registry.
    const char* api_host;
    uint16_t api_port; // 0 lets dtnmos_node_serve() take any free port
    const char*
        registration_url;    // base URL of the registry, e.g. "http://registry.local"
    const char* api_version; // of IS-04; "v1.3" when null, the only one accepted yet
    dtnmos_http_fn http;     // for the requests to the registry
    void* http_user;
    uint32_t timeout_ms;   // of each request to the registry; 5000 when 0
    uint32_t heartbeat_ms; // 5000 when 0
    dtnmos_log_fn log;     // optional
    void* log_user;
} dtnmos_node_config;

typedef struct dtnmos_device_config
{
    size_t size;
    dtnmos_id id;
    const char* label; // e.g. "DTA-2110 2110000076 port 1"
    const char* description;
} dtnmos_device_config;

typedef struct dtnmos_sender_config
{
    size_t size;
    dtnmos_id id;
    dtnmos_id device_id;
    const char* label;
    const char* description;
    const dtnmos_flow* flow; // what it sends, video or audio, written as its SDP; copied
    const char* source_ip;   // the address it sends from, in its SDP
} dtnmos_sender_config;

typedef struct dtnmos_receiver_config
{
    size_t size;
    dtnmos_id id;
    dtnmos_id device_id;
    const char* label;
    const char* description;
    dtnmos_media media; // video or audio
} dtnmos_receiver_config;

// What a controller activates on a receiver (IS-05): whether it receives, and the flow it
// receives: the flow of the media of the receiver in the transport file, with the
// multicast_ip, source_ip and destination_port of the transport parameters over it.
// has_flow is 0 when the staged parameters hold no transport file, as the parameters
// alone do not describe a flow. The node owns what it passes to a callback, which copies
// what it keeps with dtnmos_flow_copy().
typedef struct dtnmos_receiver_activation
{
    int master_enable;
    int has_flow;
    dtnmos_flow flow;
    dtnmos_id sender_id; // empty when not given
} dtnmos_receiver_activation;

// What a controller activates on a sender: whether it sends, and where to, "auto"
// resolved to where it sends now; source_ip is empty for "auto".
typedef struct dtnmos_sender_activation
{
    int master_enable;
    dtnmos_string destination_ip;
    uint16_t destination_port;
    dtnmos_string source_ip;
} dtnmos_sender_activation;

// Called when a controller activates a receiver or a sender. The callback applies it and
// returns DTNMOS_OK, or a failure that the node answers the controller with. It is called
// on the thread that handles the request, without the lock of the node, and may block for
// as long as applying takes.
typedef dtnmos_result (*dtnmos_receiver_activate_fn)(
    void* user, const dtnmos_id* receiver, const dtnmos_receiver_activation* activation,
    dtnmos_error* error);
typedef dtnmos_result (*dtnmos_sender_activate_fn)(
    void* user, const dtnmos_id* sender, const dtnmos_sender_activation* activation,
    dtnmos_error* error);

// Creates a node of config, whose strings it copies. Fails with DTNMOS_E_INVALID_ARGUMENT
// without an ID, a registration URL or an HTTP function.
DTNMOS_API dtnmos_result dtnmos_node_create(const dtnmos_node_config* config,
                                            dtnmos_node** node, dtnmos_error* error);

// Stops serving, deletes what the node registered from the registry, and frees it.
DTNMOS_API void dtnmos_node_destroy(dtnmos_node* node);

// Adds a device, sender or receiver; the next poll registers it. Fails with
// DTNMOS_E_INVALID_ARGUMENT for an ID the node has, an unknown device, or a sender of
// neither video nor audio.
DTNMOS_API dtnmos_result dtnmos_node_add_device(dtnmos_node* node,
                                                const dtnmos_device_config* device,
                                                dtnmos_error* error);
DTNMOS_API dtnmos_result dtnmos_node_add_sender(dtnmos_node* node,
                                                const dtnmos_sender_config* sender,
                                                dtnmos_sender_activate_fn activate,
                                                void* user, dtnmos_error* error);
DTNMOS_API dtnmos_result dtnmos_node_add_receiver(dtnmos_node* node,
                                                  const dtnmos_receiver_config* receiver,
                                                  dtnmos_receiver_activate_fn activate,
                                                  void* user, dtnmos_error* error);

// Removes a device, sender or receiver, and the senders and receivers of a device; the
// next poll deletes them from the registry. Fails with DTNMOS_E_NOT_FOUND for an unknown
// ID.
DTNMOS_API dtnmos_result dtnmos_node_remove(dtnmos_node* node, const dtnmos_id* id,
                                            dtnmos_error* error);

// Changes the flow a sender sends, e.g. after its format changed, and its SDP with it;
// the next poll registers the new version.
DTNMOS_API dtnmos_result dtnmos_node_update_sender(dtnmos_node* node, const dtnmos_id* id,
                                                   const dtnmos_flow* flow,
                                                   dtnmos_error* error);

// Registers what is not registered yet, deletes what was removed, and sends a heartbeat
// when one is due, registering everything again when the registry has lost the node. Sets
// next_ms, when it is not null, to when it wants to be called again. Fails with the first
// request that failed; the next poll tries again.
DTNMOS_API dtnmos_result dtnmos_node_poll(dtnmos_node* node, uint32_t* next_ms,
                                          dtnmos_error* error);

// Whether the registry holds the node and everything it has.
DTNMOS_API int dtnmos_node_registered(const dtnmos_node* node);

// Answers a request to the Node API or the Connection API, for a caller with an HTTP
// server of its own; request->url is the path and query of the request. Fills response,
// which is empty, with the answer, an error status included.
DTNMOS_API dtnmos_result dtnmos_node_handle(dtnmos_node* node,
                                            const dtnmos_http_request* request,
                                            dtnmos_http_response* response,
                                            dtnmos_error* error);

// Serves the Node API and the Connection API on api_host and api_port on a civetweb
// server, and polls the node on a thread of its own, until dtnmos_node_destroy(). Fails
// with DTNMOS_E_STATE when the library is built without DTNMOS_WITH_SERVER, and with
// DTNMOS_E_HTTP when the server cannot listen.
DTNMOS_API dtnmos_result dtnmos_node_serve(dtnmos_node* node, dtnmos_error* error);

// Whether the library was built with the server, DTNMOS_WITH_SERVER.
DTNMOS_API int dtnmos_has_server(void);

// Returns the port the node is reached at: api_port, or the one dtnmos_node_serve() took.
DTNMOS_API uint16_t dtnmos_node_api_port(const dtnmos_node* node);

// Writes the base URL of the APIs of the node into url, e.g. "http://192.168.1.5:8080".
DTNMOS_API dtnmos_result dtnmos_node_api_url(const dtnmos_node* node, dtnmos_string* url);

#ifdef __cplusplus
}
#endif
