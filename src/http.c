// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# http.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The HTTP response, which owns its status, headers and body
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/http.h"

#include <stdlib.h>
#include <string.h>

#include "internal.h"

typedef struct owned_header
{
    char* name;
    char* value;
} owned_header;

struct dtnmos_http_response
{
    int status;
    dtnmos_buffer body;
    char* content_type;
    owned_header* headers;
    size_t header_count;
    size_t header_capacity;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static char* copy_text(const char* text)
{
    const size_t length = text == NULL ? 0 : strlen(text);
    char* copy = malloc(length + 1);
    if (copy != NULL)
    {
        if (length > 0)
        {
            memcpy(copy, text, length);
        }
        copy[length] = '\0';
    }
    return copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
dtnmos_http_response* dtnmos_http_response_create(void)
{
    return calloc(1, sizeof(dtnmos_http_response));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dtnmos_http_response_free(dtnmos_http_response* response)
{
    if (response == NULL)
    {
        return;
    }
    dtnmos_buffer_free(&response->body);
    free(response->content_type);
    for (size_t i = 0; i < response->header_count; ++i)
    {
        free(response->headers[i].name);
        free(response->headers[i].value);
    }
    free(response->headers);
    free(response);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_set_status -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dtnmos_http_response_set_status(dtnmos_http_response* response, int status)
{
    if (response != NULL)
    {
        response->status = status;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_set_body -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
dtnmos_result dtnmos_http_response_set_body(dtnmos_http_response* response,
                                            const char* content_type, const char* body,
                                            size_t length)
{
    if (response == NULL || (body == NULL && length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    char* type = NULL;
    if (content_type != NULL)
    {
        type = copy_text(content_type);
        if (type == NULL)
        {
            return DTNMOS_E_NO_MEMORY;
        }
    }
    free(response->content_type);
    response->content_type = type;
    response->body.length = 0;
    return dtnmos_http_response_append_body(response, body, length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_append_body -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_http_response_append_body(dtnmos_http_response* response,
                                               const char* body, size_t length)
{
    if (response == NULL || (body == NULL && length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    // An empty body still holds its null character.
    dtnmos_buffer_append(&response->body, length > 0 ? body : "", length);
    return response->body.failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_add_header -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
dtnmos_result dtnmos_http_response_add_header(dtnmos_http_response* response,
                                              const char* name, const char* value)
{
    if (response == NULL || name == NULL || value == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (response->header_count == response->header_capacity)
    {
        const size_t capacity =
            response->header_capacity == 0 ? 8 : response->header_capacity * 2;
        owned_header* headers = realloc(response->headers, capacity * sizeof(*headers));
        if (headers == NULL)
        {
            return DTNMOS_E_NO_MEMORY;
        }
        response->headers = headers;
        response->header_capacity = capacity;
    }
    owned_header header = {copy_text(name), copy_text(value)};
    if (header.name == NULL || header.value == NULL)
    {
        free(header.name);
        free(header.value);
        return DTNMOS_E_NO_MEMORY;
    }
    if (dtnmos_span_equals(dtnmos_span_of(name), "content-type", 1) &&
        response->content_type == NULL)
    {
        response->content_type = copy_text(value);
    }
    response->headers[response->header_count++] = header;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_status -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int dtnmos_http_response_status(const dtnmos_http_response* response)
{
    return response == NULL ? 0 : response->status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_body -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* dtnmos_http_response_body(const dtnmos_http_response* response,
                                      size_t* length)
{
    if (length != NULL)
    {
        *length = response == NULL ? 0 : response->body.length;
    }
    return response == NULL || response->body.data == NULL ? "" : response->body.data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_content_type -.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* dtnmos_http_response_content_type(const dtnmos_http_response* response)
{
    return response == NULL || response->content_type == NULL ? ""
                                                              : response->content_type;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_header_count -.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t dtnmos_http_response_header_count(const dtnmos_http_response* response)
{
    return response == NULL ? 0 : response->header_count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_header -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
dtnmos_http_header dtnmos_http_response_header(const dtnmos_http_response* response,
                                               size_t index)
{
    dtnmos_http_header header = {NULL, NULL};
    if (response != NULL && index < response->header_count)
    {
        header.name = response->headers[index].name;
        header.value = response->headers[index].value;
    }
    return header;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_http_response_find_header -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* dtnmos_http_response_find_header(const dtnmos_http_response* response,
                                             const char* name)
{
    if (response == NULL || name == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < response->header_count; ++i)
    {
        if (dtnmos_span_equals(dtnmos_span_of(response->headers[i].name), name, 1))
        {
            return response->headers[i].value;
        }
    }
    return NULL;
}
