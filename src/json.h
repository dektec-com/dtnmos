// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# json.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - JSON (RFC 8259) as dtnmos reads the resources of NMOS and writes its own
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>

#include "dtnmos/dtnmos.h"
#include "internal.h"

typedef enum dtnmos_json_type
{
    DTNMOS_JSON_NULL,
    DTNMOS_JSON_FALSE,
    DTNMOS_JSON_TRUE,
    DTNMOS_JSON_NUMBER,
    DTNMOS_JSON_STRING,
    DTNMOS_JSON_ARRAY,
    DTNMOS_JSON_OBJECT
} dtnmos_json_type;

// A value. An array holds count items; an object holds count items with a key each.
typedef struct dtnmos_json
{
    dtnmos_json_type type;
    double number;
    char* string; // a string, decoded, ending in a null character
    size_t string_length;
    struct dtnmos_json* items;
    char** keys; // of an object, decoded
    size_t count;
} dtnmos_json;

// Parses length bytes of text into a value, which dtnmos_json_free() frees. Fails with
// DTNMOS_E_PARSE naming the offset of what is wrong.
dtnmos_result dtnmos_json_parse(const char* text, size_t length, dtnmos_json** value,
                                dtnmos_error* error);
void dtnmos_json_free(dtnmos_json* value);

// Returns the member key of an object, or null when value is no object or lacks it.
const dtnmos_json* dtnmos_json_member(const dtnmos_json* value, const char* key);

// Returns the text of a string value, or null when value is null or no string.
const char* dtnmos_json_text(const dtnmos_json* value);

// Returns the text of the string member key of an object, or null.
const char* dtnmos_json_member_text(const dtnmos_json* value, const char* key);

// Appends text as a JSON string, quoted and escaped.
void dtnmos_json_write_string(dtnmos_buffer* buffer, const char* text);
