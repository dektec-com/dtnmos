// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# text.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Reading text
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_buffer_append -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_buffer_append(dtnmos_buffer* buffer, const char* text, size_t length)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_buffer_printf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_buffer_printf(dtnmos_buffer* buffer, const char* format, ...)
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
        dtnmos_buffer_append(buffer, local, (size_t)needed);
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
    dtnmos_buffer_append(buffer, text, (size_t)needed);
    free(text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_buffer_free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_buffer_free(dtnmos_buffer* buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_span_of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_span dtnmos_span_of(const char* text)
{
    dtnmos_span span = {text, text == NULL ? 0 : strlen(text)};
    return span;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_span_trim -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_span dtnmos_span_trim(dtnmos_span span)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_span_equals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int dtnmos_span_equals(dtnmos_span span, const char* text, int fold)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_span_starts_with -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int dtnmos_span_starts_with(dtnmos_span span, const char* prefix)
{
    const size_t length = strlen(prefix);
    return span.length >= length && memcmp(span.data, prefix, length) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_span_split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
dtnmos_span dtnmos_span_split(dtnmos_span span, char separator, dtnmos_span* head)
{
    const char* found =
        span.length == 0 ? NULL : memchr(span.data, separator, span.length);
    if (found == NULL)
    {
        *head = span;
        dtnmos_span rest = {NULL, 0};
        return rest;
    }
    head->data = span.data;
    head->length = (size_t)(found - span.data);
    dtnmos_span rest = {found + 1, span.length - head->length - 1};
    return rest;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_parse_u64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int dtnmos_parse_u64(dtnmos_span span, uint64_t maximum, uint64_t* value)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_parse_u32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int dtnmos_parse_u32(dtnmos_span span, uint32_t maximum, uint32_t* value)
{
    uint64_t result = 0;
    if (!dtnmos_parse_u64(span, maximum, &result))
    {
        return 0;
    }
    *value = (uint32_t)result;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_parse_rate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int dtnmos_parse_rate(dtnmos_span span, uint32_t* numerator, uint32_t* denominator)
{
    dtnmos_span head;
    const dtnmos_span rest = dtnmos_span_split(span, '/', &head);
    uint32_t n = 0;
    uint32_t d = 1;
    if (!dtnmos_parse_u32(head, UINT32_MAX, &n))
    {
        return 0;
    }
    if (rest.data != NULL && (!dtnmos_parse_u32(rest, UINT32_MAX, &d) || d == 0))
    {
        return 0;
    }
    *numerator = n;
    *denominator = d;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_parse_milliseconds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int dtnmos_parse_milliseconds(dtnmos_span span, uint32_t* nanoseconds)
{
    dtnmos_span whole;
    const dtnmos_span fraction = dtnmos_span_split(span, '.', &whole);
    uint32_t milliseconds = 0;
    if (!dtnmos_parse_u32(whole, 4000, &milliseconds))
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
        if (!dtnmos_parse_u32(fraction, 999999, &digits))
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_parse_byte -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int dtnmos_parse_byte(dtnmos_span span, uint8_t* value)
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
    if (!dtnmos_parse_u32(span, 255, &result))
    {
        return 0;
    }
    *value = (uint8_t)result;
    return 1;
}
