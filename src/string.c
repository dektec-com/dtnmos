// SPDX-License-Identifier: BSD-3-Clause
//
// dtnmos_string: a string whose short texts live in the struct and longer ones on the
// heap.

#include <stdlib.h>
#include <string.h>

#include "internal.h"

const char* dtnmos_string_get(const dtnmos_string* string)
{
    if (string == NULL)
    {
        return "";
    }
    return string->heap != NULL ? string->heap : string->local;
}

size_t dtnmos_string_length(const dtnmos_string* string)
{
    return string == NULL ? 0 : string->length;
}

dtnmos_result dtnmos_string_set(dtnmos_string* string, const char* text, size_t length)
{
    if (string == NULL || (text == NULL && length > 0))
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (length < sizeof(string->local))
    {
        // text may lie in the heap string it replaces, so it is copied before that is
        // freed.
        char local[sizeof(string->local)];
        if (length > 0)
        {
            memcpy(local, text, length);
        }
        local[length] = '\0';
        free(string->heap);
        string->heap = NULL;
        memcpy(string->local, local, length + 1);
        string->length = length;
        return DTNMOS_OK;
    }
    char* heap = malloc(length + 1);
    if (heap == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    memcpy(heap, text, length);
    heap[length] = '\0';
    free(string->heap);
    string->heap = heap;
    string->local[0] = '\0';
    string->length = length;
    return DTNMOS_OK;
}

dtnmos_result dtnmos_string_set_text(dtnmos_string* string, const char* text)
{
    return dtnmos_string_set(string, text, text == NULL ? 0 : strlen(text));
}

dtnmos_result dtnmos_string_copy(dtnmos_string* target, const dtnmos_string* source)
{
    if (target == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    return dtnmos_string_set(target, dtnmos_string_get(source),
                             dtnmos_string_length(source));
}

void dtnmos_string_clear(dtnmos_string* string)
{
    if (string == NULL)
    {
        return;
    }
    free(string->heap);
    memset(string, 0, sizeof(*string));
}

dtnmos_result dtnmos_string_set_span(dtnmos_string* string, dtnmos_span span)
{
    return dtnmos_string_set(string, span.data, span.length);
}
