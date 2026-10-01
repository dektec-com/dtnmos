// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtNmosString.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - DtNmosString
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosString_Get -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosString_Get(const DtNmosString* string)
{
    if (string == NULL)
    {
        return "";
    }
    return string->Heap != NULL ? string->Heap : string->Local;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosString_Length -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtNmosString_Length(const DtNmosString* string)
{
    return string == NULL ? 0 : string->Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosString_Set -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosString_Set(DtNmosString* string, const char* text, size_t length)
{
    if (string == NULL || (text == NULL && length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (length < sizeof(string->Local))
    {
        // text may lie in the heap string it replaces, so it is copied before that is
        // freed.
        char local[sizeof(string->Local)];
        if (length > 0)
        {
            memcpy(local, text, length);
        }
        local[length] = '\0';
        free(string->Heap);
        string->Heap = NULL;
        memcpy(string->Local, local, length + 1);
        string->Length = length;
        return DTNMOS_OK;
    }
    char* heap = malloc(length + 1);
    if (heap == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    memcpy(heap, text, length);
    heap[length] = '\0';
    free(string->Heap);
    string->Heap = heap;
    string->Local[0] = '\0';
    string->Length = length;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosString_SetText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosString_SetText(DtNmosString* string, const char* text)
{
    return DtNmosString_Set(string, text, text == NULL ? 0 : strlen(text));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosString_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosString_Copy(DtNmosString* target, const DtNmosString* source)
{
    if (target == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    return DtNmosString_Set(target, DtNmosString_Get(source),
                            DtNmosString_Length(source));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosString_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosString_Clear(DtNmosString* string)
{
    if (string == NULL)
    {
        return;
    }
    free(string->Heap);
    memset(string, 0, sizeof(*string));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_string_set_span -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult dtnmos_string_set_span(DtNmosString* string, dtnmos_span span)
{
    return DtNmosString_Set(string, span.data, span.length);
}
