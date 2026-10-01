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
    NmosJsonType Type;
    double Number;
    char* String; // a string, decoded, ending in a null character
    size_t StringLength;
    struct NmosJson* Items;
    char** Keys; // of an object, decoded
    size_t Count;
} NmosJson;

// Parses length bytes of text into a value, which NmosJson_Free() frees. Fails with
// DTNMOS_E_PARSE naming the offset of what is wrong.
DtNmosResult NmosJson_Parse(const char* Text, size_t Length, NmosJson** Value);
void NmosJson_Free(NmosJson* Value);

// Returns the member key of an object, or null when value is no object or lacks it.
const NmosJson* NmosJson_Member(const NmosJson* Value, const char* Key);

// Returns the text of a string value, or null when value is null or no string.
const char* NmosJson_Text(const NmosJson* Value);

// Returns the text of the string member key of an object, or null.
const char* NmosJson_MemberText(const NmosJson* Value, const char* Key);

// Appends text as a JSON string, quoted and escaped.
void NmosJson_WriteString(NmosBuffer* Buffer, const char* Text);

// Appends value as JSON text, without spaces, which NmosJson_Parse() reads back as the
// same value.
void NmosJson_Write(NmosBuffer* Buffer, const NmosJson* Value);
