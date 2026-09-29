// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# discovery.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/dtnmos.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum dtnmos_service
{
    DTNMOS_SERVICE_QUERY,        // _nmos-query._tcp, the Query API
    DTNMOS_SERVICE_REGISTRATION, // _nmos-register._tcp, the Registration API
} dtnmos_service;

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
} dtnmos_registry_info;

DTNMOS_API void dtnmos_registry_info_clear(dtnmos_registry_info* registry);
DTNMOS_API dtnmos_result dtnmos_registry_info_copy(dtnmos_registry_info* target,
                                                   const dtnmos_registry_info* source);

// The registries a search found, which the list owns: the usable ones first, each in the
// order of priority, those without a priority last, then by instance name. IS-04 has a
// client take one at random among those of the same priority, which is for the caller.
typedef struct dtnmos_registry_list dtnmos_registry_list;

DTNMOS_API size_t dtnmos_registry_list_count(const dtnmos_registry_list* list);
DTNMOS_API const dtnmos_registry_info*
dtnmos_registry_list_at(const dtnmos_registry_list* list, size_t index);
DTNMOS_API void dtnmos_registry_list_free(dtnmos_registry_list* list);

// Searches for the registries of config->service: sends the query three times within the
// timeout, collects the answers until it ends, and asks once more, within half the
// timeout again, for the records the answers left out. Finding none is no failure: the
// list is then empty. Fails with DTNMOS_E_INVALID_ARGUMENT for a malformed address, and
// with DTNMOS_E_NETWORK when the socket cannot be opened or the query cannot be sent.
DTNMOS_API dtnmos_result dtnmos_discover(const dtnmos_discovery_config* config,
                                         dtnmos_registry_list** list,
                                         dtnmos_error* error);

#ifdef __cplusplus
}
#endif
