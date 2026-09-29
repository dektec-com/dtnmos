// SPDX-License-Identifier: BSD-3-Clause
//
// The mutex, thread, time, addresses and datagram sockets of platform.h, on Windows and
// on POSIX.

// Strict C11 hides the functions of POSIX that this file needs.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
    #define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // Of mstcpip.h, which is not included, as it needs a particular order of the headers.
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <pthread.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <time.h>
    #include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

// TAI runs ahead of UTC by the leap seconds, 37 since 2017.
#define DTNMOS_TAI_OFFSET 37

#if defined(_WIN32)

struct dtnmos_mutex
{
    SRWLOCK lock;
};

dtnmos_mutex* dtnmos_mutex_create(void)
{
    dtnmos_mutex* mutex = malloc(sizeof(*mutex));
    if (mutex != NULL)
    {
        InitializeSRWLock(&mutex->lock);
    }
    return mutex;
}

void dtnmos_mutex_free(dtnmos_mutex* mutex)
{
    free(mutex);
}

void dtnmos_mutex_lock(dtnmos_mutex* mutex)
{
    AcquireSRWLockExclusive(&mutex->lock);
}

void dtnmos_mutex_unlock(dtnmos_mutex* mutex)
{
    ReleaseSRWLockExclusive(&mutex->lock);
}

struct dtnmos_thread
{
    HANDLE handle;
    void (*function)(void*);
    void* argument;
};

static DWORD WINAPI run_thread(LPVOID parameter)
{
    dtnmos_thread* thread = parameter;
    thread->function(thread->argument);
    return 0;
}

dtnmos_thread* dtnmos_thread_start(void (*function)(void*), void* argument)
{
    dtnmos_thread* thread = malloc(sizeof(*thread));
    if (thread == NULL)
    {
        return NULL;
    }
    thread->function = function;
    thread->argument = argument;
    thread->handle = CreateThread(NULL, 0, run_thread, thread, 0, NULL);
    if (thread->handle == NULL)
    {
        free(thread);
        return NULL;
    }
    return thread;
}

void dtnmos_thread_join(dtnmos_thread* thread)
{
    if (thread == NULL)
    {
        return;
    }
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    free(thread);
}

void dtnmos_sleep_ms(uint32_t milliseconds)
{
    Sleep(milliseconds);
}

uint64_t dtnmos_monotonic_ms(void)
{
    return (uint64_t)GetTickCount64();
}

// Winsock is started once per process and left running, as the process ends it.
static int start_sockets(void)
{
    static volatile LONG started = 0;
    if (InterlockedCompareExchange(&started, 1, 0) == 0)
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            started = 0;
            return 0;
        }
    }
    return 1;
}

    #define DTNMOS_CLOSE_SOCKET closesocket
typedef SOCKET dtnmos_socket;
    // The length of a buffer, as the sockets of the platform take it.
    #define DTNMOS_SOCKET_LENGTH(length) ((int)(length))
    #define DTNMOS_NO_SOCKET INVALID_SOCKET

#else

struct dtnmos_mutex
{
    pthread_mutex_t lock;
};

dtnmos_mutex* dtnmos_mutex_create(void)
{
    dtnmos_mutex* mutex = malloc(sizeof(*mutex));
    if (mutex != NULL && pthread_mutex_init(&mutex->lock, NULL) != 0)
    {
        free(mutex);
        return NULL;
    }
    return mutex;
}

void dtnmos_mutex_free(dtnmos_mutex* mutex)
{
    if (mutex != NULL)
    {
        pthread_mutex_destroy(&mutex->lock);
        free(mutex);
    }
}

void dtnmos_mutex_lock(dtnmos_mutex* mutex)
{
    pthread_mutex_lock(&mutex->lock);
}

void dtnmos_mutex_unlock(dtnmos_mutex* mutex)
{
    pthread_mutex_unlock(&mutex->lock);
}

struct dtnmos_thread
{
    pthread_t handle;
    void (*function)(void*);
    void* argument;
};

