// #*#*#*#*#*#*#*#*#*#*#*# DtNmosRegistrySearch.c *#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The search of an application for registries, shared by its nodes and clients
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdlib.h>
#include <string.h>

#include "NmosDiscovery.h"
#include "NmosInternal.h"
#include "NmosOs.h"

// The time between the searches while no node waits for a registry, which keeps the list
// a node fails over to fresh; and the longest between those while one waits.
#define NMOS_SEARCH_EVERY_MS 3000u
#define NMOS_SEARCH_MAX_WAIT_MS 8000u

struct DtNmosRegistrySearch
{
    int Open;
    NmosMutex* Mutex;
    unsigned Finds;
    int Fed;
    // How it searches; its strings are the search's copies, which do not change while it
    // runs.
    DtNmosDiscoveryConfig Discovery;
    DtNmosRegistryList* Lists[2]; // of the Query and the Registration API, under the lock
    NmosThread* Thread;
    int Stop;    // under the lock
    int Hurried; // a node had no registry since the last search; under the lock
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IndexOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The index in Lists of the list of the APIs of Service.
//
static int IndexOf(DtNmosService Service)
{
    return Service == DTNMOS_SERVICE_QUERY ? 0 : 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Finds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the search finds the APIs of Service.
//
static int Finds(const DtNmosRegistrySearch* Search, DtNmosService Service)
{
    const unsigned Bit = Service == DTNMOS_SERVICE_QUERY ? DTNMOS_FINDS_QUERY
                         : Service == DTNMOS_SERVICE_REGISTRATION
                             ? DTNMOS_FINDS_REGISTRATION
                             : 0u;
    return (Search->Finds & Bit) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult CheckOpen(const DtNmosRegistrySearch* Search, const char* Function)
{
    if (Search == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "%s() needs a search.",
                              Function);
    }
    if (!Search->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "%s() needs an open search.", Function);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyOptional -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies Text, leaving null null; sets *Failed when the memory ran out.
//
static char* CopyOptional(const char* Text, int* Failed)
{
    if (Text == NULL)
    {
        return NULL;
    }
    const size_t Length = strlen(Text);
    char* Copy = malloc(Length + 1);
    if (Copy == NULL)
    {
        *Failed = 1;
        return NULL;
    }
    memcpy(Copy, Text, Length + 1);
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Stops -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits up to WaitMs, in steps, and returns whether the search is to stop. A wait that
// Hurry may cut short ends as soon as a node has no registry.
//
static int Stops(DtNmosRegistrySearch* Search, uint32_t WaitMs, int Hurry)
{
    const uint64_t Until = NmosOs_MonotonicMs() + WaitMs;
    for (;;)
    {
        NmosOs_MutexLock(Search->Mutex);
        const int Stop = Search->Stop;
        const int Hurried = Search->Hurried;
        NmosOs_MutexUnlock(Search->Mutex);
        const uint64_t Now = NmosOs_MonotonicMs();
        if (Stop || Now >= Until || (Hurry && Hurried))
        {
            return Stop;
        }
        NmosOs_SleepMs(Until - Now < 50 ? (uint32_t)(Until - Now) : 50);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SearchLoop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The thread of a search: searches for each API it finds, and keeps what it found for
// its users. While no node waits for a registry, it searches every 3 seconds, and at once
// when one starts to wait; while one waits, after a wait that doubles from a second to 8,
// short enough for a registry that answers only once it saw the question, and long
// enough not to flood the network.
//
static void SearchLoop(void* Argument)
{
    DtNmosRegistrySearch* Search = Argument;
    uint32_t WaitMs = 0;
    uint32_t BackOffMs = 0;
    while (!Stops(Search, WaitMs, BackOffMs == 0))
    {
        const DtNmosService Services[] = {DTNMOS_SERVICE_QUERY,
                                          DTNMOS_SERVICE_REGISTRATION};
        for (size_t i = 0; i < sizeof(Services) / sizeof(Services[0]); ++i)
        {
            if (!Finds(Search, Services[i]))
            {
                continue;
            }
            DtNmosDiscoveryConfig Config = Search->Discovery;
            Config.Service = Services[i];
            DtNmosRegistryList* List = NULL;
            if (DtNmos_Discover(&Config, &List) != DTNMOS_OK &&
                Search->Discovery.Log != NULL)
            {
                Search->Discovery.Log(Search->Discovery.LogUser, DTNMOS_LOG_WARNING,
                                      DtNmos_GetLastError());
            }
            NmosOs_MutexLock(Search->Mutex);
            DtNmosRegistryList_Free(Search->Lists[IndexOf(Services[i])]);
            Search->Lists[IndexOf(Services[i])] = List;
            NmosOs_MutexUnlock(Search->Mutex);
        }
        NmosOs_MutexLock(Search->Mutex);
        const int Hurried = Search->Hurried;
        Search->Hurried = 0;
        NmosOs_MutexUnlock(Search->Mutex);
        if (Hurried)
        {
            BackOffMs = BackOffMs == 0 ? 1000u : BackOffMs * 2;
            BackOffMs =
                BackOffMs > NMOS_SEARCH_MAX_WAIT_MS ? NMOS_SEARCH_MAX_WAIT_MS : BackOffMs;
            WaitMs = BackOffMs;
        }
        else
        {
            BackOffMs = 0;
            WaitMs = NMOS_SEARCH_EVERY_MS;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosRegistrySearch* DtNmosRegistrySearch_Alloc(void)
{
    return calloc(1, sizeof(DtNmosRegistrySearch));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frees what search holds and leaves it empty and closed; its thread has ended.
//
static void Release(DtNmosRegistrySearch* Search)
{
    DtNmosRegistryList_Free(Search->Lists[0]);
    DtNmosRegistryList_Free(Search->Lists[1]);
    free((char*)Search->Discovery.InterfaceAddress);
    free((char*)Search->Discovery.Destination);
    free((char*)Search->Discovery.DnsServer);
    free((char*)Search->Discovery.DnsDomain);
    NmosOs_MutexFree(Search->Mutex);
    memset(Search, 0, sizeof(*Search));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosRegistrySearch_Close(DtNmosRegistrySearch* Search)
{
    const DtNmosResult Result = CheckOpen(Search, "DtNmosRegistrySearch_Close");
    if (Result != DTNMOS_OK)
    {
        return Result;
    }
    if (Search->Thread != NULL)
    {
        NmosOs_MutexLock(Search->Mutex);
        Search->Stop = 1;
        NmosOs_MutexUnlock(Search->Mutex);
        NmosOs_ThreadJoin(Search->Thread);
    }
    Release(Search);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_Feed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosRegistrySearch_Feed(DtNmosRegistrySearch* Search,
                                       DtNmosService Service, const char* const* Urls,
                                       size_t Count)
{
    const DtNmosResult Open = CheckOpen(Search, "DtNmosRegistrySearch_Feed");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (!Search->Fed)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "Only a search opened as fed is fed; this one searches.");
    }
    if (!Finds(Search, Service) || (Count > 0 && Urls == NULL))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The search is fed URLs of an API it finds.");
    }
    DtNmosRegistryList* List = NULL;
    const DtNmosResult Made = NmosRegistryList_FromUrls(Service, Urls, Count, &List);
    if (Made != DTNMOS_OK)
    {
        return Made;
    }
    NmosOs_MutexLock(Search->Mutex);
    DtNmosRegistryList_Free(Search->Lists[IndexOf(Service)]);
    Search->Lists[IndexOf(Service)] = List;
    NmosOs_MutexUnlock(Search->Mutex);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosRegistrySearch_Free(DtNmosRegistrySearch* Search)
{
    if (Search == NULL)
    {
        return;
    }
    if (Search->Open)
    {
        DtNmosRegistrySearch_Close(Search);
    }
    free(Search);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosRegistrySearch_Freep(DtNmosRegistrySearch** Search)
{
    if (Search != NULL)
    {
        DtNmosRegistrySearch_Free(*Search);
        *Search = NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_List -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosRegistrySearch_List(DtNmosRegistrySearch* Search,
                                       DtNmosService Service, DtNmosRegistryList** List)
{
    if (List != NULL)
    {
        *List = NULL;
    }
    const DtNmosResult Open = CheckOpen(Search, "DtNmosRegistrySearch_List");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (List == NULL || !Finds(Search, Service))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosRegistrySearch_List() needs a list, of an API the "
                              "search finds.");
    }
    NmosOs_MutexLock(Search->Mutex);
    const DtNmosResult Result =
        NmosRegistryList_Copy(Search->Lists[IndexOf(Service)], List);
    NmosOs_MutexUnlock(Search->Mutex);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistrySearch_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosRegistrySearch_Open(DtNmosRegistrySearch* Search,
                                       const DtNmosRegistrySearchConfig* Config)
{
    if (Search == NULL || Config == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosRegistrySearch_Open() needs a search and a config.");
    }
    if (Search->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "The search is open already; close it first.");
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(Config, DtNmosRegistrySearchConfig,
                                                 sizeof(DtNmosRegistrySearchConfig));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    const unsigned Known = DTNMOS_FINDS_QUERY | DTNMOS_FINDS_REGISTRATION;
    if (Config->Finds == 0 || (Config->Finds & ~Known) != 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A search finds DTNMOS_FINDS_QUERY, "
                              "DTNMOS_FINDS_REGISTRATION or both.");
    }
    if (Config->Discovery != NULL)
    {
        const DtNmosResult DiscoverySized = DTNMOS_CHECK_SIZE(
            Config->Discovery, DtNmosDiscoveryConfig, sizeof(DtNmosDiscoveryConfig));
        if (DiscoverySized != DTNMOS_OK)
        {
            return DiscoverySized;
        }
        Search->Discovery = *Config->Discovery;
    }
    Search->Discovery.Size = sizeof(Search->Discovery);
    int Failed = 0;
    Search->Discovery.InterfaceAddress =
        CopyOptional(Search->Discovery.InterfaceAddress, &Failed);
    Search->Discovery.Destination = CopyOptional(Search->Discovery.Destination, &Failed);
    Search->Discovery.DnsServer = CopyOptional(Search->Discovery.DnsServer, &Failed);
    Search->Discovery.DnsDomain = CopyOptional(Search->Discovery.DnsDomain, &Failed);
    Search->Finds = Config->Finds;
    Search->Fed = Config->Fed != 0;
    Search->Mutex = NmosOs_MutexCreate();
    if (Failed || Search->Mutex == NULL)
    {
        Release(Search);
        return NmosError_FailMemory();
    }
    Search->Open = 1;
    if (!Search->Fed)
    {
        Search->Thread = NmosOs_ThreadStart(SearchLoop, Search);
        if (Search->Thread == NULL)
        {
            Release(Search);
            return NmosError_Fail(DTNMOS_E_INTERNAL,
                                  "The thread of the search did not start.");
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosRegistrySearch_Hurry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosRegistrySearch_Hurry(DtNmosRegistrySearch* Search)
{
    if (Search != NULL && Search->Open)
    {
        NmosOs_MutexLock(Search->Mutex);
        Search->Hurried = 1;
        NmosOs_MutexUnlock(Search->Mutex);
    }
}
