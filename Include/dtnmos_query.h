// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_query.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A registry asked, watched and controlled: IS-04 v1.3 Query, IS-05 and DNS-SD
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "dtnmos.h"
#include "dtnmos_http.h"
#include "dtnmos_sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= The Query API (IS-04 v1.3) +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct dtnmos_query dtnmos_query;

typedef struct dtnmos_query_config
{
    size_t size;              // sizeof(dtnmos_query_config)
    const char* registry_url; // base URL of the registry, e.g. "http://registry.local"
    const char*
        api_version;     // of the Query API; "v1.3" when null, the only one accepted yet
    dtnmos_http_fn http; // e.g. dtnmos_curl_http
    void* http_user;
    uint32_t timeout_ms; // of each request; 5000 when 0
    dtnmos_log_fn log;   // optional
    void* log_user;
} dtnmos_query_config;

// A sender as the registry lists it. Strings the registry leaves out are empty.
typedef struct dtnmos_sender_info
{
    dtnmos_id id;
    dtnmos_id flow_id; // empty when the sender has no flow
    dtnmos_id device_id;
    dtnmos_string label;
    dtnmos_string description;
    dtnmos_media media;          // from its flow; DTNMOS_MEDIA_OTHER without one
    dtnmos_string transport;     // e.g. "urn:x-nmos:transport:rtp.mcast"
    dtnmos_string manifest_href; // the URL of its SDP; empty when it has none
} dtnmos_sender_info;

DTNMOS_API void dtnmos_sender_info_clear(dtnmos_sender_info* sender);
DTNMOS_API dtnmos_result dtnmos_sender_info_copy(dtnmos_sender_info* target,
                                                 const dtnmos_sender_info* source);

// The senders of a registry, which the list owns.
typedef struct dtnmos_sender_list dtnmos_sender_list;

DTNMOS_API size_t dtnmos_sender_list_count(const dtnmos_sender_list* list);
DTNMOS_API const dtnmos_sender_info* dtnmos_sender_list_at(const dtnmos_sender_list* list,
                                                           size_t index);
DTNMOS_API void dtnmos_sender_list_free(dtnmos_sender_list* list);

// Creates a query of the registry of config, whose strings it copies. Fails with
// DTNMOS_E_INVALID_ARGUMENT without a registry URL or HTTP function, or with another
// API version than v1.3.
DTNMOS_API dtnmos_result dtnmos_query_create(const dtnmos_query_config* config,
                                             dtnmos_query** query, dtnmos_error* error);
DTNMOS_API void dtnmos_query_destroy(dtnmos_query* query);

// Lists the senders of the registry, following its paging, each with the media of its
// flow.
DTNMOS_API dtnmos_result dtnmos_query_senders(dtnmos_query* query,
                                              dtnmos_sender_list** list,
                                              dtnmos_error* error);

// Finds the sender whose ID is id_or_label when it is a UUID, or else whose label it
// is, into sender, which is cleared first and which the caller clears. Fails with
// DTNMOS_E_NOT_FOUND, or DTNMOS_E_AMBIGUOUS when senders share the label, the message
// listing their IDs.
DTNMOS_API dtnmos_result dtnmos_query_find_sender(dtnmos_query* query,
                                                  const char* id_or_label,
                                                  dtnmos_sender_info* sender,
                                                  dtnmos_error* error);

// Fetches the SDP of sender from its manifest_href into text, which is cleared first.
// Fails with DTNMOS_E_NOT_FOUND when the sender has no manifest.
DTNMOS_API dtnmos_result dtnmos_query_sender_manifest(dtnmos_query* query,
                                                      const dtnmos_sender_info* sender,
                                                      dtnmos_string* text,
                                                      dtnmos_error* error);

// Fetches the SDP of sender and parses it as dtnmos_sdp_parse() does.
DTNMOS_API dtnmos_result dtnmos_query_sender_sdp(dtnmos_query* query,
                                                 const dtnmos_sender_info* sender,
                                                 dtnmos_sdp** sdp, dtnmos_error* error);

