// #*#*#*#*#*#*#*#*#*#*#*#*#*# NmosInternal.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Helpers all sources of dtnmos use: errors, text buffers, parsing, SHA-1
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dtnmos.h"

// Lets GCC and Clang check the arguments of a printf-like function against its format.
#if defined(__GNUC__) || defined(__clang__)
    #define DTNMOS_PRINTF(FormatIndex, FirstArgument)                                    \
        __attribute__((format(printf, FormatIndex, FirstArgument)))
#else
    #define DTNMOS_PRINTF(FormatIndex, FirstArgument)
#endif

// Gives a static variable one copy per thread.
#if defined(_MSC_VER)
    #define NMOS_THREAD_LOCAL __declspec(thread)
#else
    #define NMOS_THREAD_LOCAL _Thread_local
#endif

// Reports a failure: sets this thread's last error, which DtNmos_GetLastError() returns,
// to the formatted message, and returns Code.
DtNmosResult NmosError_Fail(DtNmosResult Code, const char* Format, ...)
    DTNMOS_PRINTF(2, 3);

// Reports that memory ran out: sets the last error to "Out of memory." and returns
// DTNMOS_E_NO_MEMORY.
DtNmosResult NmosError_FailMemory(void);

// Checks the Size field of a struct the caller passed (rule 12 of CONTRIBUTING.md).
//
// Size is the struct's Size field, First the size of the struct's first version, Current
// its size in this library, and What its name for the message. A Size from First up to
// Current is accepted: it is an older version, whose later fields take their defaults.
// 0, or a Size outside that range, fails with DTNMOS_E_INVALID_ARGUMENT.
DtNmosResult NmosError_CheckSize(size_t Size, size_t First, size_t Current,
                                 const char* What);
// Checks the Size of *Pointer, a struct of type Type, with NmosError_CheckSize().
#define DTNMOS_CHECK_SIZE(Pointer, Type, First)                                          \
    NmosError_CheckSize((Pointer)->Size, (First), sizeof(Type), #Type)

// Clears this thread's last error. Called before a program's callback, so that a callback
// that fails without setting a message does not pass on an old one.
void NmosError_Clear(void);

// A text that grows as text is appended. When an allocation fails, Failed is set and
// further appends do nothing, so the writer checks for failure once at the end.
typedef struct NmosBuffer
{
    char* Data;      // the text, null-terminated; null while empty
    size_t Length;   // the length of the text
    size_t Capacity; // the size of the allocation
    bool Failed;     // an allocation failed; the text is incomplete
} NmosBuffer;

// Appends Length bytes of Text.
void NmosBuffer_Append(NmosBuffer* Buffer, const char* Text, size_t Length);
// Appends formatted text, as printf does.
void NmosBuffer_Printf(NmosBuffer* Buffer, const char* Format, ...) DTNMOS_PRINTF(2, 3);
// Frees the text and empties the buffer.
void NmosBuffer_Free(NmosBuffer* Buffer);

// Appends a string literal; the compiler knows its length.
#define DTNMOS_APPEND_LITERAL(Buffer, Literal)                                           \
    NmosBuffer_Append((Buffer), (Literal), sizeof(Literal) - 1)

// A piece of a longer text, which is not null-terminated.
typedef struct NmosSpan
{
    const char* Data; // the first character; null for no text at all
    size_t Length;    // the number of characters
} NmosSpan;

// Returns a span of a null-terminated text. A null Text gives an empty span.
NmosSpan NmosSpan_Of(const char* Text);
// Returns Span without spaces and tabs at the start and the end.
NmosSpan NmosSpan_Trim(NmosSpan Span);
// Returns whether Span equals Text. With Fold, ASCII letters are compared ignoring case.
bool NmosSpan_Equals(NmosSpan Span, const char* Text, bool Fold);
// Returns whether Span starts with Prefix, case included.
bool NmosSpan_StartsWith(NmosSpan Span, const char* Prefix);
// Splits Span at the first Separator. *Head gets the part before it; the part after it is
// returned. Without a separator, *Head gets all of Span and the result is empty, with a
// null Data.
NmosSpan NmosSpan_Split(NmosSpan Span, char Separator, NmosSpan* Head);

// Reads a decimal number that is all of Span and at most Maximum. Returns false when it
// is not.
bool NmosText_ParseU64(NmosSpan Span, uint64_t Maximum, uint64_t* Value);
// Reads a decimal number as NmosText_ParseU64() does, into a uint32_t.
bool NmosText_ParseU32(NmosSpan Span, uint32_t Maximum, uint32_t* Value);
// Reads a rate, such as "25" or "30000/1001". Returns false when it is invalid or the
// denominator is 0.
bool NmosText_ParseRate(NmosSpan Span, uint32_t* Numerator, uint32_t* Denominator);
// Reads a time in milliseconds with up to six decimals, such as "1" or "0.125", and
// returns it in nanoseconds.
bool NmosText_ParseMilliseconds(NmosSpan Span, uint32_t* Nanoseconds);
// Reads a byte in decimal, or in hexadecimal after 0x, as in DID_SDID={0x61,0x02}.
bool NmosText_ParseByte(NmosSpan Span, uint8_t* Value);

// Copies Span into Target, a char array of Size bytes, and adds a null. Returns false,
// leaving Target empty, when it does not fit.
bool NmosText_CopySpan(char* Target, size_t Size, NmosSpan Span);

// Copies Length bytes of Data, and a null, into the caller's Buffer of *Size bytes, and
// sets *Size to Length. When they do not fit, or Buffer is null, fails with
// DTNMOS_E_BUFFER_TOO_SMALL and sets *Size to Length + 1, the size needed.
DtNmosResult NmosText_CopyText(char* Buffer, size_t* Size, const char* Data,
                               size_t Length);

// Owns memory that must stay valid as long as the owner, such as the strings and arrays
// of a copied flow. Each piece is a separate allocation; all are freed together. A store
// set to zero is empty.
typedef struct NmosStore
{
    void** Pieces;   // the allocations it owns
    size_t Count;    // the number of pieces
    size_t Capacity; // the size of the Pieces array
} NmosStore;

// Copies Length bytes of Data, plus a null, into memory owned by Store. Returns null
// when memory runs out.
char* NmosStore_Text(NmosStore* Store, const char* Data, size_t Length);
// Copies Size bytes of Data into memory owned by Store. Returns null when memory runs out
// or Size is 0.
void* NmosStore_Copy(NmosStore* Store, const void* Data, size_t Size);
// Frees everything Store owns, and leaves it empty.
void NmosStore_Free(NmosStore* Store);

// The state of a SHA-1 hash (RFC 3174). Used only for name-based UUIDs.
typedef struct NmosSha1
{
    uint32_t State[5]; // the hash so far
    uint64_t Length;   // the number of bytes hashed
    uint8_t Block[64]; // the bytes of the block not yet hashed
    size_t Used;       // the number of bytes in Block
} NmosSha1;

// Starts a new hash.
void NmosSha1_Init(NmosSha1* Sha1);
// Adds Length bytes of Data to the hash.
void NmosSha1_Update(NmosSha1* Sha1, const void* Data, size_t Length);
// Finishes the hash and writes its 20 bytes into Digest.
void NmosSha1_Final(NmosSha1* Sha1, uint8_t Digest[20]);
