// #*#*#*#*#*#*#*#*#*#*#*#*#*# NmosInternal.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - What the sources of dtnmos share and do not export
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "dtnmos.h"

#if defined(__GNUC__) || defined(__clang__)
    #define DTNMOS_PRINTF(FormatIndex, FirstArgument)                                    \
        __attribute__((format(printf, FormatIndex, FirstArgument)))
#else
    #define DTNMOS_PRINTF(FormatIndex, FirstArgument)
#endif

// Makes the message of format the last error of this thread, which DtNmos_GetLastError()
// returns, and returns code.
DtNmosResult NmosError_Fail(DtNmosResult Code, const char* Format, ...)
    DTNMOS_PRINTF(2, 3);

// Makes "Out of memory." the last error of this thread, and returns DTNMOS_E_NO_MEMORY.
DtNmosResult NmosError_FailMemory(void);

// Checks the Size of a struct a caller gives, as rule 12 of CONTRIBUTING.md has it:
// size, the struct's Size; first, the size of its first version; current, its sizeof
// in this library; what, its name in the message. 0, a size below first and a size
// above current fail with DTNMOS_E_INVALID_ARGUMENT; a size from first up to current is
// a version whose later fields take their defaults.
DtNmosResult NmosError_CheckSize(size_t Size, size_t First, size_t Current,
                                 const char* What);
#define DTNMOS_CHECK_SIZE(Pointer, Type, First)                                          \
    NmosError_CheckSize((Pointer)->Size, (First), sizeof(Type), #Type)

// Empties the last error of this thread, before a callback whose message is passed on,
// so that a callback that fails without one does not pass on an older one.
void NmosError_Clear(void);

// A text that grows as it is appended to. A failed allocation sets failed and makes every
// later append do nothing, so that a writer checks once at the end.
typedef struct NmosBuffer
{
    char* Data;
    size_t Length;
    size_t Capacity;
    int Failed;
} NmosBuffer;

void NmosBuffer_Append(NmosBuffer* Buffer, const char* Text, size_t Length);
void NmosBuffer_Printf(NmosBuffer* Buffer, const char* Format, ...) DTNMOS_PRINTF(2, 3);
void NmosBuffer_Free(NmosBuffer* Buffer);

// Appends a string literal, whose length the compiler knows.
#define DTNMOS_APPEND_LITERAL(Buffer, Literal)                                           \
    NmosBuffer_Append((Buffer), (Literal), sizeof(Literal) - 1)

// A piece of text that is not null terminated.
typedef struct NmosSpan
{
    const char* Data;
    size_t Length;
} NmosSpan;

NmosSpan NmosSpan_Of(const char* Text);
// Returns span without the spaces and tabs at both ends.
NmosSpan NmosSpan_Trim(NmosSpan Span);
// Whether span is text, comparing ASCII letters without regard to case when fold is set.
int NmosSpan_Equals(NmosSpan Span, const char* Text, int Fold);
// Whether span starts with prefix, exactly.
int NmosSpan_StartsWith(NmosSpan Span, const char* Prefix);
// Splits span at the first separator: head is what lies before it, and the return value
// what follows it, empty with a null data when there is no separator.
NmosSpan NmosSpan_Split(NmosSpan Span, char Separator, NmosSpan* Head);

// Reads span, all of it, as a decimal number of at most maximum; returns 0 on failure.
int NmosText_ParseU64(NmosSpan Span, uint64_t Maximum, uint64_t* Value);
int NmosText_ParseU32(NmosSpan Span, uint32_t Maximum, uint32_t* Value);
// Reads a rate, "25" or "30000/1001"; returns 0 on failure or a zero denominator.
int NmosText_ParseRate(NmosSpan Span, uint32_t* Numerator, uint32_t* Denominator);
// Reads a time in milliseconds with up to six decimals, "1" or "0.125", as nanoseconds.
int NmosText_ParseMilliseconds(NmosSpan Span, uint32_t* Nanoseconds);
// Reads a number in decimal or in hexadecimal after 0x, as in DID_SDID={0x61,0x02}.
int NmosText_ParseByte(NmosSpan Span, uint8_t* Value);

// Copies span into the array target of size bytes with its terminating null; returns 0,
// leaving target empty, when it does not fit.
int NmosText_CopySpan(char* Target, size_t Size, NmosSpan Span);

// Copies the length bytes of data and a null into the caller's buffer of *Size bytes,
// and sets *Size to length. When they do not fit, or buffer is null, it fails with
// DTNMOS_E_BUFFER_TOO_SMALL and sets *Size to length + 1, the bytes it needs.
DtNmosResult NmosText_CopyText(char* Buffer, size_t* Size, const char* Data,
                               size_t Length);

// The owner of the strings and arrays of a flow, or of anything else whose pointers
// stay valid until the owner is freed: each piece is allocated on its own and freed
// with the owner. A store set to zero is empty.
typedef struct NmosStore
{
    void** Pieces;
    size_t Count;
    size_t Capacity;
} NmosStore;

// Returns a copy of the length bytes of data, followed by a null, owned by store; null
// when the memory ran out.
char* NmosStore_Text(NmosStore* Store, const char* Data, size_t Length);
// Returns a copy of the size bytes of data, owned by store; null when the memory ran
// out or size is 0.
void* NmosStore_Copy(NmosStore* Store, const void* Data, size_t Size);
// Frees what store owns and leaves it empty.
void NmosStore_Free(NmosStore* Store);

// SHA-1 (RFC 3174), for name-based UUIDs only.
typedef struct NmosSha1
{
    uint32_t State[5];
    uint64_t Length;
    uint8_t Block[64];
    size_t Used;
} NmosSha1;

void NmosSha1_Init(NmosSha1* Sha1);
void NmosSha1_Update(NmosSha1* Sha1, const void* Data, size_t Length);
void NmosSha1_Final(NmosSha1* Sha1, uint8_t Digest[20]);