static void* run_thread(void* parameter)
{
    dtnmos_thread* thread = parameter;
    thread->function(thread->argument);
    return NULL;
}

dtnmos_thread* dtnmos_thread_start(void (*function)(void*), void* argument)
{
    dtnmos_thread* thread = malloc(sizeof(*thread));
    if (thread == NULL)
    {
        return NULL;
    }
    thread->function = function;
    thread->argument = argument;
    if (pthread_create(&thread->handle, NULL, run_thread, thread) != 0)
    {
        free(thread);
        return NULL;
    }
    return thread;
}

void dtnmos_thread_join(dtnmos_thread* thread)
{
    if (thread == NULL)
    {
        return;
    }
    pthread_join(thread->handle, NULL);
    free(thread);
}

void dtnmos_sleep_ms(uint32_t milliseconds)
{
    struct timespec duration;
    duration.tv_sec = (time_t)(milliseconds / 1000);
    duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&duration, &duration) != 0)
    {
    }
}

uint64_t dtnmos_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int start_sockets(void)
{
    return 1;
}

    #define DTNMOS_CLOSE_SOCKET close
typedef int dtnmos_socket;
    #define DTNMOS_SOCKET_LENGTH(length) (length)
    #define DTNMOS_NO_SOCKET (-1)

#endif

void dtnmos_version_now(uint64_t* last, char* text, size_t size)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    uint64_t nanoseconds =
        ((uint64_t)now.tv_sec + DTNMOS_TAI_OFFSET) * 1000000000u + (uint64_t)now.tv_nsec;
    // Versions must rise even when two changes fall within one tick of the clock.
    if (nanoseconds <= *last)
    {
        nanoseconds = *last + 1;
    }
    *last = nanoseconds;
    snprintf(text, size, "%llu:%llu", (unsigned long long)(nanoseconds / 1000000000u),
             (unsigned long long)(nanoseconds % 1000000000u));
}

