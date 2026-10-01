// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Results, errors, ids and logging, which every header uses
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "dtnmos_version.h" // DTNMOS_VERSION, generated from the project's version

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

// Returns the version of the library the program runs with, which may differ from the
// DTNMOS_VERSION of dtnmos_version.h it was built against; each pointer may be null.
DTNMOS_API void DtNmos_Version(int* Major, int* Minor, int* Patch);

typedef enum DtNmosResult
{
    DTNMOS_OK = 0,
    // A result at DTNMOS_E or above is a failure; one below it a success, as with
    // CDTAPI's DTAPI_E.
    DTNMOS_E = 0x1000,
    DTNMOS_E_INVALID_ARGUMENT =
        DTNMOS_E + 1,                  // a parameter is null, empty or out of range
    DTNMOS_E_PARSE = DTNMOS_E + 2,     // an SDP or JSON document is malformed
    DTNMOS_E_NOT_FOUND = DTNMOS_E + 3, // the registry has no such resource
    DTNMOS_E_AMBIGUOUS = DTNMOS_E + 4, // a label names more than one resource
    DTNMOS_E_HTTP = DTNMOS_E + 5, // a request failed or was answered with an error status
    DTNMOS_E_TIMEOUT = DTNMOS_E + 6, // a request got no answer in time
    DTNMOS_E_STATE = DTNMOS_E + 7,   // the handle is not in a state that allows the call
    DTNMOS_E_NO_MEMORY = DTNMOS_E + 8,
    DTNMOS_E_INTERNAL = DTNMOS_E + 9,
    DTNMOS_E_NETWORK = DTNMOS_E + 10, // a socket could not be opened, or could not send
    DTNMOS_E_BUFFER_TOO_SMALL = DTNMOS_E + 11 // a text does not fit the caller's buffer
} DtNmosResult;

// Returns the name of a result, e.g. "DTNMOS_E_NOT_FOUND"; a static string.
DTNMOS_API const char* DtNmosResult_Name(DtNmosResult Result);

// Returns the message of the last failure of a call on this thread, in English, naming
// what failed and why; "" when none failed yet. A call that fails sets it, and one that
// succeeds leaves it as it was. The text is the thread's own, valid until the next call
// on it fails.
DTNMOS_API const char* DtNmos_GetLastError(void);

// Sets the message DtNmos_GetLastError() returns on this thread, and returns result. A
// callback the library calls, an activation of the node or an HTTP function, fails this
// way: it returns DtNmos_SetLastError(DTNMOS_E_..., "what failed"), and the library
// passes the message on, as the answer of the node to a controller for one.
DTNMOS_API DtNmosResult DtNmos_SetLastError(DtNmosResult Result, const char* Message);

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
DTNMOS_API DtNmosResult DtNmosId_FromName(const DtNmosId* NamespaceId, const char* name,
                                          DtNmosId* Id);

// Where log messages go.
typedef enum DtNmosLogLevel
{
    DTNMOS_LOG_DEBUG = 0,
    DTNMOS_LOG_INFO = 1,
    DTNMOS_LOG_WARNING = 2,
    DTNMOS_LOG_ERROR = 3
} DtNmosLogLevel;

typedef void (*DtNmosLogFunc)(void* User, DtNmosLogLevel Level, const char* Message);

#ifdef __cplusplus
}
#endif
