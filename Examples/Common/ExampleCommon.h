// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleCommon.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - What the example programs share: the headers, arguments, files and a registry
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dtnmos.h"
#include "dtnmos_http.h"
#include "dtnmos_node.h"
#include "dtnmos_query.h"
#include "dtnmos_sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exit codes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

#define EXAMPLE_OK 0      // Did what was asked
#define EXAMPLE_FAILED 1  // A call failed, or the command line was wrong
#define EXAMPLE_NOTHING 2 // Ran, but found nothing, such as no registry

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Arguments +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Options are "--name value" or a lone "--name". A program lists the options it knows;
// anything else on the command line is an error.
//

typedef struct ExampleOption
{
    const char* Name; // such as "--registry"
    bool TakesValue;  // followed by a value
    const char* Help; // one line for the usage text
} ExampleOption;

// Checks that every argument is a known option, with a value where it takes one. Returns
// false after printing what is wrong when the command line is not valid, and after
// printing Usage and the options when it asks for --help, which every program knows.
bool Example_CheckArguments(int Argc, char** Argv, const char* Usage,
                            const ExampleOption* Options, int NumOptions);

// Returns whether the option Name, which takes no value, is on the command line.
bool Example_HasFlag(int Argc, char** Argv, const char* Name);

// Reads option Name as a decimal integer into *Value, which is left alone when the option
// is not given. Prints the problem and returns false for a value that is not an integer.
bool Example_Int64(int Argc, char** Argv, const char* Name, int64_t* Value);

// Returns the value of option Name, or NULL when it is not given.
const char* Example_Value(int Argc, char** Argv, const char* Name);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Prints "What: RESULT_NAME: the message of the failure" for a failed call and returns
// EXAMPLE_FAILED.
int Example_Failed(const char* What, DtNmosResult Result);

// Searches the network for a registry of Service with DNS-SD, and writes the base URL
// of the most preferred usable one into Url, of Size bytes. Returns EXAMPLE_OK,
// EXAMPLE_NOTHING when none was found, or EXAMPLE_FAILED after printing why.
int Example_FindRegistry(DtNmosService Service, char* Url, size_t Size);

// A DtNmosLogFunc that prints warnings and errors, and all other messages too when User
// is not NULL.
void Example_Log(void* User, DtNmosLogLevel Level, const char* Message);

// Reads the file at Path into a new buffer *Text, with a null at its end, for the caller
// to free, and sets *Length to its length. Returns false, after printing why, when it
// cannot.
bool Example_ReadFile(const char* Path, char** Text, size_t* Length);

// Sleeps for about Ms milliseconds.
void Example_SleepMs(int Ms);

#ifdef __cplusplus
}
#endif