// A receiver as the registry lists it. Strings the registry leaves out are empty.
typedef struct dtnmos_receiver_info
{
    dtnmos_id id;
    dtnmos_id device_id;
    dtnmos_string label;
    dtnmos_string description;
    dtnmos_media media;      // from its format; DTNMOS_MEDIA_OTHER for another one
    dtnmos_string transport; // e.g. "urn:x-nmos:transport:rtp"
    dtnmos_id sender_id;     // the sender it is subscribed to; empty when none
    int active;              // whether that subscription is active
} dtnmos_receiver_info;

DTNMOS_API void dtnmos_receiver_info_clear(dtnmos_receiver_info* receiver);
DTNMOS_API dtnmos_result dtnmos_receiver_info_copy(dtnmos_receiver_info* target,
                                                   const dtnmos_receiver_info* source);

// The receivers of a registry, which the list owns.
typedef struct dtnmos_receiver_list dtnmos_receiver_list;

DTNMOS_API size_t dtnmos_receiver_list_count(const dtnmos_receiver_list* list);
DTNMOS_API const dtnmos_receiver_info*
dtnmos_receiver_list_at(const dtnmos_receiver_list* list, size_t index);
DTNMOS_API void dtnmos_receiver_list_free(dtnmos_receiver_list* list);

// Lists the receivers of the registry, following its paging.
DTNMOS_API dtnmos_result dtnmos_query_receivers(dtnmos_query* query,
                                                dtnmos_receiver_list** list,
                                                dtnmos_error* error);

// Finds the receiver whose ID is id_or_label when it is a UUID, or else whose label it
// is, as dtnmos_query_find_sender() finds a sender, with the same errors.
DTNMOS_API dtnmos_result dtnmos_query_find_receiver(dtnmos_query* query,
                                                    const char* id_or_label,
                                                    dtnmos_receiver_info* receiver,
                                                    dtnmos_error* error);

// +=+=+=+=+=+=+=+=+= A controller that connects receivers (IS-05 v1.1) +=+=+=+=+=+=+=+=+=

// What dtnmos_connect() connected: the receiver and the sender as the registry lists
// them, and the SDP of the sender that the receiver was given. Set to zero, it is empty;
// dtnmos_connection_clear() frees what it holds.
typedef struct dtnmos_connection
{
    dtnmos_receiver_info receiver;
    dtnmos_sender_info sender;
    dtnmos_string sdp;
} dtnmos_connection;

DTNMOS_API void dtnmos_connection_clear(dtnmos_connection* connection);

// Connects the receiver of the registry of query to a sender, each given by its ID or
// its label, as a controller of IS-05 does: it finds the Connection API of the receiver
// through the control urn:x-nmos:control:sr-ctrl/v1.1 of its device, and activates at
// once its staged parameters with the sender, master_enable true and the SDP of the
// sender as its transport file. The requests to the node go through the HTTP function
// of the query, with its timeout. connection, when not null, is cleared first and
// receives what was connected.
//
// Fails as dtnmos_query_find_receiver() and dtnmos_query_find_sender() do; with
// DTNMOS_E_INVALID_ARGUMENT when the flow of the sender is of another kind than the
// format of the receiver, video, audio or data; with DTNMOS_E_NOT_FOUND when the
// device has no such control or the sender no SDP; and with DTNMOS_E_HTTP when the node
// answers with another status than 200, the message holding the error it gave.
DTNMOS_API dtnmos_result dtnmos_connect(dtnmos_query* query, const char* receiver,
                                        const char* sender, dtnmos_connection* connection,
                                        dtnmos_error* error);

// Disconnects the receiver of the registry of query, given by its ID or label: activates
// at once its staged parameters with master_enable false and no sender. disconnected,
// when not null, is cleared first and receives the receiver as the registry lists it.
// Fails as dtnmos_connect() does.
DTNMOS_API dtnmos_result dtnmos_disconnect(dtnmos_query* query, const char* receiver,
                                           dtnmos_receiver_info* disconnected,
                                           dtnmos_error* error);

// Moves a sender of the registry of query, given by its ID or label, to destination_ip
// and destination_port, as a controller of IS-05 does: through the Connection API of the
// sender, found as dtnmos_connect() finds that of a receiver, it activates at once the
// staged parameters of its leg with the new destination. moved, when not null, is
// cleared first and receives the sender as the registry lists it. Fails as
// dtnmos_connect() does, and with DTNMOS_E_INVALID_ARGUMENT for a sender of another
// transport than RTP.
DTNMOS_API dtnmos_result dtnmos_move_sender(dtnmos_query* query, const char* sender,
                                            const char* destination_ip,
                                            uint16_t destination_port,
                                            dtnmos_sender_info* moved,
                                            dtnmos_error* error);

