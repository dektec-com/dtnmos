// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosQuery.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Helpers for requests to a registry's Query API, shared by the controller
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "NmosJson.h"
#include "dtnmos_query.h"

// Checks that a query is open. Fails with DTNMOS_E_STATE when it is not; the message
// names Function.
DtNmosResult NmosQuery_CheckOpen(const DtNmosQuery* Query, const char* Function);

// Returns the base URL of the registry's Query API. It ends in a slash.
const char* NmosQuery_Base(const DtNmosQuery* Query);

// Returns the time a request may take, in milliseconds.
uint32_t NmosQuery_Timeout(const DtNmosQuery* Query);

// Sends an HTTP request through the query's HTTP function, with its timeout, and logs
// it. Body may be null. Fails only when the HTTP function fails; an error status in the
// answer is not a failure here.
DtNmosResult NmosQuery_Request(DtNmosQuery* Query, const char* Method, const char* Url,
                               const char* ContentType, const char* Body,
                               size_t BodyLength, DtNmosHttpResponse* Response);

// Gets a JSON resource: a GET of Url, whose answer is parsed into *Json. The caller frees
// *Json.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_NOT_FOUND  the answer was 404
//   DTNMOS_E_HTTP       the answer was another status than 200
//   DTNMOS_E_PARSE      the answer is not JSON
// and the errors of the request.
DtNmosResult NmosQuery_GetJson(DtNmosQuery* Query, const char* Url, NmosJson** Json);

// Downloads the SDP of a sender, from its manifest_href, into *Response. The caller frees
// it; it is null after a failure. Fails as DtNmosQuery_SenderManifest() does.
DtNmosResult NmosQuery_Manifest(DtNmosQuery* Query, const DtNmosSenderInfo* Sender,
                                DtNmosHttpResponse** Response);
