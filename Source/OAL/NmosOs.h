// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosOs.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The operating system services dtnmos uses: locks, threads, clocks and sockets
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A lock that one thread at a time holds.
typedef struct NmosMutex NmosMutex;

// Creates a mutex. Returns null when out of memory.
NmosMutex* NmosOs_MutexCreate(void);
// Frees a mutex. Nobody may hold it.
void NmosOs_MutexFree(NmosMutex* Mutex);
// Takes the lock, waiting until no other thread holds it.
void NmosOs_MutexLock(NmosMutex* Mutex);
// Releases the lock.
void NmosOs_MutexUnlock(NmosMutex* Mutex);

// A condition that threads wait for while they hold a mutex.
typedef struct NmosCondition NmosCondition;

// Creates a condition. Returns null when out of memory.
NmosCondition* NmosOs_ConditionCreate(void);
// Frees a condition. Nobody may wait for it.
void NmosOs_ConditionFree(NmosCondition* Condition);
// Releases Mutex, which the caller holds, waits until the condition is signalled, and
// takes Mutex again. It may also return without a signal, so the caller checks again
// what it waits for.
void NmosOs_ConditionWait(NmosCondition* Condition, NmosMutex* Mutex);
// Wakes every thread that waits for the condition.
void NmosOs_ConditionWakeAll(NmosCondition* Condition);

typedef struct NmosThread NmosThread;

// Starts a thread that runs Function(Argument). Returns null when it cannot.
NmosThread* NmosOs_ThreadStart(void (*Function)(void*), void* Argument);

// Waits until the thread has ended, and frees it.
void NmosOs_ThreadJoin(NmosThread* Thread);

// Sleeps for the given number of milliseconds.
void NmosOs_SleepMs(uint32_t Milliseconds);

// Returns a time in milliseconds, for measuring intervals. It never goes back, even when
// the clock of the computer is set.
uint64_t NmosOs_MonotonicMs(void);

// Returns the current time in TAI nanoseconds since the PTP epoch (1970), the time that
// IS-04 and IS-05 use. It is the system clock plus a fixed TAI offset.
uint64_t NmosOs_TaiNowNs(void);

// Makes a new IS-04 version stamp, "<seconds>:<nanoseconds>" of TAI time, and writes it
// into Text, which holds at least 32 characters. *Last is the previous stamp in
// nanoseconds; the new stamp is always later, even when the clock has not moved. The
// caller guards *Last against other threads.
void NmosOs_VersionNow(uint64_t* Last, char* Text, size_t Size);

// Finds the address of this computer that is used to reach Host, and writes it as text
// into Address. Returns false when Host cannot be resolved or reached.
bool NmosOs_AddressToward(const char* Host, char* Address, size_t Size);

// An address of a network interface of this computer that is up. A DekTec card's network
// port is among them: the operating system sees it as a network interface too.
typedef struct NmosInterface
{
    char Name[64];    // its name in the operating system, e.g. "eth0" or "Ethernet 2"
    char PortId[18];  // its MAC address as IS-04 writes it: "00-14-f4-00-00-01"
    char Address[64]; // one IPv4 or IPv6 address of the interface
} NmosInterface;

// Lists the addresses of this computer's network interfaces, one entry per address, and
// sets *Count to the number. Returns null and *Count 0 when there are none or memory runs
// out. The caller frees the array. An interface without a MAC address, such as
// loopback, has "00-00-00-00-00-00".
NmosInterface* NmosOs_Interfaces(size_t* Count);

// Returns a TCP port that is free on the address Host right now, or 0 when there is none.
uint16_t NmosOs_FreePort(const char* Host);

// An IPv4 UDP socket, for multicast DNS queries.
typedef struct NmosUdp NmosUdp;

// Opens a UDP socket on a free port. Returns null on failure.
//
// BindAddress is the local address to bind to; null binds to every address.
// InterfaceAddress selects the interface multicast is sent from; null uses the default
// route. Multicast is sent with a hop limit of 255, and loops back to this computer.
NmosUdp* NmosOs_UdpOpen(const char* BindAddress, const char* InterfaceAddress);

// Returns the local port of the socket.
uint16_t NmosOs_UdpPort(const NmosUdp* Udp);

// Sends Length bytes to Address and Port. Returns false on failure.
bool NmosOs_UdpSend(NmosUdp* Udp, const char* Address, uint16_t Port, const void* Data,
                    size_t Length);

// Waits up to TimeoutMs for a datagram and copies it into Buffer. Returns its length, 0
// when none came in time, or -1 on failure. FromAddress, when not null, gets the
// sender's address as text, and *FromPort, when not null, its port.
int NmosOs_UdpReceive(NmosUdp* Udp, void* Buffer, size_t Size, uint32_t TimeoutMs,
                      char* FromAddress, size_t FromSize, uint16_t* FromPort);

// Closes the socket.
void NmosOs_UdpClose(NmosUdp* Udp);

// Finds this computer's DNS server and search domain, as DHCP or the administrator set
// them. Server gets the first IPv4 DNS server, and Domain the domain searched. On POSIX
// they come from /etc/resolv.conf; on Windows from the first adapter with a gateway.
// Each is left empty when there is none.
void NmosOs_SystemDns(char* Server, size_t ServerSize, char* Domain, size_t DomainSize);
