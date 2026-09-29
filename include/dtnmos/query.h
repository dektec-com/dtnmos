// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# query.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/dtnmos.h"
#include "dtnmos/http.h"
#include "dtnmos/sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

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

#ifdef __cplusplus
}
#endif
