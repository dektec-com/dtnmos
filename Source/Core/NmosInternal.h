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
    #define DTNMOS_PRINTF(format_index, first_argument)                                  \
        __attribute__((format(printf, format_index, first_argument)))
#else
    #define DTNMOS_PRINTF(format_index, first_argument)
#endif

// Makes the message of format the last error of this thread, which DtNmos_GetLastError()
// returns, and returns code.
DtNmosResult dtnmos_fail(DtNmosResult code, const char* format, ...) DTNMOS_PRINTF(2, 3);

// Makes "Out of memory." the last error of this thread, and returns DTNMOS_E_NO_MEMORY.
DtNmosResult dtnmos_fail_memory(void);

// Checks the Size of a struct a caller gives, as rule 12 of CONTRIBUTING.md has it:
// size, the struct's Size; first, the size of its first version; current, its sizeof
// in this library; what, its name in the message. 0, a size below first and a size
// above current fail with DTNMOS_E_INVALID_ARGUMENT; a size from first up to current is
// a version whose later fields take their defaults.
DtNmosResult dtnmos_check_size(size_t size, size_t first, size_t current,
                               const char* what);
#define DTNMOS_CHECK_SIZE(pointer, type, first)                                          \
    dtnmos_check_size((pointer)->Size, (first), sizeof(type), #type)

// Empties the last error of this thread, before a callback whose message is passed on,
// so that a callback that fails without one does not pass on an older one.
void dtnmos_clear_error(void);

// A text that grows as it is appended to. A failed allocation sets failed and makes every
// later append do nothing, so that a writer checks once at the end.
typedef struct dtnmos_buffer
{
    char* data;
    size_t length;
    size_t capacity;
    int failed;
} dtnmos_buffer;

void dtnmos_buffer_append(dtnmos_buffer* buffer, const char* text, size_t length);
void dtnmos_buffer_printf(dtnmos_buffer* buffer, const char* format, ...)
    DTNMOS_PRINTF(2, 3);
void dtnmos_buffer_free(dtnmos_buffer* buffer);

// Appends a string literal, whose length the compiler knows.
#define DTNMOS_APPEND_LITERAL(buffer, literal)                                           \
    dtnmos_buffer_append((buffer), (literal), sizeof(literal) - 1)

// A piece of text that is not null terminated.
typedef struct dtnmos_span
{
    const char* data;
    size_t length;
} dtnmos_span;

dtnmos_span dtnmos_span_of(const char* text);
// Returns span without the spaces and tabs at both ends.
dtnmos_span dtnmos_span_trim(dtnmos_span span);
// Whether span is text, comparing ASCII letters without regard to case when fold is set.
int dtnmos_span_equals(dtnmos_span span, const char* text, int fold);
// Whether span starts with prefix, exactly.
int dtnmos_span_starts_with(dtnmos_span span, const char* prefix);
// Splits span at the first separator: head is what lies before it, and the return value
// what follows it, empty with a null data when there is no separator.
dtnmos_span dtnmos_span_split(dtnmos_span span, char separator, dtnmos_span* head);

// Reads span, all of it, as a decimal number of at most maximum; returns 0 on failure.
int dtnmos_parse_u64(dtnmos_span span, uint64_t maximum, uint64_t* value);
int dtnmos_parse_u32(dtnmos_span span, uint32_t maximum, uint32_t* value);
// Reads a rate, "25" or "30000/1001"; returns 0 on failure or a zero denominator.
int dtnmos_parse_rate(dtnmos_span span, uint32_t* numerator, uint32_t* denominator);
// Reads a time in milliseconds with up to six decimals, "1" or "0.125", as nanoseconds.
int dtnmos_parse_milliseconds(dtnmos_span span, uint32_t* nanoseconds);
// Reads a number in decimal or in hexadecimal after 0x, as in DID_SDID={0x61,0x02}.
int dtnmos_parse_byte(dtnmos_span span, uint8_t* value);

// Copies span into the array target of size bytes with its terminating null; returns 0,
// leaving target empty, when it does not fit.
int dtnmos_copy_span(char* target, size_t size, dtnmos_span span);

// Copies the length bytes of data and a null into the caller's buffer of *size bytes,
// and sets *size to length. When they do not fit, or buffer is null, it fails with
// DTNMOS_E_BUFFER_TOO_SMALL and sets *size to length + 1, the bytes it needs.
DtNmosResult dtnmos_copy_text(char* buffer, size_t* size, const char* data,
                              size_t length);

// The owner of the strings and arrays of a flow, or of anything else whose pointers
// stay valid until the owner is freed: each piece is allocated on its own and freed
// with the owner. A store set to zero is empty.
typedef struct dtnmos_store
{
    void** pieces;
    size_t count;
    size_t capacity;
} dtnmos_store;

// Returns a copy of the length bytes of data, followed by a null, owned by store; null
// when the memory ran out.
char* dtnmos_store_text(dtnmos_store* store, const char* data, size_t length);
// Returns a copy of the size bytes of data, owned by store; null when the memory ran
// out or size is 0.
void* dtnmos_store_copy(dtnmos_store* store, const void* data, size_t size);
// Frees what store owns and leaves it empty.
void dtnmos_store_free(dtnmos_store* store);

// SHA-1 (RFC 3174), for name-based UUIDs only.
typedef struct dtnmos_sha1
{
    uint32_t state[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} dtnmos_sha1;

void dtnmos_sha1_init(dtnmos_sha1* sha1);
void dtnmos_sha1_update(dtnmos_sha1* sha1, const void* data, size_t length);
void dtnmos_sha1_final(dtnmos_sha1* sha1, uint8_t digest[20]);
