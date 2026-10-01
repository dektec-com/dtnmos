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
#define DTNMOS_VERSION_PATCH 0

// Returns the version of the library the program runs with; each pointer may be null.
DTNMOS_API void dtnmos_version(int* major, int* minor, int* patch);

typedef enum dtnmos_result
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
} dtnmos_result;

// Returns the name of a result, e.g. "DTNMOS_E_NOT_FOUND"; a static string.
DTNMOS_API const char* dtnmos_result_name(dtnmos_result result);

// A failure: its code, and a message in English that names what failed and why. The
// message is a fixed array, so that reporting a failure never allocates.
typedef struct dtnmos_error
{
    dtnmos_result code;
    char message[512];
} dtnmos_error;

// A string the library hands out. A dtnmos_string set to zero is valid and empty, so a
// struct that holds strings needs nothing but = {0} before the library fills it. Short
// strings live in the struct itself and longer ones on the heap; the members are
// private, the functions below reach them.
//
// A struct that holds strings has a _clear() that frees them and a _copy() that copies
// them. Copying such a struct with = shares its heap strings, so that clearing both
// frees them twice: use its _copy().
typedef struct dtnmos_string
{
    size_t length;
    char* heap;
    char local[24];
} dtnmos_string;

// Returns the text of string, ending in a null character; never null, "" when string is
// empty or null. Valid until the string is changed or cleared.
DTNMOS_API const char* dtnmos_string_get(const dtnmos_string* string);

// Returns the length of string in bytes, without the null character; 0 when string is
// null.
DTNMOS_API size_t dtnmos_string_length(const dtnmos_string* string);

// Makes string the length bytes of text, which need not end in a null character and may
// lie in string itself. Fails with DTNMOS_E_NO_MEMORY, leaving string as it was.
DTNMOS_API dtnmos_result dtnmos_string_set(dtnmos_string* string, const char* text,
                                           size_t length);

// Makes string the text up to the null character of text; null makes it empty.
DTNMOS_API dtnmos_result dtnmos_string_set_text(dtnmos_string* string, const char* text);

// Makes target a copy of source.
DTNMOS_API dtnmos_result dtnmos_string_copy(dtnmos_string* target,
                                            const dtnmos_string* source);

// Frees what string holds and leaves it empty.
DTNMOS_API void dtnmos_string_clear(dtnmos_string* string);

// An NMOS resource ID: a UUID in its text form, lower case, e.g.
// "5f38f7a2-1d91-5e0c-8a2b-6e1c2f7d9a01".
typedef struct dtnmos_id
{
    char text[37];
} dtnmos_id;

// Writes the name-based UUID (version 5, RFC 9562) of name in the namespace
// namespace_id, so that a node, a device, a sender or a receiver keeps its ID across
// restarts, e.g. from the serial number of a card, its port and the label of an
// element. Fails with DTNMOS_E_INVALID_ARGUMENT when namespace_id is no UUID.
DTNMOS_API dtnmos_result dtnmos_id_from_name(const dtnmos_id* namespace_id,
                                             const char* name, dtnmos_id* id,
                                             dtnmos_error* error);

// Where log messages go.
typedef enum dtnmos_log_level
{
    DTNMOS_LOG_DEBUG = 0,
    DTNMOS_LOG_INFO = 1,
    DTNMOS_LOG_WARNING = 2,
    DTNMOS_LOG_ERROR = 3
} dtnmos_log_level;

typedef void (*dtnmos_log_fn)(void* user, dtnmos_log_level level, const char* message);

#ifdef __cplusplus
}
#endif
