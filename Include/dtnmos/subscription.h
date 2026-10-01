// #*#*#*#*#*#*#*#*#*#*#*#*#*# subscription.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A subscription to the resources of a registry (IS-04 v1.3)
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "dtnmos/dtnmos.h"
#include "dtnmos/query.h"
#include "dtnmos/websocket.h"

#ifdef __cplusplus
extern "C"
{
#endif

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

#ifdef __cplusplus
}
#endif
