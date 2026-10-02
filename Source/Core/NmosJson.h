// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosJson.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A small JSON (RFC 8259) reader and writer, for the NMOS resources
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>

#include "NmosInternal.h"
#include "dtnmos.h"

// The type of a JSON value.
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

// A parsed JSON value. Only the fields of its type are used.
typedef struct NmosJson
{
    NmosJsonType Type;      // what kind of value this is
    double Number;          // the value of a number
    char* String;           // the text of a string, unescaped, null-terminated
    size_t StringLength;    // the length of String
    struct NmosJson* Items; // the items of an array, or the member values of an object
    char** Keys;            // the member names of an object, unescaped, one per item
    size_t Count;           // the number of items or members
} NmosJson;

// Parses Length bytes of JSON text into *Value, which NmosJson_Free() frees. Fails with
// DTNMOS_E_PARSE; the message gives the offset of the error.
DtNmosResult NmosJson_Parse(const char* Text, size_t Length, NmosJson** Value);
// Frees a value that NmosJson_Parse() returned. Value may be null.
void NmosJson_Free(NmosJson* Value);

// Returns the member of an object with name Key. Returns null when Value is not an
// object or has no such member.
const NmosJson* NmosJson_Member(const NmosJson* Value, const char* Key);

// Returns the text of a string. Returns null when Value is null or not a string.
const char* NmosJson_Text(const NmosJson* Value);

// Returns the text of the member Key of an object, or null when there is no such string
// member.
const char* NmosJson_MemberText(const NmosJson* Value, const char* Key);

// Appends Text to Buffer as a JSON string: quoted, with escapes where JSON needs them.
void NmosJson_WriteString(NmosBuffer* Buffer, const char* Text);

// Appends Value to Buffer as JSON text, without spaces. NmosJson_Parse() reads it back as
// the same value.
void NmosJson_Write(NmosBuffer* Buffer, const NmosJson* Value);