int dtnmos_address_toward(const char* host, char* address, size_t size)
{
    if (host == NULL || !start_sockets())
    {
        return 0;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* found = NULL;
    if (getaddrinfo(host, "9", &hints, &found) != 0 || found == NULL)
    {
        return 0;
    }
    int result = 0;
    // Connecting a datagram socket sends nothing; it only chooses the route and so the
    // address of this host on it.
    const dtnmos_socket probe = socket(found->ai_family, SOCK_DGRAM, 0);
    if (probe != DTNMOS_NO_SOCKET)
    {
        struct sockaddr_storage local;
        socklen_t length = sizeof(local);
        if (connect(probe, found->ai_addr, (socklen_t)found->ai_addrlen) == 0 &&
            getsockname(probe, (struct sockaddr*)&local, &length) == 0)
        {
            const void* bytes =
                local.ss_family == AF_INET6
                    ? (const void*)&((struct sockaddr_in6*)&local)->sin6_addr
                    : (const void*)&((struct sockaddr_in*)&local)->sin_addr;
            result = inet_ntop(local.ss_family, bytes, address, (socklen_t)size) != NULL;
        }
        DTNMOS_CLOSE_SOCKET(probe);
    }
    freeaddrinfo(found);
    return result;
}

uint16_t dtnmos_free_port(const char* host)
{
    if (host == NULL || !start_sockets())
    {
        return 0;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo* found = NULL;
    if (getaddrinfo(host, "0", &hints, &found) != 0 || found == NULL)
    {
        return 0;
    }
    uint16_t port = 0;
    const dtnmos_socket probe = socket(found->ai_family, SOCK_STREAM, 0);
    if (probe != DTNMOS_NO_SOCKET)
    {
        struct sockaddr_storage local;
        socklen_t length = sizeof(local);
        if (bind(probe, found->ai_addr, (socklen_t)found->ai_addrlen) == 0 &&
            getsockname(probe, (struct sockaddr*)&local, &length) == 0)
        {
            port = ntohs(local.ss_family == AF_INET6
                             ? ((struct sockaddr_in6*)&local)->sin6_port
                             : ((struct sockaddr_in*)&local)->sin_port);
        }
        DTNMOS_CLOSE_SOCKET(probe);
    }
    freeaddrinfo(found);
    return port;
}

struct dtnmos_udp
{
    dtnmos_socket socket;
};

dtnmos_udp* dtnmos_udp_open(const char* bind_address, const char* interface_address)
{
    if (!start_sockets())
    {
        return NULL;
    }
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    struct in_addr interface;
    memset(&interface, 0, sizeof(interface));
    if ((bind_address != NULL &&
         inet_pton(AF_INET, bind_address, &local.sin_addr) != 1) ||
        (interface_address != NULL &&
         inet_pton(AF_INET, interface_address, &interface) != 1))
    {
        return NULL;
    }
    const dtnmos_socket handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (handle == DTNMOS_NO_SOCKET)
    {
        return NULL;
    }
    // Multicast DNS asks for a hop limit of 255, and a responder on this host answers
    // too.
    const int ttl = 255;
    const int loop = 1;
    int ok = bind(handle, (struct sockaddr*)&local, sizeof(local)) == 0 &&
             setsockopt(handle, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl,
                        sizeof(ttl)) == 0 &&
             setsockopt(handle, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop,
                        sizeof(loop)) == 0;
    if (ok && interface_address != NULL)
    {
        ok = setsockopt(handle, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&interface,
                        sizeof(interface)) == 0;
    }
#if defined(_WIN32)
    // Without this, a datagram to a port nobody listens on makes the next receive fail.
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(handle, SIO_UDP_CONNRESET, &report, sizeof(report), NULL, 0, &returned, NULL,
             NULL);
#endif
    dtnmos_udp* udp = ok ? malloc(sizeof(*udp)) : NULL;
    if (udp == NULL)
    {
        DTNMOS_CLOSE_SOCKET(handle);
        return NULL;
    }
    udp->socket = handle;
    return udp;
}

uint16_t dtnmos_udp_port(const dtnmos_udp* udp)
{
    struct sockaddr_in local;
    socklen_t length = sizeof(local);
    if (getsockname(udp->socket, (struct sockaddr*)&local, &length) != 0)
    {
        return 0;
    }
    return ntohs(local.sin_port);
}

int dtnmos_udp_send(dtnmos_udp* udp, const char* address, uint16_t port, const void* data,
                    size_t length)
{
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &to.sin_addr) != 1)
    {
        return 0;
    }
    return (size_t)sendto(udp->socket, (const char*)data, DTNMOS_SOCKET_LENGTH(length), 0,
                          (struct sockaddr*)&to, sizeof(to)) == length;
}

long dtnmos_udp_receive(dtnmos_udp* udp, void* buffer, size_t size, uint32_t timeout_ms,
                        char* from_address, size_t from_size, uint16_t* from_port)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(udp->socket, &readable);
    struct timeval timeout;
    timeout.tv_sec = (long)(timeout_ms / 1000u);
    timeout.tv_usec = (long)(timeout_ms % 1000u) * 1000;
    const int ready = select((int)udp->socket + 1, &readable, NULL, NULL, &timeout);
    if (ready == 0)
    {
        return 0;
    }
    if (ready < 0)
    {
        return -1;
    }
    struct sockaddr_in from;
    socklen_t from_length = sizeof(from);
    const long received =
        (long)recvfrom(udp->socket, (char*)buffer, DTNMOS_SOCKET_LENGTH(size), 0,
                       (struct sockaddr*)&from, &from_length);
    if (received < 0)
    {
        return -1;
    }
    if (from_address != NULL &&
        inet_ntop(AF_INET, &from.sin_addr, from_address, (socklen_t)from_size) == NULL)
    {
        from_address[0] = '\0';
    }
    if (from_port != NULL)
    {
        *from_port = ntohs(from.sin_port);
    }
    return received;
}

void dtnmos_udp_close(dtnmos_udp* udp)
{
    if (udp != NULL)
    {
        DTNMOS_CLOSE_SOCKET(udp->socket);
        free(udp);
    }
}
