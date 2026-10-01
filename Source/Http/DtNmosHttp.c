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

typedef struct NmosOwnedHeader
{
    char* name;
    char* Value;
} NmosOwnedHeader;

struct DtNmosHttpResponse
{
    int Status;
    NmosBuffer Body;
    char* ContentType;
    NmosOwnedHeader* Headers;
    size_t HeaderCount;
    size_t HeaderCapacity;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static char* CopyText(const char* Text)
{
    const size_t Length = Text == NULL ? 0 : strlen(Text);
    char* Copy = malloc(Length + 1);
    if (Copy != NULL)
    {
        if (Length > 0)
        {
            memcpy(Copy, Text, Length);
        }
        Copy[Length] = '\0';
    }
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosHttpResponse* DtNmosHttpResponse_Alloc(void)
{
    return calloc(1, sizeof(DtNmosHttpResponse));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosHttpResponse_Free(DtNmosHttpResponse* Response)
{
    if (Response == NULL)
    {
        return;
    }
    NmosBuffer_Free(&Response->Body);
    free(Response->ContentType);
    for (size_t i = 0; i < Response->HeaderCount; ++i)
    {
        free(Response->Headers[i].name);
        free(Response->Headers[i].Value);
    }
    free(Response->Headers);
    free(Response);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosHttpResponse_Freep(DtNmosHttpResponse** Response)
{
    if (Response != NULL)
    {
        DtNmosHttpResponse_Free(*Response);
        *Response = NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_SetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosHttpResponse_SetStatus(DtNmosHttpResponse* Response, int Status)
{
    if (Response != NULL)
    {
        Response->Status = Status;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_SetBody -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosHttpResponse_SetBody(DtNmosHttpResponse* Response,
                                        const char* ContentType, const char* Body,
                                        size_t Length)
{
    if (Response == NULL || (Body == NULL && Length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    char* Type = NULL;
    if (ContentType != NULL)
    {
        Type = CopyText(ContentType);
        if (Type == NULL)
        {
            return DTNMOS_E_NO_MEMORY;
        }
    }
    free(Response->ContentType);
    Response->ContentType = Type;
    Response->Body.Length = 0;
    return DtNmosHttpResponse_AppendBody(Response, Body, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_AppendBody -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosHttpResponse_AppendBody(DtNmosHttpResponse* Response, const char* Body,
                                           size_t Length)
{
    if (Response == NULL || (Body == NULL && Length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    // An empty body still holds its null character.
    NmosBuffer_Append(&Response->Body, Length > 0 ? Body : "", Length);
    return Response->Body.Failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_AddHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosHttpResponse_AddHeader(DtNmosHttpResponse* Response, const char* name,
                                          const char* Value)
{
    if (Response == NULL || name == NULL || Value == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (Response->HeaderCount == Response->HeaderCapacity)
    {
        const size_t Capacity =
            Response->HeaderCapacity == 0 ? 8 : Response->HeaderCapacity * 2;
        NmosOwnedHeader* Headers =
            realloc(Response->Headers, Capacity * sizeof(*Headers));
        if (Headers == NULL)
        {
            return DTNMOS_E_NO_MEMORY;
        }
        Response->Headers = Headers;
        Response->HeaderCapacity = Capacity;
    }
    NmosOwnedHeader Header = {CopyText(name), CopyText(Value)};
    if (Header.name == NULL || Header.Value == NULL)
    {
        free(Header.name);
        free(Header.Value);
        return DTNMOS_E_NO_MEMORY;
    }
    if (NmosSpan_Equals(NmosSpan_Of(name), "content-type", 1) &&
        Response->ContentType == NULL)
    {
        Response->ContentType = CopyText(Value);
    }
    Response->Headers[Response->HeaderCount++] = Header;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Status -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtNmosHttpResponse_Status(const DtNmosHttpResponse* Response)
{
    return Response == NULL ? 0 : Response->Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Body -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosHttpResponse_Body(const DtNmosHttpResponse* Response, size_t* Length)
{
    if (Length != NULL)
    {
        *Length = Response == NULL ? 0 : Response->Body.Length;
    }
    return Response == NULL || Response->Body.Data == NULL ? "" : Response->Body.Data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_ContentType -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosHttpResponse_ContentType(const DtNmosHttpResponse* Response)
{
    return Response == NULL || Response->ContentType == NULL ? "" : Response->ContentType;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_HeaderCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosHttpResponse_HeaderCount(const DtNmosHttpResponse* Response)
{
    return Response == NULL ? 0 : Response->HeaderCount;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_Header -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosHttpHeader DtNmosHttpResponse_Header(const DtNmosHttpResponse* Response,
                                           size_t Index)
{
    DtNmosHttpHeader Header = {NULL, NULL};
    if (Response != NULL && Index < Response->HeaderCount)
    {
        Header.Name = Response->Headers[Index].name;
        Header.Value = Response->Headers[Index].Value;
    }
    return Header;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosHttpResponse_FindHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosHttpResponse_FindHeader(const DtNmosHttpResponse* Response,
                                          const char* name)
{
    if (Response == NULL || name == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < Response->HeaderCount; ++i)
    {
        if (NmosSpan_Equals(NmosSpan_Of(Response->Headers[i].name), name, 1))
        {
            return Response->Headers[i].Value;
        }
    }
    return NULL;
}
