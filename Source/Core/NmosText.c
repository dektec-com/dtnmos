// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosText.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Reading text
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosBuffer_Append -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosBuffer_Append(NmosBuffer* buffer, const char* text, size_t length)
{
    if (buffer->failed)
    {
        return;
    }
    if (buffer->length + length + 1 > buffer->capacity)
    {
        size_t capacity = buffer->capacity < 256 ? 256 : buffer->capacity;
        while (capacity < buffer->length + length + 1)
        {
            capacity *= 2;
        }
        char* data = realloc(buffer->data, capacity);
        if (data == NULL)
        {
            buffer->failed = 1;
            return;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosBuffer_Printf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosBuffer_Printf(NmosBuffer* buffer, const char* format, ...)
{
    char local[256];
    va_list arguments;
    va_start(arguments, format);
    const int needed = vsnprintf(local, sizeof(local), format, arguments);
    va_end(arguments);
    if (needed < 0)
    {
        buffer->failed = 1;
        return;
    }
    if ((size_t)needed < sizeof(local))
    {
        NmosBuffer_Append(buffer, local, (size_t)needed);
        return;
    }
    char* text = malloc((size_t)needed + 1);
    if (text == NULL)
    {
        buffer->failed = 1;
        return;
    }
    va_start(arguments, format);
    vsnprintf(text, (size_t)needed + 1, format, arguments);
    va_end(arguments);
    NmosBuffer_Append(buffer, text, (size_t)needed);
    free(text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosBuffer_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosBuffer_Free(NmosBuffer* buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosSpan NmosSpan_Of(const char* text)
{
    NmosSpan span = {text, text == NULL ? 0 : strlen(text)};
    return span;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Trim -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosSpan NmosSpan_Trim(NmosSpan span)
{
    while (span.length > 0 && (span.data[0] == ' ' || span.data[0] == '\t'))
    {
        ++span.data;
        --span.length;
    }
    while (span.length > 0 &&
           (span.data[span.length - 1] == ' ' || span.data[span.length - 1] == '\t'))
    {
        --span.length;
    }
    return span;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- lower -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Equals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int NmosSpan_Equals(NmosSpan span, const char* text, int fold)
{
    const size_t length = strlen(text);
    if (span.length != length)
    {
        return 0;
    }
    for (size_t i = 0; i < length; ++i)
    {
        const char a = fold ? lower(span.data[i]) : span.data[i];
        const char b = fold ? lower(text[i]) : text[i];
        if (a != b)
        {
            return 0;
        }
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_StartsWith -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int NmosSpan_StartsWith(NmosSpan span, const char* prefix)
{
    const size_t length = strlen(prefix);
    return span.length >= length && memcmp(span.data, prefix, length) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosSpan NmosSpan_Split(NmosSpan span, char separator, NmosSpan* head)
{
    const char* found =
        span.length == 0 ? NULL : memchr(span.data, separator, span.length);
    if (found == NULL)
    {
        *head = span;
        NmosSpan rest = {NULL, 0};
        return rest;
    }
    head->data = span.data;
    head->length = (size_t)(found - span.data);
    NmosSpan rest = {found + 1, span.length - head->length - 1};
    return rest;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseU64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int NmosText_ParseU64(NmosSpan span, uint64_t maximum, uint64_t* value)
{
    if (span.length == 0)
    {
        return 0;
    }
    uint64_t result = 0;
    for (size_t i = 0; i < span.length; ++i)
    {
        const char c = span.data[i];
        if (c < '0' || c > '9')
        {
            return 0;
        }
        const uint64_t digit = (uint64_t)(c - '0');
        if (result > (maximum - digit) / 10)
        {
            return 0;
        }
        result = result * 10 + digit;
    }
    *value = result;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseU32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int NmosText_ParseU32(NmosSpan span, uint32_t maximum, uint32_t* value)
{
    uint64_t result = 0;
    if (!NmosText_ParseU64(span, maximum, &result))
    {
        return 0;
    }
    *value = (uint32_t)result;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosText_ParseRate(NmosSpan span, uint32_t* numerator, uint32_t* denominator)
{
    NmosSpan head;
    const NmosSpan rest = NmosSpan_Split(span, '/', &head);
    uint32_t n = 0;
    uint32_t d = 1;
    if (!NmosText_ParseU32(head, UINT32_MAX, &n))
    {
        return 0;
    }
    if (rest.data != NULL && (!NmosText_ParseU32(rest, UINT32_MAX, &d) || d == 0))
    {
        return 0;
    }
    *numerator = n;
    *denominator = d;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseMilliseconds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosText_ParseMilliseconds(NmosSpan span, uint32_t* nanoseconds)
{
    NmosSpan whole;
    const NmosSpan fraction = NmosSpan_Split(span, '.', &whole);
    uint32_t milliseconds = 0;
    if (!NmosText_ParseU32(whole, 4000, &milliseconds))
    {
        return 0;
    }
    uint32_t result = milliseconds * 1000000u;
    if (fraction.data != NULL)
    {
        if (fraction.length == 0 || fraction.length > 6)
        {
            return 0;
        }
        uint32_t digits = 0;
        if (!NmosText_ParseU32(fraction, 999999, &digits))
        {
            return 0;
        }
        for (size_t i = fraction.length; i < 6; ++i)
        {
            digits *= 10;
        }
        result += digits;
    }
    *nanoseconds = result;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseByte -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosText_ParseByte(NmosSpan span, uint8_t* value)
{
    if (span.length > 2 && span.data[0] == '0' &&
        (span.data[1] == 'x' || span.data[1] == 'X'))
    {
        unsigned result = 0;
        for (size_t i = 2; i < span.length; ++i)
        {
            const char c = lower(span.data[i]);
            unsigned digit = 0;
            if (c >= '0' && c <= '9')
            {
                digit = (unsigned)(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                digit = (unsigned)(c - 'a' + 10);
            }
            else
            {
                return 0;
            }
            result = result * 16 + digit;
            if (result > 255)
            {
                return 0;
            }
        }
        *value = (uint8_t)result;
        return 1;
    }
    uint32_t result = 0;
    if (!NmosText_ParseU32(span, 255, &result))
    {
        return 0;
    }
    *value = (uint8_t)result;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_CopySpan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int NmosText_CopySpan(char* target, size_t size, NmosSpan span)
{
    if (size == 0)
    {
        return 0;
    }
    if (span.length >= size)
    {
        target[0] = '\0';
        return 0;
    }
    if (span.length > 0)
    {
        memcpy(target, span.data, span.length);
    }
    target[span.length] = '\0';
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- store_piece -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Allocates size bytes that store owns; null when the memory ran out.
//
static void* store_piece(NmosStore* store, size_t size)
{
    if (store->count == store->capacity)
    {
        const size_t capacity = store->capacity == 0 ? 8 : store->capacity * 2;
        void** pieces = realloc(store->pieces, capacity * sizeof(*pieces));
        if (pieces == NULL)
        {
            return NULL;
        }
        store->pieces = pieces;
        store->capacity = capacity;
    }
    void* piece = malloc(size);
    if (piece != NULL)
    {
        store->pieces[store->count++] = piece;
    }
    return piece;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosStore_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
char* NmosStore_Text(NmosStore* store, const char* data, size_t length)
{
    char* text = store_piece(store, length + 1);
    if (text != NULL)
    {
        if (length > 0)
        {
            memcpy(text, data, length);
        }
        text[length] = '\0';
    }
    return text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosStore_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void* NmosStore_Copy(NmosStore* store, const void* data, size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
    void* copy = store_piece(store, size);
    if (copy != NULL)
    {
        memcpy(copy, data, size);
    }
    return copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosStore_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosStore_Free(NmosStore* store)
{
    for (size_t i = 0; i < store->count; ++i)
    {
        free(store->pieces[i]);
    }
    free(store->pieces);
    memset(store, 0, sizeof(*store));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosText_CopyText(char* buffer, size_t* size, const char* data,
                               size_t length)
{
    if (buffer == NULL || *size < length + 1)
    {
        const size_t had = *size;
        *size = length + 1;
        return NmosError_Fail(DTNMOS_E_BUFFER_TOO_SMALL,
                              "The text needs %zu bytes, and the buffer has %zu.",
                              length + 1, buffer == NULL ? (size_t)0 : had);
    }
    if (length > 0)
    {
        memcpy(buffer, data, length);
    }
    buffer[length] = '\0';
    *size = length;
    return DTNMOS_OK;
}
