// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosJson.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - JSON (RFC 8259) as dtnmos reads the resources of NMOS and writes its own
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>

#include "NmosInternal.h"
#include "dtnmos.h"

typedef enum NmosJsonType
{
    DTNMOS_JSON_NULL,
    DTNMOS_JSON_FALSE,
    DTNMOS_JSON_TRUE,
    DTNMOS_JSON_NUMBER,
    DTNMOS_JSON_STRING,
    DTNMOS_JSON_ARRAY,
    DTNMOS_JSON_OBJECT
} NmosJsonType;

// A value. An array holds count items; an object holds count items with a key each.
typedef struct NmosJson
{
    NmosJsonType type;
    double number;
    char* string; // a string, decoded, ending in a null character
    size_t string_length;
    struct NmosJson* items;
    char** keys; // of an object, decoded
    size_t count;
} NmosJson;

// Parses length bytes of text into a value, which NmosJson_Free() frees. Fails with
// DTNMOS_E_PARSE naming the offset of what is wrong.
DtNmosResult NmosJson_Parse(const char* text, size_t length, NmosJson** value);
void NmosJson_Free(NmosJson* value);

// Returns the member key of an object, or null when value is no object or lacks it.
const NmosJson* NmosJson_Member(const NmosJson* value, const char* key);

// Returns the text of a string value, or null when value is null or no string.
const char* NmosJson_Text(const NmosJson* value);

// Returns the text of the string member key of an object, or null.
const char* NmosJson_MemberText(const NmosJson* value, const char* key);

// Appends text as a JSON string, quoted and escaped.
void NmosJson_WriteString(NmosBuffer* buffer, const char* text);

// Appends value as JSON text, without spaces, which NmosJson_Parse() reads back as the
// same value.
void NmosJson_Write(NmosBuffer* buffer, const NmosJson* value);
