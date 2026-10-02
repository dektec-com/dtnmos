// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Results, errors, ids and logging, which every header uses
//
// SPDX-License-Identifier: BSD-3-Clause
//
// dtnmos is a C library for AMWA NMOS: it registers a program's senders and receivers
// with an NMOS registry (IS-04), lets a controller connect them (IS-05), finds registries
// on the network (DNS-SD), and reads and writes SDP files. This header has what all its
// headers share: results, errors, IDs and logging.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdbool.h>
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

// What a function that can fail returns. A result at DTNMOS_E or above is a failure;
// DtNmos_GetLastError() then says what went wrong.
typedef enum DtNmosResult
{
    DTNMOS_OK = 0,
    DTNMOS_E = 0x1000,
    DTNMOS_E_INVALID_ARGUMENT =
        DTNMOS_E + 1,                  // a parameter is null, empty or out of range
    DTNMOS_E_PARSE = DTNMOS_E + 2,     // an SDP or JSON document is malformed
    DTNMOS_E_NOT_FOUND = DTNMOS_E + 3, // the registry has no such resource
    DTNMOS_E_AMBIGUOUS = DTNMOS_E + 4, // a label names more than one resource
    DTNMOS_E_HTTP = DTNMOS_E + 5, // a request failed or was answered with an error status
    DTNMOS_E_TIMEOUT = DTNMOS_E + 6, // a request got no answer in time
    DTNMOS_E_STATE = DTNMOS_E + 7,   // the object is not in a state for the call, e.g.
                                     // a node that is not open
    DTNMOS_E_NO_MEMORY = DTNMOS_E + 8,
    DTNMOS_E_INTERNAL = DTNMOS_E + 9,
    DTNMOS_E_NETWORK = DTNMOS_E + 10, // a socket could not be opened, or could not send
    DTNMOS_E_BUFFER_TOO_SMALL = DTNMOS_E + 11 // a text does not fit the caller's buffer
} DtNmosResult;

// The ID of an NMOS resource (a node, device, sender, receiver, flow or source): a
// UUID in text form, lower case, e.g. "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01".
typedef struct DtNmosId
{
    char Text[37]; // The UUID and its null; empty for no ID
} DtNmosId;

// How important a log message is.
typedef enum DtNmosLogLevel
{
    DTNMOS_LOG_DEBUG = 0,
    DTNMOS_LOG_INFO = 1,
    DTNMOS_LOG_WARNING = 2,
    DTNMOS_LOG_ERROR = 3
} DtNmosLogLevel;

// A program's function that receives the library's log messages, with the User the
// program gave. Message is valid only during the call.
typedef void (*DtNmosLogFunc)(void* User, DtNmosLogLevel Level, const char* Message);

// Returns a message, in English, that says what failed in the last call on this thread
// that failed, and why; "" if none has. A call that succeeds does not change it. The
// text stays valid until the next failure on the thread.
DTNMOS_API const char* DtNmos_GetLastError(void);

// Sets the message DtNmos_GetLastError() returns on this thread, and returns Result.
//
// A callback of the program uses it to fail with a reason: it returns
// DtNmos_SetLastError(DTNMOS_E_..., "why"). The library passes the reason on; for an
// activation, the node sends it to the controller.
DTNMOS_API DtNmosResult DtNmos_SetLastError(DtNmosResult Result, const char* Message);

// Returns the version of the library the program runs with, which may differ from the
// version it was built against (DTNMOS_VERSION in dtnmos_version.h). Each pointer may be
// NULL.
DTNMOS_API void DtNmos_Version(int* Major, int* Minor, int* Patch);

// Makes an ID from a name, so that a resource gets the same ID every time the program
// runs. The same NamespaceId and Name always give the same *Id (a version 5 UUID, RFC
// 9562), e.g. from a node's ID and "device/<serial>:<port>".
//
// Returns DTNMOS_OK, or DTNMOS_E_INVALID_ARGUMENT when NamespaceId is not a UUID.
DTNMOS_API DtNmosResult DtNmosId_FromName(const DtNmosId* NamespaceId, const char* Name,
                                          DtNmosId* Id);

// Returns the name of a result, e.g. "DTNMOS_E_NOT_FOUND", for messages. Do not free the
// string.
DTNMOS_API const char* DtNmosResult_Name(DtNmosResult Result);

#ifdef __cplusplus
}
#endif
