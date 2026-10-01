// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Results, errors, strings, ids and logging, which every header uses
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#if defined(DTNMOS_SHARED)
    #if defined(_WIN32)
        #if defined(DTNMOS_BUILDING)
            #define DTNMOS_API __declspec(dllexport)
        #else
            #define DTNMOS_API __declspec(dllimport)
        #endif
    #else
        #define DTNMOS_API __attribute__((visibility("default")))
    #endif
#else
    #define DTNMOS_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define DTNMOS_VERSION_MAJOR 0
#define DTNMOS_VERSION_MINOR 1
#define DTNMOS_VERSION_PATCH 1

// Returns the version of the library the program runs with; each pointer may be null.
DTNMOS_API void DtNmos_Version(int* major, int* minor, int* patch);

typedef enum DtNmosResult
{
    DTNMOS_OK = 0,
    DTNMOS_E_INVALID_ARGUMENT, // a parameter is null, empty or out of range
    DTNMOS_E_PARSE,            // an SDP or JSON document is malformed
    DTNMOS_E_NOT_FOUND,        // the registry has no such resource
    DTNMOS_E_AMBIGUOUS,        // a label names more than one resource
    DTNMOS_E_HTTP,             // a request failed or was answered with an error status
    DTNMOS_E_TIMEOUT,          // a request got no answer in time
    DTNMOS_E_STATE,            // the handle is not in a state that allows the call
    DTNMOS_E_NO_MEMORY,
    DTNMOS_E_INTERNAL,
    DTNMOS_E_NETWORK // a socket could not be opened, or could not send
} DtNmosResult;

// Returns the name of a result, e.g. "DTNMOS_E_NOT_FOUND"; a static string.
DTNMOS_API const char* DtNmosResult_Name(DtNmosResult result);

// A failure: its code, and a message in English that names what failed and why. The
// message is a fixed array, so that reporting a failure never allocates.
typedef struct DtNmosError
{
    DtNmosResult Code;
    char Message[512];
} DtNmosError;

// A string the library hands out. A DtNmosString set to zero is valid and empty, so a
// struct that holds strings needs nothing but = {0} before the library fills it. Short
// strings live in the struct itself and longer ones on the heap; the members are
// private, the functions below reach them.
//
// A struct that holds strings has a _clear() that frees them and a _copy() that copies
// them. Copying such a struct with = shares its heap strings, so that clearing both
// frees them twice: use its _copy().
typedef struct DtNmosString
{
    size_t Length;
    char* Heap;
    char Local[24];
} DtNmosString;

// Returns the text of string, ending in a null character; never null, "" when string is
// empty or null. Valid until the string is changed or cleared.
DTNMOS_API const char* DtNmosString_Get(const DtNmosString* string);

// Returns the length of string in bytes, without the null character; 0 when string is
// null.
DTNMOS_API size_t DtNmosString_Length(const DtNmosString* string);

// Makes string the length bytes of text, which need not end in a null character and may
// lie in string itself. Fails with DTNMOS_E_NO_MEMORY, leaving string as it was.
DTNMOS_API DtNmosResult DtNmosString_Set(DtNmosString* string, const char* text,
                                         size_t length);

// Makes string the text up to the null character of text; null makes it empty.
DTNMOS_API DtNmosResult DtNmosString_SetText(DtNmosString* string, const char* text);

// Makes target a copy of source.
DTNMOS_API DtNmosResult DtNmosString_Copy(DtNmosString* target,
                                          const DtNmosString* source);

// Frees what string holds and leaves it empty.
DTNMOS_API void DtNmosString_Clear(DtNmosString* string);

// An NMOS resource ID: a UUID in its text form, lower case, e.g.
// "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01".
typedef struct DtNmosId
{
    char Text[37];
} DtNmosId;

// Writes the name-based UUID (version 5, RFC 9562) of name in the namespace
// namespace_id, so that a node, a device, a sender or a receiver keeps its ID across
// restarts, e.g. from the serial number of a card, its port and the label of an
// element. Fails with DTNMOS_E_INVALID_ARGUMENT when namespace_id is no UUID.
DTNMOS_API DtNmosResult DtNmosId_FromName(const DtNmosId* namespace_id, const char* name,
                                          DtNmosId* id, DtNmosError* error);

// Where log messages go.
typedef enum DtNmosLogLevel
{
    DTNMOS_LOG_DEBUG = 0,
    DTNMOS_LOG_INFO = 1,
    DTNMOS_LOG_WARNING = 2,
    DTNMOS_LOG_ERROR = 3
} DtNmosLogLevel;

typedef void (*DtNmosLogFunc)(void* user, DtNmosLogLevel level, const char* message);

#ifdef __cplusplus
}
#endif
