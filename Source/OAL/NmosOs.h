// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosOs.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - What the node needs of the operating system, behind names of dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

typedef struct NmosMutex NmosMutex;

// Returns a new mutex, or null when out of memory.
NmosMutex* NmosOs_MutexCreate(void);
void NmosOs_MutexFree(NmosMutex* Mutex);
void NmosOs_MutexLock(NmosMutex* Mutex);
void NmosOs_MutexUnlock(NmosMutex* Mutex);

typedef struct NmosThread NmosThread;

// Starts a thread that runs function(argument); returns null when it cannot.
NmosThread* NmosOs_ThreadStart(void (*Function)(void*), void* Argument);

// Waits for the thread to end and frees it.
void NmosOs_ThreadJoin(NmosThread* Thread);

void NmosOs_SleepMs(uint32_t Milliseconds);

// Milliseconds of a clock that only runs forward.
uint64_t NmosOs_MonotonicMs(void);

// Writes the time now as an IS-04 version, "<seconds>:<nanoseconds>" of TAI, into text,
// which holds at least 32 characters. last holds the previous version in nanoseconds,
// which the new one exceeds even within one tick of the clock; its owner guards it.
void NmosOs_VersionNow(uint64_t* Last, char* Text, size_t Size);

// Writes the address of this host that reaches host into address, as text; returns 0 when
// host cannot be resolved or reached.
int NmosOs_AddressToward(const char* Host, char* Address, size_t Size);

// An address of a network interface of the host, which is up: a port of a card of
// DekTec among them, which the operating system has as a network interface too.
typedef struct NmosInterface
{
    char Name[64];    // as the operating system names it, e.g. "eth0" or "Ethernet 2"
    char PortId[18];  // its MAC address, as IS-04 writes it: "00-14-f4-00-00-01"
    char Address[64]; // one address of IPv4 or IPv6 of the interface
} NmosInterface;

// Returns the addresses of the network interfaces of the host, one entry for each, and
// sets *Count to their number; returns null, *Count 0, when there are none or the
// memory ran out. The caller frees the array. An interface without a MAC address, such
// as loopback, has "00-00-00-00-00-00".
NmosInterface* NmosOs_Interfaces(size_t* Count);

// Returns a TCP port that is free on the address host now, or 0 when there is none.
uint16_t NmosOs_FreePort(const char* Host);

// An IPv4 datagram socket, for the queries of multicast DNS.
typedef struct NmosUdp NmosUdp;

// Opens a socket on a free port of bind_address, or of every address when it is null,
// whose multicast leaves through the interface of interface_address, or of the default
// route when it is null, with a hop limit of 255 and loopback. Returns null on failure.
NmosUdp* NmosOs_UdpOpen(const char* BindAddress, const char* InterfaceAddress);

// Returns the port the socket is bound to.
uint16_t NmosOs_UdpPort(const NmosUdp* Udp);

// Sends length bytes of data to address and port; returns 0 on failure.
int NmosOs_UdpSend(NmosUdp* Udp, const char* Address, uint16_t Port, const void* Data,
                   size_t Length);

// Waits up to timeout_ms for a datagram and receives it into buffer; returns its length,
// 0 when none came in time, or -1 on failure. from_address, when not null, receives the
// address of the sender as text, and from_port its port.
int NmosOs_UdpReceive(NmosUdp* Udp, void* Buffer, size_t Size, uint32_t TimeoutMs,
                      char* FromAddress, size_t FromSize, uint16_t* FromPort);

void NmosOs_UdpClose(NmosUdp* Udp);

// Writes the first IPv4 DNS server of the host into server, and the domain it searches
// into domain, as DHCP or the administrator gave them: of /etc/resolv.conf on POSIX, and
// of the first adapter with a gateway on Windows. Each is left empty when the host has
// none.
void NmosOs_SystemDns(char* Server, size_t ServerSize, char* Domain, size_t DomainSize);
