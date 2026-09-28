// SPDX-License-Identifier: BSD-3-Clause
//
// What the node needs of the operating system, behind names of dtnmos: a mutex, a thread,
// sleeping, the time, and the address of this host on the way to another. Windows and
// POSIX.

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct dtnmos_mutex dtnmos_mutex;

// Returns a new mutex, or null when out of memory.
dtnmos_mutex* dtnmos_mutex_create(void);
void dtnmos_mutex_free(dtnmos_mutex* mutex);
void dtnmos_mutex_lock(dtnmos_mutex* mutex);
void dtnmos_mutex_unlock(dtnmos_mutex* mutex);

typedef struct dtnmos_thread dtnmos_thread;

// Starts a thread that runs function(argument); returns null when it cannot.
dtnmos_thread* dtnmos_thread_start(void (*function)(void*), void* argument);

// Waits for the thread to end and frees it.
void dtnmos_thread_join(dtnmos_thread* thread);

void dtnmos_sleep_ms(uint32_t milliseconds);

// Milliseconds of a clock that only runs forward.
uint64_t dtnmos_monotonic_ms(void);

// Writes the time now as an IS-04 version, "<seconds>:<nanoseconds>" of TAI, into text,
// which holds at least 32 characters. last holds the previous version in nanoseconds,
// which the new one exceeds even within one tick of the clock; its owner guards it.
void dtnmos_version_now(uint64_t* last, char* text, size_t size);

// Writes the address of this host that reaches host into address, as text; returns 0 when
// host cannot be resolved or reached.
int dtnmos_address_toward(const char* host, char* address, size_t size);

// Returns a TCP port that is free on the address host now, or 0 when there is none.
uint16_t dtnmos_free_port(const char* host);
