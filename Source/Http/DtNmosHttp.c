// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosHttp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The HTTP response, which owns its status, headers and body
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_http.h"

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"

typedef struct owned_header
{
    char* name;
    char* value;
} owned_header;

struct DtNmosHttpResponse
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosHttpResponse* DtNmosHttpResponse_Create(void)
{
    return calloc(1, sizeof(DtNmosHttpResponse));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosHttpResponse_Free(DtNmosHttpResponse* response)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_SetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosHttpResponse_SetStatus(DtNmosHttpResponse* response, int status)
{
    if (response != NULL)
    {
        response->status = status;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_SetBody -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosHttpResponse_SetBody(DtNmosHttpResponse* response,
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
    return DtNmosHttpResponse_AppendBody(response, body, length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_AppendBody -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosHttpResponse_AppendBody(DtNmosHttpResponse* response, const char* body,
                                           size_t length)
{
    if (response == NULL || (body == NULL && length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    // An empty body still holds its null character.
    dtnmos_buffer_append(&response->body, length > 0 ? body : "", length);
    return response->body.failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_AddHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosHttpResponse_AddHeader(DtNmosHttpResponse* response, const char* name,
                                          const char* value)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Status -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtNmosHttpResponse_Status(const DtNmosHttpResponse* response)
{
    return response == NULL ? 0 : response->status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Body -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosHttpResponse_Body(const DtNmosHttpResponse* response, size_t* length)
{
    if (length != NULL)
    {
        *length = response == NULL ? 0 : response->body.length;
    }
    return response == NULL || response->body.data == NULL ? "" : response->body.data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_ContentType -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosHttpResponse_ContentType(const DtNmosHttpResponse* response)
{
    return response == NULL || response->content_type == NULL ? ""
                                                              : response->content_type;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_HeaderCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosHttpResponse_HeaderCount(const DtNmosHttpResponse* response)
{
    return response == NULL ? 0 : response->header_count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Header -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosHttpHeader DtNmosHttpResponse_Header(const DtNmosHttpResponse* response,
                                           size_t index)
{
    DtNmosHttpHeader header = {NULL, NULL};
    if (response != NULL && index < response->header_count)
    {
        header.Name = response->headers[index].name;
        header.Value = response->headers[index].value;
    }
    return header;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_FindHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosHttpResponse_FindHeader(const DtNmosHttpResponse* response,
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
