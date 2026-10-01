// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosQuery.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - What the controller shares of the query of a registry
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "NmosJson.h"
#include "dtnmos_query.h"

// Fails with DTNMOS_E_STATE, naming function, when query is not open.
DtNmosResult NmosQuery_CheckOpen(const DtNmosQuery* query, const char* function);

// Returns the base URL of the Query API of query, ending in a slash.
const char* NmosQuery_Base(const DtNmosQuery* query);

// Returns how long a request of query may take, in milliseconds.
uint32_t NmosQuery_Timeout(const DtNmosQuery* query);

// Sends a request with the HTTP function and the timeout of query, and logs it; the body
// may be null. Fails only when the HTTP function does, whatever status the answer has.
DtNmosResult NmosQuery_Request(DtNmosQuery* query, const char* method, const char* url,
                               const char* content_type, const char* body,
                               size_t body_length, DtNmosHttpResponse* response);

// Performs a GET of url and parses its JSON, which the caller frees. Fails unless the
// answer is 200, with DTNMOS_E_NOT_FOUND for 404.
DtNmosResult NmosQuery_GetJson(DtNmosQuery* query, const char* url, NmosJson** json);

// Fetches the SDP of sender from its manifest_href into a response, which the caller
// frees; null when it fails. Fails as DtNmosQuery_SenderManifest() does.
DtNmosResult NmosQuery_Manifest(DtNmosQuery* query, const DtNmosSenderInfo* sender,
                                DtNmosHttpResponse** response);
