// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# platform.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - What the node needs of the operating system, behind names of dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

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

// An IPv4 datagram socket, for the queries of multicast DNS.
typedef struct dtnmos_udp dtnmos_udp;

// Opens a socket on a free port of bind_address, or of every address when it is null,
// whose multicast leaves through the interface of interface_address, or of the default
// route when it is null, with a hop limit of 255 and loopback. Returns null on failure.
dtnmos_udp* dtnmos_udp_open(const char* bind_address, const char* interface_address);

// Returns the port the socket is bound to.
uint16_t dtnmos_udp_port(const dtnmos_udp* udp);

// Sends length bytes of data to address and port; returns 0 on failure.
int dtnmos_udp_send(dtnmos_udp* udp, const char* address, uint16_t port, const void* data,
                    size_t length);

// Waits up to timeout_ms for a datagram and receives it into buffer; returns its length,
// 0 when none came in time, or -1 on failure. from_address, when not null, receives the
// address of the sender as text, and from_port its port.
long dtnmos_udp_receive(dtnmos_udp* udp, void* buffer, size_t size, uint32_t timeout_ms,
                        char* from_address, size_t from_size, uint16_t* from_port);

void dtnmos_udp_close(dtnmos_udp* udp);
