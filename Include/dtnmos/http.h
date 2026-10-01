// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# http.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
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

// A header of a request or a response.
typedef struct dtnmos_http_header
{
    const char* name;
    const char* value;
} dtnmos_http_header;

// A request: of the library to a server for a client request, or of a client to the
// library for a request the library answers.
typedef struct dtnmos_http_request
{
    size_t size;        // sizeof(dtnmos_http_request)
    const char* method; // "GET", "POST", "PUT", "PATCH" or "DELETE"
    const char* url;    // absolute URL of a client request; path and query of another
    const char* content_type; // of body; null without body
    const char* body;
    size_t body_length;
    uint32_t timeout_ms; // of a client request: how long it may take in all
} dtnmos_http_request;

// A response: its status, headers and body, which it owns.
typedef struct dtnmos_http_response dtnmos_http_response;

// Creates an empty response, with status 0, for a caller that answers requests of its
// own with the library; returns null when out of memory.
DTNMOS_API dtnmos_http_response* dtnmos_http_response_create(void);
DTNMOS_API void dtnmos_http_response_free(dtnmos_http_response* response);

DTNMOS_API void dtnmos_http_response_set_status(dtnmos_http_response* response,
                                                int status);

// Makes the body length bytes of body, which it copies, of content_type, which may be
// null.
DTNMOS_API dtnmos_result dtnmos_http_response_set_body(dtnmos_http_response* response,
                                                       const char* content_type,
                                                       const char* body, size_t length);

// Appends length bytes of body, which it copies, as a transport receives it in pieces.
DTNMOS_API dtnmos_result dtnmos_http_response_append_body(dtnmos_http_response* response,
                                                          const char* body,
                                                          size_t length);

// Adds a header, whose name and value it copies.
DTNMOS_API dtnmos_result dtnmos_http_response_add_header(dtnmos_http_response* response,
                                                         const char* name,
                                                         const char* value);

DTNMOS_API int dtnmos_http_response_status(const dtnmos_http_response* response);

// Returns the body, ending in a null character, and its length when length is not null.
DTNMOS_API const char* dtnmos_http_response_body(const dtnmos_http_response* response,
                                                 size_t* length);

// Returns the content type of the body; "" when it has none.
DTNMOS_API const char*
dtnmos_http_response_content_type(const dtnmos_http_response* response);

// Returns the number of headers, and the header at index, whose strings the response
// owns.
DTNMOS_API size_t dtnmos_http_response_header_count(const dtnmos_http_response* response);
DTNMOS_API dtnmos_http_header
dtnmos_http_response_header(const dtnmos_http_response* response, size_t index);

// Returns the value of the first header called name, compared without regard to case,
// or null when there is none.
DTNMOS_API const char*
dtnmos_http_response_find_header(const dtnmos_http_response* response, const char* name);

// Performs a client request and fills response, which is empty. Returns DTNMOS_OK when
// an answer came, whatever its status; DTNMOS_E_TIMEOUT when none came in time, and
// DTNMOS_E_HTTP when the server could not be reached or the exchange failed, with a
// message in error. Called on the thread of the caller of the library.
typedef dtnmos_result (*dtnmos_http_fn)(void* user, const dtnmos_http_request* request,
                                        dtnmos_http_response* response,
                                        dtnmos_error* error);

// Whether the library was built with the transport on libcurl.
DTNMOS_API int dtnmos_has_curl(void);

// A dtnmos_http_fn on libcurl, for HTTP and HTTPS; user is unused. Without libcurl it
// fails with DTNMOS_E_STATE.
DTNMOS_API dtnmos_result dtnmos_curl_http(void* user, const dtnmos_http_request* request,
                                          dtnmos_http_response* response,
                                          dtnmos_error* error);

#ifdef __cplusplus
}
#endif
