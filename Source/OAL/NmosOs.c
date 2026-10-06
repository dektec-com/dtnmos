// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosOs.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Mutex, thread, time, addresses and sockets of NmosOs.h, on Windows and POSIX
//
// SPDX-License-Identifier: BSD-3-Clause

// Strict C11 hides the functions of POSIX that this file needs.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
    #define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // After winsock2.h, which it needs.
    #include <iphlpapi.h>
    // Of mstcpip.h, which is not included, as it needs a particular order of the headers.
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
#else
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #if defined(__linux__)
        #include <netpacket/packet.h>
    #endif
    #include <pthread.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <time.h>
    #include <unistd.h>
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "NmosDns.h"
#include "NmosOs.h"

// TAI runs ahead of UTC by the leap seconds, 37 since 2017.
#define DTNMOS_TAI_OFFSET 37

#if defined(_WIN32)

struct NmosMutex
{
    SRWLOCK Lock;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosMutex* NmosOs_MutexCreate(void)
{
    NmosMutex* Mutex = malloc(sizeof(*Mutex));
    if (Mutex != NULL)
    {
        InitializeSRWLock(&Mutex->Lock);
    }
    return Mutex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_MutexFree(NmosMutex* Mutex)
{
    free(Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexLock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_MutexLock(NmosMutex* Mutex)
{
    AcquireSRWLockExclusive(&Mutex->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexUnlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_MutexUnlock(NmosMutex* Mutex)
{
    ReleaseSRWLockExclusive(&Mutex->Lock);
}

struct NmosCondition
{
    CONDITION_VARIABLE Variable;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosCondition* NmosOs_ConditionCreate(void)
{
    NmosCondition* Condition = malloc(sizeof(*Condition));
    if (Condition != NULL)
    {
        InitializeConditionVariable(&Condition->Variable);
    }
    return Condition;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_ConditionFree(NmosCondition* Condition)
{
    free(Condition);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_ConditionWait(NmosCondition* Condition, NmosMutex* Mutex)
{
    SleepConditionVariableSRW(&Condition->Variable, &Mutex->Lock, INFINITE, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionWakeAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosOs_ConditionWakeAll(NmosCondition* Condition)
{
    WakeAllConditionVariable(&Condition->Variable);
}

struct NmosThread
{
    HANDLE Handle;
    void (*Function)(void*);
    void* Argument;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DWORD WINAPI RunThread(LPVOID Parameter)
{
    NmosThread* Thread = Parameter;
    Thread->Function(Thread->Argument);
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ThreadStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosThread* NmosOs_ThreadStart(void (*Function)(void*), void* Argument)
{
    NmosThread* Thread = malloc(sizeof(*Thread));
    if (Thread == NULL)
    {
        return NULL;
    }
    Thread->Function = Function;
    Thread->Argument = Argument;
    Thread->Handle = CreateThread(NULL, 0, RunThread, Thread, 0, NULL);
    if (Thread->Handle == NULL)
    {
        free(Thread);
        return NULL;
    }
    return Thread;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ThreadJoin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosOs_ThreadJoin(NmosThread* Thread)
{
    if (Thread == NULL)
    {
        return;
    }
    WaitForSingleObject(Thread->Handle, INFINITE);
    CloseHandle(Thread->Handle);
    free(Thread);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_SleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_SleepMs(uint32_t Milliseconds)
{
    Sleep(Milliseconds);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MonotonicMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint64_t NmosOs_MonotonicMs(void)
{
    return (uint64_t)GetTickCount64();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartSockets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Winsock is started once per process and left running, as the process ends it.
//
static bool StartSockets(void)
{
    static volatile LONG Started = 0;
    if (InterlockedCompareExchange(&Started, 1, 0) == 0)
    {
        WSADATA Data;
        if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
        {
            Started = 0;
            return false;
        }
    }
    return true;
}

    #define DTNMOS_CLOSE_SOCKET closesocket
typedef SOCKET NmosSocket;
    // The length of a buffer, as the sockets of the platform take it.
    #define DTNMOS_SOCKET_LENGTH(length) ((int)(length))
    #define DTNMOS_NO_SOCKET INVALID_SOCKET

#else

struct NmosMutex
{
    pthread_mutex_t Lock;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosMutex* NmosOs_MutexCreate(void)
{
    NmosMutex* Mutex = malloc(sizeof(*Mutex));
    if (Mutex != NULL && pthread_mutex_init(&Mutex->Lock, NULL) != 0)
    {
        free(Mutex);
        return NULL;
    }
    return Mutex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_MutexFree(NmosMutex* Mutex)
{
    if (Mutex != NULL)
    {
        pthread_mutex_destroy(&Mutex->Lock);
        free(Mutex);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexLock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_MutexLock(NmosMutex* Mutex)
{
    pthread_mutex_lock(&Mutex->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MutexUnlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_MutexUnlock(NmosMutex* Mutex)
{
    pthread_mutex_unlock(&Mutex->Lock);
}

struct NmosCondition
{
    pthread_cond_t Variable;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosCondition* NmosOs_ConditionCreate(void)
{
    NmosCondition* Condition = malloc(sizeof(*Condition));
    if (Condition != NULL && pthread_cond_init(&Condition->Variable, NULL) != 0)
    {
        free(Condition);
        return NULL;
    }
    return Condition;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_ConditionFree(NmosCondition* Condition)
{
    if (Condition != NULL)
    {
        pthread_cond_destroy(&Condition->Variable);
        free(Condition);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_ConditionWait(NmosCondition* Condition, NmosMutex* Mutex)
{
    pthread_cond_wait(&Condition->Variable, &Mutex->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ConditionWakeAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosOs_ConditionWakeAll(NmosCondition* Condition)
{
    pthread_cond_broadcast(&Condition->Variable);
}

struct NmosThread
{
    pthread_t Handle;
    void (*Function)(void*);
    void* Argument;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* RunThread(void* Parameter)
{
    NmosThread* Thread = Parameter;
    Thread->Function(Thread->Argument);
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ThreadStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosThread* NmosOs_ThreadStart(void (*Function)(void*), void* Argument)
{
    NmosThread* Thread = malloc(sizeof(*Thread));
    if (Thread == NULL)
    {
        return NULL;
    }
    Thread->Function = Function;
    Thread->Argument = Argument;
    if (pthread_create(&Thread->Handle, NULL, RunThread, Thread) != 0)
    {
        free(Thread);
        return NULL;
    }
    return Thread;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_ThreadJoin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosOs_ThreadJoin(NmosThread* Thread)
{
    if (Thread == NULL)
    {
        return;
    }
    pthread_join(Thread->Handle, NULL);
    free(Thread);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_SleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_SleepMs(uint32_t Milliseconds)
{
    struct timespec Duration;
    Duration.tv_sec = (time_t)(Milliseconds / 1000);
    Duration.tv_nsec = (long)(Milliseconds % 1000) * 1000000L;
    while (nanosleep(&Duration, &Duration) != 0)
    {
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_MonotonicMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint64_t NmosOs_MonotonicMs(void)
{
    struct timespec Now;
    clock_gettime(CLOCK_MONOTONIC, &Now);
    return (uint64_t)Now.tv_sec * 1000u + (uint64_t)Now.tv_nsec / 1000000u;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartSockets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool StartSockets(void)
{
    return true;
}

    #define DTNMOS_CLOSE_SOCKET close
typedef int NmosSocket;
    #define DTNMOS_SOCKET_LENGTH(length) (length)
    #define DTNMOS_NO_SOCKET (-1)

#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_TaiNowNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint64_t NmosOs_TaiNowNs(void)
{
    struct timespec Now;
    timespec_get(&Now, TIME_UTC);
    return ((uint64_t)Now.tv_sec + DTNMOS_TAI_OFFSET) * 1000000000u +
           (uint64_t)Now.tv_nsec;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_VersionNow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosOs_VersionNow(uint64_t* Last, char* Text, size_t Size)
{
    uint64_t Nanoseconds = NmosOs_TaiNowNs();
    // Versions must rise even when two changes fall within one tick of the clock.
    if (Nanoseconds <= *Last)
    {
        Nanoseconds = *Last + 1;
    }
    *Last = Nanoseconds;
    snprintf(Text, Size, "%llu:%llu", (unsigned long long)(Nanoseconds / 1000000000u),
             (unsigned long long)(Nanoseconds % 1000000000u));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_AddressToward -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosOs_AddressToward(const char* Host, char* Address, size_t Size)
{
    if (Host == NULL || !StartSockets())
    {
        return false;
    }
    struct addrinfo Hints;
    memset(&Hints, 0, sizeof(Hints));
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* Found = NULL;
    if (getaddrinfo(Host, "9", &Hints, &Found) != 0 || Found == NULL)
    {
        return false;
    }
    bool Result = false;
    // Connecting a datagram socket sends nothing; it only chooses the route and so the
    // address of this host on it.
    const NmosSocket Probe = socket(Found->ai_family, SOCK_DGRAM, 0);
    if (Probe != DTNMOS_NO_SOCKET)
    {
        struct sockaddr_storage Local;
        socklen_t Length = sizeof(Local);
        if (connect(Probe, Found->ai_addr, (socklen_t)Found->ai_addrlen) == 0 &&
            getsockname(Probe, (struct sockaddr*)&Local, &Length) == 0)
        {
            const void* Bytes =
                Local.ss_family == AF_INET6
                    ? (const void*)&((struct sockaddr_in6*)&Local)->sin6_addr
                    : (const void*)&((struct sockaddr_in*)&Local)->sin_addr;
            Result = inet_ntop(Local.ss_family, Bytes, Address, (socklen_t)Size) != NULL;
        }
        DTNMOS_CLOSE_SOCKET(Probe);
    }
    freeaddrinfo(Found);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_FreePort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint16_t NmosOs_FreePort(const char* Host)
{
    if (Host == NULL || !StartSockets())
    {
        return 0;
    }
    struct addrinfo Hints;
    memset(&Hints, 0, sizeof(Hints));
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_flags = AI_PASSIVE;
    struct addrinfo* Found = NULL;
    if (getaddrinfo(Host, "0", &Hints, &Found) != 0 || Found == NULL)
    {
        return 0;
    }
    uint16_t Port = 0;
    const NmosSocket Probe = socket(Found->ai_family, SOCK_STREAM, 0);
    if (Probe != DTNMOS_NO_SOCKET)
    {
        struct sockaddr_storage Local;
        socklen_t Length = sizeof(Local);
        if (bind(Probe, Found->ai_addr, (socklen_t)Found->ai_addrlen) == 0 &&
            getsockname(Probe, (struct sockaddr*)&Local, &Length) == 0)
        {
            Port = ntohs(Local.ss_family == AF_INET6
                             ? ((struct sockaddr_in6*)&Local)->sin6_port
                             : ((struct sockaddr_in*)&Local)->sin_port);
        }
        DTNMOS_CLOSE_SOCKET(Probe);
    }
    freeaddrinfo(Found);
    return Port;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddressText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes an address of IPv4 or IPv6 into text; returns false for null or another family.
//
static bool AddressText(const struct sockaddr* Address, char* Text, size_t Size)
{
    if (Address != NULL && Address->sa_family == AF_INET)
    {
        return inet_ntop(AF_INET, &((const struct sockaddr_in*)Address)->sin_addr, Text,
                         (socklen_t)Size) != NULL;
    }
    if (Address != NULL && Address->sa_family == AF_INET6)
    {
        return inet_ntop(AF_INET6, &((const struct sockaddr_in6*)Address)->sin6_addr,
                         Text, (socklen_t)Size) != NULL;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteMac -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the MAC address Mac, of Length bytes, into PortId as IS-04 writes it; an address
// of another length than 6 bytes is written as all zero.
//
static void WriteMac(const unsigned char* Mac, size_t Length, char* PortId, size_t Size)
{
    static const unsigned char Zero[6] = {0};
    const unsigned char* m = Mac != NULL && Length == 6 ? Mac : Zero;
    snprintf(PortId, Size, "%02x-%02x-%02x-%02x-%02x-%02x", m[0], m[1], m[2], m[3], m[4],
             m[5]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Appends an empty entry to *List, of *Count entries with room for *Capacity, and
// returns it; returns null when the memory ran out.
//
static NmosInterface* AddInterface(NmosInterface** List, size_t* Count, size_t* Capacity)
{
    if (*Count == *Capacity)
    {
        const size_t Grown = *Capacity == 0 ? 8 : *Capacity * 2;
        NmosInterface* More = realloc(*List, Grown * sizeof(**List));
        if (More == NULL)
        {
            return NULL;
        }
        *List = More;
        *Capacity = Grown;
    }
    NmosInterface* Added = &(*List)[(*Count)++];
    memset(Added, 0, sizeof(*Added));
    return Added;
}

#if defined(_WIN32)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetAdapters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the adapters of the host as GetAdaptersAddresses() gives them with Flags,
// which the caller frees, or null when it fails.
//
static IP_ADAPTER_ADDRESSES* GetAdapters(ULONG Flags)
{
    ULONG Size = 16 * 1024;
    IP_ADAPTER_ADDRESSES* Adapters = NULL;
    ULONG Status = ERROR_BUFFER_OVERFLOW;
    for (int Attempt = 0; Attempt < 3 && Status == ERROR_BUFFER_OVERFLOW; ++Attempt)
    {
        free(Adapters);
        Adapters = malloc(Size);
        if (Adapters == NULL)
        {
            return NULL;
        }
        Status = GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, Adapters, &Size);
    }
    if (Status != NO_ERROR)
    {
        free(Adapters);
        return NULL;
    }
    return Adapters;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_Interfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosInterface* NmosOs_Interfaces(size_t* Count)
{
    *Count = 0;
    NmosInterface* List = NULL;
    size_t Capacity = 0;
    bool Failed = false;
    IP_ADAPTER_ADDRESSES* Adapters = GetAdapters(
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER);
    for (const IP_ADAPTER_ADDRESSES* Adapter = Adapters; Adapter != NULL && !Failed;
         Adapter = Adapter->Next)
    {
        if (Adapter->OperStatus != IfOperStatusUp)
        {
            continue;
        }
        for (const IP_ADAPTER_UNICAST_ADDRESS* Unicast = Adapter->FirstUnicastAddress;
             Unicast != NULL && !Failed; Unicast = Unicast->Next)
        {
            char Address[64];
            if (!AddressText(Unicast->Address.lpSockaddr, Address, sizeof(Address)))
            {
                continue;
            }
            NmosInterface* Added = AddInterface(&List, Count, &Capacity);
            Failed = Added == NULL;
            if (Added == NULL)
            {
                break;
            }
            if (WideCharToMultiByte(CP_UTF8, 0, Adapter->FriendlyName, -1, Added->Name,
                                    (int)sizeof(Added->Name), NULL, NULL) == 0)
            {
                snprintf(Added->Name, sizeof(Added->Name), "%s", Adapter->AdapterName);
            }
            WriteMac(Adapter->PhysicalAddress, Adapter->PhysicalAddressLength,
                     Added->PortId, sizeof(Added->PortId));
            memcpy(Added->Address, Address, sizeof(Address));
        }
    }
    free(Adapters);
    if (Failed)
    {
        free(List);
        *Count = 0;
        return NULL;
    }
    return List;
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_Interfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosInterface* NmosOs_Interfaces(size_t* Count)
{
    *Count = 0;
    struct ifaddrs* Addresses = NULL;
    if (getifaddrs(&Addresses) != 0)
    {
        return NULL;
    }
    NmosInterface* List = NULL;
    size_t Capacity = 0;
    bool Failed = false;
    for (const struct ifaddrs* a = Addresses; a != NULL && !Failed; a = a->ifa_next)
    {
        char Address[64];
        if (!AddressText(a->ifa_addr, Address, sizeof(Address)))
        {
            continue;
        }
        NmosInterface* Added = AddInterface(&List, Count, &Capacity);
        Failed = Added == NULL;
        if (Added == NULL)
        {
            break;
        }
        snprintf(Added->Name, sizeof(Added->Name), "%s", a->ifa_name);
        WriteMac(NULL, 0, Added->PortId, sizeof(Added->PortId));
        memcpy(Added->Address, Address, sizeof(Address));
    #if defined(__linux__)
        // The MAC address is that of the entry of the interface with its link layer.
        for (const struct ifaddrs* l = Addresses; l != NULL; l = l->ifa_next)
        {
            if (l->ifa_addr != NULL && l->ifa_addr->sa_family == AF_PACKET &&
                strcmp(l->ifa_name, a->ifa_name) == 0)
            {
                const struct sockaddr_ll* Link = (const struct sockaddr_ll*)l->ifa_addr;
                WriteMac(Link->sll_addr, Link->sll_halen, Added->PortId,
                         sizeof(Added->PortId));
                break;
            }
        }
    #endif
    }
    freeifaddrs(Addresses);
    if (Failed)
    {
        free(List);
        *Count = 0;
        return NULL;
    }
    return List;
}

#endif

struct NmosUdp
{
    NmosSocket Socket;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_UdpOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosUdp* NmosOs_UdpOpen(const char* BindAddress, const char* InterfaceAddress)
{
    if (!StartSockets())
    {
        return NULL;
    }
    struct sockaddr_in Local;
    memset(&Local, 0, sizeof(Local));
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = htonl(INADDR_ANY);
    struct in_addr Interface;
    memset(&Interface, 0, sizeof(Interface));
    if ((BindAddress != NULL && inet_pton(AF_INET, BindAddress, &Local.sin_addr) != 1) ||
        (InterfaceAddress != NULL &&
         inet_pton(AF_INET, InterfaceAddress, &Interface) != 1))
    {
        return NULL;
    }
    const NmosSocket Handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (Handle == DTNMOS_NO_SOCKET)
    {
        return NULL;
    }
    // Multicast DNS asks for a hop limit of 255, and a responder on this host answers
    // too.
    const int Ttl = 255;
    const int Loop = 1;
    bool Ok = bind(Handle, (struct sockaddr*)&Local, sizeof(Local)) == 0 &&
              setsockopt(Handle, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&Ttl,
                         sizeof(Ttl)) == 0 &&
              setsockopt(Handle, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&Loop,
                         sizeof(Loop)) == 0;
    if (Ok && InterfaceAddress != NULL)
    {
        Ok = setsockopt(Handle, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&Interface,
                        sizeof(Interface)) == 0;
    }
#if defined(_WIN32)
    // Without this, a datagram to a port nobody listens on makes the next receive fail.
    BOOL Report = FALSE;
    DWORD Returned = 0;
    WSAIoctl(Handle, SIO_UDP_CONNRESET, &Report, sizeof(Report), NULL, 0, &Returned, NULL,
             NULL);
#endif
    NmosUdp* Udp = Ok ? malloc(sizeof(*Udp)) : NULL;
    if (Udp == NULL)
    {
        DTNMOS_CLOSE_SOCKET(Handle);
        return NULL;
    }
    Udp->Socket = Handle;
    return Udp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_UdpPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint16_t NmosOs_UdpPort(const NmosUdp* Udp)
{
    struct sockaddr_in Local;
    socklen_t Length = sizeof(Local);
    if (getsockname(Udp->Socket, (struct sockaddr*)&Local, &Length) != 0)
    {
        return 0;
    }
    return ntohs(Local.sin_port);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_UdpSend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosOs_UdpSend(NmosUdp* Udp, const char* Address, uint16_t Port, const void* Data,
                    size_t Length)
{
    struct sockaddr_in To;
    memset(&To, 0, sizeof(To));
    To.sin_family = AF_INET;
    To.sin_port = htons(Port);
    if (inet_pton(AF_INET, Address, &To.sin_addr) != 1)
    {
        return false;
    }
    return (size_t)sendto(Udp->Socket, (const char*)Data, DTNMOS_SOCKET_LENGTH(Length), 0,
                          (struct sockaddr*)&To, sizeof(To)) == Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_UdpReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int NmosOs_UdpReceive(NmosUdp* Udp, void* Buffer, size_t Size, uint32_t TimeoutMs,
                      char* FromAddress, size_t FromSize, uint16_t* FromPort)
{
    fd_set Readable;
    FD_ZERO(&Readable);
    FD_SET(Udp->Socket, &Readable);
    struct timeval Timeout;
    Timeout.tv_sec = (long)(TimeoutMs / 1000u);
    Timeout.tv_usec = (long)(TimeoutMs % 1000u) * 1000;
    const int Ready = select((int)Udp->Socket + 1, &Readable, NULL, NULL, &Timeout);
    if (Ready == 0)
    {
        return 0;
    }
    if (Ready < 0)
    {
        return -1;
    }
    struct sockaddr_in From;
    socklen_t FromLength = sizeof(From);
    const int Received =
        (int)recvfrom(Udp->Socket, (char*)Buffer, DTNMOS_SOCKET_LENGTH(Size), 0,
                      (struct sockaddr*)&From, &FromLength);
    if (Received < 0)
    {
        return -1;
    }
    if (FromAddress != NULL &&
        inet_ntop(AF_INET, &From.sin_addr, FromAddress, (socklen_t)FromSize) == NULL)
    {
        FromAddress[0] = '\0';
    }
    if (FromPort != NULL)
    {
        *FromPort = ntohs(From.sin_port);
    }
    return Received;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_UdpClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosOs_UdpClose(NmosUdp* Udp)
{
    if (Udp != NULL)
    {
        DTNMOS_CLOSE_SOCKET(Udp->Socket);
        free(Udp);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosOs_SystemDns -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosOs_SystemDns(char* Server, size_t ServerSize, char* Domain, size_t DomainSize)
{
    Server[0] = '\0';
    Domain[0] = '\0';
#if defined(_WIN32)
    // The suffix that DHCP gives is the one of the connection, which GetNetworkParams()
    // does not know; that of the domain of the PC is taken when it has none.
    IP_ADAPTER_ADDRESSES* Adapters = GetAdapters(
        GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST);
    for (const IP_ADAPTER_ADDRESSES* Adapter = Adapters;
         Adapter != NULL && Server[0] == '\0'; Adapter = Adapter->Next)
    {
        if (Adapter->OperStatus != IfOperStatusUp ||
            Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            Adapter->FirstGatewayAddress == NULL)
        {
            continue;
        }
        for (const IP_ADAPTER_DNS_SERVER_ADDRESS* Dns = Adapter->FirstDnsServerAddress;
             Dns != NULL; Dns = Dns->Next)
        {
            const struct sockaddr* Address = Dns->Address.lpSockaddr;
            if (Address != NULL && Address->sa_family == AF_INET &&
                inet_ntop(AF_INET, &((const struct sockaddr_in*)Address)->sin_addr,
                          Server, ServerSize) != NULL)
            {
                break;
            }
            Server[0] = '\0';
        }
        if (Server[0] != '\0' && Adapter->DnsSuffix != NULL &&
            WideCharToMultiByte(CP_UTF8, 0, Adapter->DnsSuffix, -1, Domain,
                                (int)DomainSize, NULL, NULL) == 0)
        {
            Domain[0] = '\0';
        }
    }
    free(Adapters);
    if (Server[0] != '\0' && Domain[0] == '\0')
    {
        FIXED_INFO* Params = NULL;
        ULONG Length = 0;
        if (GetNetworkParams(NULL, &Length) == ERROR_BUFFER_OVERFLOW &&
            (Params = malloc(Length)) != NULL &&
            GetNetworkParams(Params, &Length) == NO_ERROR &&
            strlen(Params->DomainName) < DomainSize)
        {
            memcpy(Domain, Params->DomainName, strlen(Params->DomainName) + 1);
        }
        free(Params);
    }
#else
    FILE* File = fopen("/etc/resolv.conf", "r");
    if (File == NULL)
    {
        return;
    }
    char Text[16 * 1024];
    const size_t Length = fread(Text, 1, sizeof(Text) - 1, File);
    fclose(File);
    Text[Length] = '\0';
    NmosDns_ReadResolvConf(Text, Server, ServerSize, Domain, DomainSize);
#endif
}