// +=+=+=+=+=+=+=+=+=+= A subscription to the resources of a registry +=+=+=+=+=+=+=+=+=+=

// What happened to a resource.
typedef enum dtnmos_change_kind
{
    DTNMOS_CHANGE_PRESENT = 0, // it is there: the first message of a subscription
    DTNMOS_CHANGE_ADDED = 1,
    DTNMOS_CHANGE_MODIFIED = 2,
    DTNMOS_CHANGE_REMOVED = 3
} dtnmos_change_kind;

// Returns the name of a kind, e.g. "added"; a static string.
DTNMOS_API const char* dtnmos_change_kind_name(dtnmos_change_kind kind);

// A change of one resource, valid during the call it is passed to: its ID, and its JSON
// before and after, each null when the resource was not there.
typedef struct dtnmos_change
{
    dtnmos_change_kind kind;
    const char* id;
    const char* pre;
    size_t pre_length;
    const char* post;
    size_t post_length;
} dtnmos_change;

typedef void (*dtnmos_change_fn)(void* user, const dtnmos_change* change);

typedef struct dtnmos_subscription_config
{
    size_t size;                 // sizeof(dtnmos_subscription_config)
    const char* resource_path;   // e.g. "/senders"
    uint32_t max_update_rate_ms; // how often the registry sends at most; 100 when 0
    // The WebSocket; null takes dtnmos_curl_websocket().
    const dtnmos_websocket_transport* websocket;
    dtnmos_change_fn on_change; // called by dtnmos_subscription_poll()
    void* on_change_user;
} dtnmos_subscription_config;

typedef struct dtnmos_subscription dtnmos_subscription;

// Asks the registry of query for a subscription to the resources at
// config->resource_path, through the HTTP function of the query, and connects to its
// WebSocket, with the timeout of the query. The query must outlive the subscription.
// Fails as the requests of the query do, with DTNMOS_E_PARSE when the answer names no
// WebSocket, and as the connect of the WebSocket does.
DTNMOS_API dtnmos_result
dtnmos_subscription_create(dtnmos_query* query, const dtnmos_subscription_config* config,
                           dtnmos_subscription** subscription, dtnmos_error* error);

// Waits at most timeout_ms for a message, and calls on_change for each change in it, on
// the thread of the caller. The first message holds every resource as it is, each a
// DTNMOS_CHANGE_PRESENT. Fails with DTNMOS_E_TIMEOUT when no message came, with
// DTNMOS_E_PARSE for a message it cannot read, after which it can be polled again, and
// with DTNMOS_E_NETWORK when the WebSocket closed or failed, after which a new
// subscription starts again from the first message.
DTNMOS_API dtnmos_result dtnmos_subscription_poll(dtnmos_subscription* subscription,
                                                  uint32_t timeout_ms,
                                                  dtnmos_error* error);

// The URL of the WebSocket of the subscription.
DTNMOS_API const char* dtnmos_subscription_url(const dtnmos_subscription* subscription);

// Closes the WebSocket and frees the subscription; the registry drops a subscription
// that is not persistent when its last WebSocket closes.
DTNMOS_API void dtnmos_subscription_destroy(dtnmos_subscription* subscription);

// Reads the JSON of a sender, as a change gives it, into sender, which is cleared first
// and which the caller clears. Its media stays DTNMOS_MEDIA_OTHER: it is a property of
// its flow. Fails with DTNMOS_E_PARSE for what is no JSON object.
DTNMOS_API dtnmos_result dtnmos_sender_info_parse(const char* json, size_t length,
                                                  dtnmos_sender_info* sender,
                                                  dtnmos_error* error);

// +=+=+=+=+=+=+=+=+=+=+=+=+= Finding registries through DNS-SD +=+=+=+=+=+=+=+=+=+=+=+=+=

typedef enum dtnmos_service
{
    DTNMOS_SERVICE_QUERY,        // _nmos-query._tcp, the Query API
    DTNMOS_SERVICE_REGISTRATION, // _nmos-register._tcp, the Registration API
} dtnmos_service;

