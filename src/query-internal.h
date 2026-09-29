// #*#*#*#*#*#*#*#*#*#*#*#*#* query-internal.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - What the controller shares of the query of a registry
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>

#include "dtnmos/query.h"
#include "json.h"

// Returns the base URL of the Query API of query, ending in a slash.
const char* dtnmos_query_base(const dtnmos_query* query);

// Sends a request with the HTTP function and the timeout of query, and logs it; the body
// may be null. Fails only when the HTTP function does, whatever status the answer has.
dtnmos_result dtnmos_query_request(dtnmos_query* query, const char* method,
                                   const char* url, const char* content_type,
                                   const char* body, size_t body_length,
                                   dtnmos_http_response* response, dtnmos_error* error);

// Performs a GET of url and parses its JSON, which the caller frees. Fails unless the
// answer is 200, with DTNMOS_E_NOT_FOUND for 404.
dtnmos_result dtnmos_query_get_json(dtnmos_query* query, const char* url,
                                    dtnmos_json** json, dtnmos_error* error);