// How a registry is found: through multicast DNS on the link, or through the DNS server
// of the network in its domain (unicast DNS-SD). The searches of a config or them
// together.
typedef enum dtnmos_search
{
    DTNMOS_SEARCH_MULTICAST = 1, // multicast DNS, in the domain local
    DTNMOS_SEARCH_UNICAST = 2,   // the DNS server of the host, in its domain
} dtnmos_search;

typedef struct dtnmos_discovery_config
{
    size_t size; // sizeof(dtnmos_discovery_config)
    dtnmos_service service;
    // The IPv4 address of the interface the query leaves through; null takes that of the
    // default route.
    const char* interface_address;
    // Where the query goes, "<IPv4 address>:<port>"; null is "224.0.0.251:5353". A test
    // gives a responder of its own.
    const char* destination;
    uint32_t timeout_ms; // how long answers are collected; 1000 when 0
    dtnmos_log_fn log;   // optional
    void* log_user;
    // The searches made at the same time, DTNMOS_SEARCH_MULTICAST and
    // DTNMOS_SEARCH_UNICAST or-ed; 0 makes both.
    unsigned searches;
    // The DNS server of the unicast search, "<IPv4 address>:<port>"; null takes the first
    // IPv4 DNS server of the host, at port 53.
    const char* dns_server;
    // The domain the unicast search browses, e.g. "example.com"; null takes the one the
    // host searches. Without a server or a domain, only multicast DNS is asked.
    const char* dns_domain;
} dtnmos_discovery_config;

// A Query or Registration API that a registry announces. Strings an announcement leaves
// out are empty.
typedef struct dtnmos_registry_info
{
    dtnmos_service service;
    dtnmos_string instance; // the name of the service instance, e.g. "Registry 1"
    dtnmos_string host;     // the host of its SRV record, e.g. "registry-1.local"
    dtnmos_string address;  // the IPv4 address of host, when the answers gave it
    uint16_t port;
    // The base URL of the API: "<api_proto>://<address>:<port>", with the host instead of
    // the address for https, or when the answers gave no address.
    dtnmos_string url;
    dtnmos_string api_proto;    // "http" or "https"
    dtnmos_string api_versions; // e.g. "v1.2,v1.3"
    int priority; // pri: lower is preferred, 100 and up are for development;
                  // -1 when the announcement has none
    int auth;     // api_auth is true: the API asks for authorization (IS-10)
    // The API offers v1.3 over http or https without authorization, as dtnmos can use it.
    int usable;
    dtnmos_search found_by; // the search that found it
} dtnmos_registry_info;

DTNMOS_API void dtnmos_registry_info_clear(dtnmos_registry_info* registry);
DTNMOS_API dtnmos_result dtnmos_registry_info_copy(dtnmos_registry_info* target,
                                                   const dtnmos_registry_info* source);

// The registries a search found, which the list owns: the usable ones first, each in the
// order of priority, those without a priority last, those of the DNS server before those
// of multicast DNS, then by instance name. IS-04 has a client take one at random among
// those of the same priority, which is for the caller.
typedef struct dtnmos_registry_list dtnmos_registry_list;

DTNMOS_API size_t dtnmos_registry_list_count(const dtnmos_registry_list* list);
DTNMOS_API const dtnmos_registry_info*
dtnmos_registry_list_at(const dtnmos_registry_list* list, size_t index);
DTNMOS_API void dtnmos_registry_list_free(dtnmos_registry_list* list);

// Searches for the registries of config->service, through multicast DNS and the DNS
// server at the same time, from one socket: sends the query three times within the
// timeout, the one to the DNS server until it answers, collects the answers until it
// ends, and asks once more, within half the timeout again, for the records the answers
// left out; the DNS server one question per query. Finding none is no failure: the list
// is then empty. Fails with DTNMOS_E_INVALID_ARGUMENT for a malformed address or domain,
// and with DTNMOS_E_NETWORK when the socket cannot be opened or the query of multicast
// DNS cannot be sent; a DNS server that cannot be reached is only logged.
DTNMOS_API dtnmos_result dtnmos_discover(const dtnmos_discovery_config* config,
                                         dtnmos_registry_list** list,
                                         dtnmos_error* error);

#ifdef __cplusplus
}
#endif
