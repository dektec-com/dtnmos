// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosDiscovery.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Finding registries with DNS-SD over a one-shot query of multicast DNS
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosDiscovery.h"
#include "NmosDns.h"
#include "NmosInternal.h"
#include "NmosOs.h"

enum
{
    NMOS_MAX_INSTANCES = 64,
    NMOS_MAX_HOSTS = 64,
    NMOS_MAX_INTERFACES = 32, // that multicast DNS is asked on
    NMOS_MAX_MESSAGE = 9000,  // the largest answer multicast DNS allows
    NMOS_SENDS = 3,           // of the first query, spread over the timeout
    NMOS_DEFAULT_TIMEOUT_MS = 1000,
    NMOS_MDNS_PORT = 5353,
    NMOS_DNS_PORT = 53
};

static const char* const MdnsAddress = "224.0.0.251";

static bool IsIpv4(const char* Text);

// A service instance as its records describe it.
typedef struct NmosInstance
{
    char Name[DTNMOS_DNS_NAME_SIZE]; // e.g. "Registry 1._nmos-query._tcp.local"
    bool HasSrv;
    bool HasTxt;
    char Host[DTNMOS_DNS_NAME_SIZE];
    uint16_t Port;
    int Priority;
    char Proto[16];
    char Versions[128];
    bool Auth;
} NmosInstance;

typedef struct NmosHostAddress
{
    char Name[DTNMOS_DNS_NAME_SIZE];
    uint8_t Address[4];
} NmosHostAddress;

// What the answers told so far.
typedef struct NmosGathered
{
    const char* Service; // e.g. "_nmos-query._tcp.local"
    NmosInstance Instances[NMOS_MAX_INSTANCES];
    size_t InstanceCount;
    NmosHostAddress Hosts[NMOS_MAX_HOSTS];
    size_t HostCount;
} NmosGathered;

// One of the searches: where its queries go, and what its answers told.
typedef struct NmosSearch
{
    bool Active;
    DtNmosSearch Kind;
    char Address[64];
    uint16_t Port;
    uint16_t Flags;                     // of its queries
    bool Answered;                      // the DNS server answered its first query
    char Service[DTNMOS_DNS_NAME_SIZE]; // e.g. "_nmos-query._tcp.local"
    NmosGathered Found;
} NmosSearch;

// The interfaces multicast DNS is asked on. One without an address is that of the
// default route, which the socket sends from as it is.
typedef struct NmosInterfaceSet
{
    char Addresses[NMOS_MAX_INTERFACES][64];
    bool Live[NMOS_MAX_INTERFACES]; // false once a query could not be sent from it
    size_t Count;
} NmosInterfaceSet;

// A list owns the strings of its registries that are no arrays in its store.
struct DtNmosRegistryList
{
    DtNmosRegistryInfo* Registries;
    size_t Count;
    NmosStore Store;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosRegistryList_Count(const DtNmosRegistryList* List)
{
    return List == NULL ? 0 : List->Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosRegistryInfo* DtNmosRegistryList_At(const DtNmosRegistryList* List,
                                                size_t Index)
{
    return List == NULL || Index >= List->Count ? NULL : &List->Registries[Index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosRegistryList_Free(DtNmosRegistryList* List)
{
    if (List == NULL)
    {
        return;
    }
    NmosStore_Free(&List->Store);
    free(List->Registries);
    free(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NewList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns an empty list with room for Count registries, or null when out of memory.
//
static DtNmosRegistryList* NewList(size_t Count)
{
    DtNmosRegistryList* List = calloc(1, sizeof(*List));
    if (List != NULL && Count > 0 &&
        (List->Registries = calloc(Count, sizeof(*List->Registries))) == NULL)
    {
        free(List);
        return NULL;
    }
    return List;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosRegistryList_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosRegistryList_Copy(const DtNmosRegistryList* From,
                                   DtNmosRegistryList** To)
{
    const size_t Count = DtNmosRegistryList_Count(From);
    DtNmosRegistryList* List = NewList(Count);
    if (List == NULL)
    {
        return NmosError_FailMemory();
    }
    for (size_t i = 0; i < Count; ++i)
    {
        const DtNmosRegistryInfo* Source = &From->Registries[i];
        DtNmosRegistryInfo* Copy = &List->Registries[List->Count++];
        *Copy = *Source;
        Copy->Instance =
            NmosStore_Text(&List->Store, Source->Instance, strlen(Source->Instance));
        Copy->Url = NmosStore_Text(&List->Store, Source->Url, strlen(Source->Url));
        Copy->ApiVersions = NmosStore_Text(&List->Store, Source->ApiVersions,
                                           strlen(Source->ApiVersions));
        if (Copy->Instance == NULL || Copy->Url == NULL || Copy->ApiVersions == NULL)
        {
            DtNmosRegistryList_Free(List);
            return NmosError_FailMemory();
        }
    }
    *To = List;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosRegistryList_FromUrls -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosRegistryList_FromUrls(DtNmosService Service, const char* const* Urls,
                                       size_t Count, DtNmosRegistryList** To)
{
    DtNmosRegistryList* List = NewList(Count);
    if (List == NULL)
    {
        return NmosError_FailMemory();
    }
    for (size_t i = 0; i < Count; ++i)
    {
        // "<http or https>://<host>[:<port>]", the host an address between brackets for
        // IPv6.
        const char* Url = Urls[i] != NULL ? Urls[i] : "";
        const bool Https = strncmp(Url, "https://", 8) == 0;
        if (!Https && strncmp(Url, "http://", 7) != 0)
        {
            DtNmosRegistryList_Free(List);
            return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                  "%s is no URL of http or https.", Url);
        }
        const char* Host = Url + (Https ? 8 : 7);
        const bool Bracketed = Host[0] == '[';
        const char* HostEnd = Bracketed ? strchr(Host, ']') : Host + strcspn(Host, ":/");
        if (HostEnd == NULL || HostEnd == Host + (Bracketed ? 1 : 0) ||
            (size_t)(HostEnd - Host) >= DTNMOS_MAX_ADDRESS_SIZE)
        {
            DtNmosRegistryList_Free(List);
            return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "%s names no host.", Url);
        }
        const char* After = HostEnd + (Bracketed ? 1 : 0);
        // The port, when there is one, runs up to a path or the end of the URL.
        uint32_t Port = Https ? 443 : 80;
        const NmosSpan Number = {After + 1, *After == ':' ? strcspn(After + 1, "/") : 0};
        if (*After == ':' && (!NmosText_ParseU32(Number, 65535, &Port) || Port == 0))
        {
            DtNmosRegistryList_Free(List);
            return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                  "%s has no port of 1 to 65535.", Url);
        }
        DtNmosRegistryInfo* Registry = &List->Registries[List->Count++];
        Registry->Service = Service;
        const char* Name = Host + (Bracketed ? 1 : 0);
        const size_t NameLength = (size_t)(HostEnd - Name);
        memcpy(Registry->Host, Name, NameLength);
        Registry->Host[NameLength] = '\0';
        if (IsIpv4(Registry->Host))
        {
            snprintf(Registry->Address, sizeof(Registry->Address), "%s", Registry->Host);
        }
        Registry->Port = (uint16_t)Port;
        snprintf(Registry->ApiProto, sizeof(Registry->ApiProto), "%s",
                 Https ? "https" : "http");
        Registry->Priority = (int)i;
        Registry->Usable = true;
        // The URL of an API is its base, without a path.
        size_t Length = strlen(Url);
        while (Length > 0 && Url[Length - 1] == '/')
        {
            --Length;
        }
        Registry->Instance = NmosStore_Text(&List->Store, Url, Length);
        Registry->Url = NmosStore_Text(&List->Store, Url, Length);
        Registry->ApiVersions = NmosStore_Text(&List->Store, "v1.3", 4);
        if (Registry->Instance == NULL || Registry->Url == NULL ||
            Registry->ApiVersions == NULL)
        {
            DtNmosRegistryList_Free(List);
            return NmosError_FailMemory();
        }
    }
    *To = List;
    return DTNMOS_OK;
}

static void LogMessage(const DtNmosDiscoveryConfig* Config, DtNmosLogLevel Level,
                       const char* Format, ...) DTNMOS_PRINTF(3, 4);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LogMessage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void LogMessage(const DtNmosDiscoveryConfig* Config, DtNmosLogLevel Level,
                       const char* Format, ...)
{
    if (Config->Log == NULL)
    {
        return;
    }
    char Message[512];
    va_list Arguments;
    va_start(Arguments, Format);
    vsnprintf(Message, sizeof(Message), Format, Arguments);
    va_end(Arguments);
    Config->Log(Config->LogUser, Level, Message);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsInstanceOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether name is an instance of the service, "<instance>.<service>".
//
static bool IsInstanceOf(const char* Name, const char* Service)
{
    const size_t NameLength = strlen(Name);
    const size_t ServiceLength = strlen(Service);
    return NameLength > ServiceLength + 1 &&
           Name[NameLength - ServiceLength - 1] == '.' &&
           NmosDns_SameName(Name + NameLength - ServiceLength, Service);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InstanceNamed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the instance of that name, adding it when it is new; null when there is no
// room.
//
static NmosInstance* InstanceNamed(NmosGathered* Found, const char* Name)
{
    for (size_t i = 0; i < Found->InstanceCount; ++i)
    {
        if (NmosDns_SameName(Found->Instances[i].Name, Name))
        {
            return &Found->Instances[i];
        }
    }
    if (Found->InstanceCount == NMOS_MAX_INSTANCES)
    {
        return NULL;
    }
    NmosInstance* Added = &Found->Instances[Found->InstanceCount++];
    memset(Added, 0, sizeof(*Added));
    snprintf(Added->Name, sizeof(Added->Name), "%s", Name);
    Added->Priority = -1;
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HostNamed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const NmosHostAddress* HostNamed(const NmosGathered* Found, const char* Name)
{
    for (size_t i = 0; i < Found->HostCount; ++i)
    {
        if (NmosDns_SameName(Found->Hosts[i].Name, Name))
        {
            return &Found->Hosts[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeRecord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void TakeRecord(void* User, const NmosDnsRecord* Record)
{
    NmosGathered* Found = User;
    if (Record->Type == DTNMOS_DNS_TYPE_PTR &&
        NmosDns_SameName(Record->Name, Found->Service))
    {
        if (IsInstanceOf(Record->Target, Found->Service))
        {
            (void)InstanceNamed(Found, Record->Target);
        }
        return;
    }
    if (Record->Type == DTNMOS_DNS_TYPE_A)
    {
        if (HostNamed(Found, Record->Name) == NULL && Found->HostCount < NMOS_MAX_HOSTS)
        {
            NmosHostAddress* Added = &Found->Hosts[Found->HostCount++];
            snprintf(Added->Name, sizeof(Added->Name), "%s", Record->Name);
            memcpy(Added->Address, Record->Address, 4);
        }
        return;
    }
    if ((Record->Type != DTNMOS_DNS_TYPE_SRV && Record->Type != DTNMOS_DNS_TYPE_TXT) ||
        !IsInstanceOf(Record->Name, Found->Service))
    {
        return;
    }
    NmosInstance* Service = InstanceNamed(Found, Record->Name);
    if (Service == NULL)
    {
        return;
    }
    if (Record->Type == DTNMOS_DNS_TYPE_SRV)
    {
        Service->HasSrv = true;
        snprintf(Service->Host, sizeof(Service->Host), "%s", Record->Target);
        Service->Port = Record->Port;
        return;
    }
    Service->HasTxt = true;
    char Value[128];
    if (NmosDns_TxtValue(Record->Txt, Record->TxtLength, "pri", Value, sizeof(Value)))
    {
        Service->Priority = atoi(Value);
    }
    if (NmosDns_TxtValue(Record->Txt, Record->TxtLength, "api_proto", Value,
                         sizeof(Value)))
    {
        // A value too long for http or https is neither.
        const size_t Length = strlen(Value);
        memcpy(Service->Proto, Length < sizeof(Service->Proto) ? Value : "unknown",
               Length < sizeof(Service->Proto) ? Length + 1 : sizeof("unknown"));
    }
    if (NmosDns_TxtValue(Record->Txt, Record->TxtLength, "api_ver", Value, sizeof(Value)))
    {
        snprintf(Service->Versions, sizeof(Service->Versions), "%s", Value);
    }
    if (NmosDns_TxtValue(Record->Txt, Record->TxtLength, "api_auth", Value,
                         sizeof(Value)))
    {
        Service->Auth = strcmp(Value, "true") == 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Collect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Receives answers until deadline_ms and gathers their records into the search they
// answer: that of the DNS server when they come from it with the ID of the query, and
// that of multicast DNS otherwise.
//
static void Collect(const DtNmosDiscoveryConfig* Config, NmosUdp* Udp,
                    NmosSearch* Searches, uint16_t Id, uint8_t* Buffer,
                    uint64_t DeadlineMs)
{
    NmosSearch* Unicast = &Searches[1];
    for (;;)
    {
        const uint64_t Now = NmosOs_MonotonicMs();
        if (Now >= DeadlineMs)
        {
            return;
        }
        char From[64];
        uint16_t FromPort = 0;
        const int Received =
            NmosOs_UdpReceive(Udp, Buffer, NMOS_MAX_MESSAGE, (uint32_t)(DeadlineMs - Now),
                              From, sizeof(From), &FromPort);
        if (Received < 0)
        {
            return;
        }
        if (Received == 0)
        {
            continue;
        }
        NmosSearch* Target = &Searches[0];
        if (Unicast->Active && FromPort == Unicast->Port &&
            strcmp(From, Unicast->Address) == 0)
        {
            uint16_t AnswerId = 0;
            unsigned Rcode = 0;
            if (!NmosDns_ReadHeader(Buffer, (size_t)Received, &AnswerId, &Rcode) ||
                AnswerId != Id)
            {
                LogMessage(Config, DTNMOS_LOG_DEBUG,
                           "An answer of the DNS server %s:%u is not to the query.", From,
                           FromPort);
                continue;
            }
            Unicast->Answered = true;
            if (Rcode != DTNMOS_DNS_NO_ERROR)
            {
                // 3 is a name the server does not know, 5 a query it refuses.
                LogMessage(Config, DTNMOS_LOG_DEBUG,
                           "The DNS server %s:%u answered with response code %u.", From,
                           FromPort, Rcode);
                continue;
            }
            Target = Unicast;
        }
        else if (!Searches[0].Active)
        {
            continue;
        }
        if (!NmosDns_ReadResponse(Buffer, (size_t)Received, TakeRecord, &Target->Found))
        {
            LogMessage(Config, DTNMOS_LOG_DEBUG,
                       "An answer from %s is not a DNS response.", From);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendMulticast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends a query of multicast DNS from each live interface of set; one it cannot be sent
// from is logged and left out of the queries that follow. Returns whether it went out.
//
static bool SendMulticast(const DtNmosDiscoveryConfig* Config, NmosUdp* Udp,
                          NmosInterfaceSet* Set, const NmosSearch* One, const void* Query,
                          size_t Length)
{
    bool Sent = false;
    for (size_t i = 0; i < Set->Count; ++i)
    {
        if (!Set->Live[i])
        {
            continue;
        }
        const char* Interface = Set->Addresses[i];
        if ((Interface[0] == '\0' || NmosOs_UdpSetInterface(Udp, Interface)) &&
            NmosOs_UdpSend(Udp, One->Address, One->Port, Query, Length))
        {
            Sent = true;
            continue;
        }
        if (Interface[0] != '\0')
        {
            LogMessage(Config, DTNMOS_LOG_WARNING,
                       "The query for %s could not be sent from %s; that interface is "
                       "left out.",
                       One->Service, Interface);
            Set->Live[i] = false;
        }
    }
    return Sent;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AskMissing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Asks one search for the records its answers left out: multicast DNS in one query, from
// each interface, and the DNS server one question per query, as it answers no more.
// Returns whether it asked.
//
static bool AskMissing(const DtNmosDiscoveryConfig* Config, NmosUdp* Udp,
                       NmosInterfaceSet* Set, const NmosSearch* One, uint16_t Id)
{
    const NmosGathered* Found = &One->Found;
    NmosDnsQuestion Missing[2 * NMOS_MAX_INSTANCES];
    size_t MissingCount = 0;
    for (size_t i = 0; i < Found->InstanceCount; ++i)
    {
        const NmosInstance* Service = &Found->Instances[i];
        if (!Service->HasSrv)
        {
            Missing[MissingCount++] =
                (NmosDnsQuestion){Service->Name, DTNMOS_DNS_TYPE_SRV};
        }
        if (!Service->HasTxt)
        {
            Missing[MissingCount++] =
                (NmosDnsQuestion){Service->Name, DTNMOS_DNS_TYPE_TXT};
        }
        else if (Service->HasSrv && HostNamed(Found, Service->Host) == NULL &&
                 MissingCount < 2 * NMOS_MAX_INSTANCES)
        {
            Missing[MissingCount++] = (NmosDnsQuestion){Service->Host, DTNMOS_DNS_TYPE_A};
        }
    }
    if (MissingCount == 0)
    {
        return false;
    }
    LogMessage(Config, DTNMOS_LOG_DEBUG, "Asking again for %zu records of %s.",
               MissingCount, One->Service);
    const size_t PerQuery = One->Kind == DTNMOS_SEARCH_UNICAST ? 1 : MissingCount;
    uint8_t* Query = malloc(NMOS_MAX_MESSAGE);
    bool Asked = false;
    for (size_t First = 0; Query != NULL && First < MissingCount; First += PerQuery)
    {
        const size_t Length = NmosDns_WriteQuery(Query, NMOS_MAX_MESSAGE, Id, One->Flags,
                                                 Missing + First, PerQuery);
        if (Length == 0)
        {
            continue;
        }
        Asked |= One->Kind == DTNMOS_SEARCH_MULTICAST
                     ? SendMulticast(Config, Udp, Set, One, Query, Length)
                     : NmosOs_UdpSend(Udp, One->Address, One->Port, Query, Length);
    }
    free(Query);
    return Asked;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InstanceLabel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the name of an instance as people read it: its first label, unescaped.
//
static void InstanceLabel(const char* Name, const char* Service, char* Label, size_t Size)
{
    const size_t Length = strlen(Name) - strlen(Service) - 1;
    size_t Used = 0;
    for (size_t i = 0; i < Length && Used + 1 < Size; ++i)
    {
        if (Name[i] == '\\' && i + 1 < Length)
        {
            ++i;
        }
        Label[Used++] = Name[i];
    }
    Label[Used] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether versions, e.g. "v1.2,v1.3", holds version.
//
static bool HasVersion(const char* Versions, const char* Version)
{
    const size_t Length = strlen(Version);
    for (const char* At = Versions; *At != '\0';)
    {
        const char* End = strchr(At, ',');
        const size_t Token = End != NULL ? (size_t)(End - At) : strlen(At);
        if (Token == Length && strncmp(At, Version, Length) == 0)
        {
            return true;
        }
        At += Token + (End != NULL ? 1 : 0);
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CompareRegistries -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int CompareRegistries(const void* a, const void* b)
{
    const DtNmosRegistryInfo* Left = a;
    const DtNmosRegistryInfo* Right = b;
    if (Left->Usable != Right->Usable)
    {
        return Left->Usable ? -1 : 1;
    }
    if ((Left->Priority < 0) != (Right->Priority < 0))
    {
        return Left->Priority < 0 ? 1 : -1;
    }
    if (Left->Priority != Right->Priority)
    {
        return Left->Priority < Right->Priority ? -1 : 1;
    }
    if (Left->FoundBy != Right->FoundBy)
    {
        return Left->FoundBy == DTNMOS_SEARCH_UNICAST ? -1 : 1;
    }
    return strcmp(Left->Instance, Right->Instance);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Describe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills registry from a complete instance, its strings that are no arrays owned by
// store; returns false when out of memory.
//
static bool Describe(const NmosGathered* Found, const NmosInstance* Service,
                     DtNmosService Kind, NmosStore* Store, DtNmosRegistryInfo* Registry)
{
    memset(Registry, 0, sizeof(*Registry));
    Registry->Service = Kind;
    Registry->Port = Service->Port;
    Registry->Priority = Service->Priority;
    Registry->Auth = Service->Auth;
    // IS-04 made api_proto required with v1.1; an announcement without it offers http.
    const char* Proto = Service->Proto[0] != '\0' ? Service->Proto : "http";
    const bool Https = strcmp(Proto, "https") == 0;
    Registry->Usable = (Https || strcmp(Proto, "http") == 0) && !Service->Auth &&
                       HasVersion(Service->Versions, "v1.3");

    char Label[DTNMOS_DNS_NAME_SIZE];
    InstanceLabel(Service->Name, Found->Service, Label, sizeof(Label));
    char Address[16] = "";
    const NmosHostAddress* Host = HostNamed(Found, Service->Host);
    if (Host != NULL)
    {
        snprintf(Address, sizeof(Address), "%u.%u.%u.%u", Host->Address[0],
                 Host->Address[1], Host->Address[2], Host->Address[3]);
    }
    // https checks the host name against the certificate, so it keeps the name.
    char Url[DTNMOS_DNS_NAME_SIZE + 32];
    snprintf(Url, sizeof(Url), "%s://%s:%u", Proto,
             Address[0] != '\0' && !Https ? Address : Service->Host, Service->Port);
    // The host is a DNS name, and the protocol is kept in 16 bytes, so both fit.
    snprintf(Registry->Host, sizeof(Registry->Host), "%s", Service->Host);
    snprintf(Registry->Address, sizeof(Registry->Address), "%s", Address);
    snprintf(Registry->ApiProto, sizeof(Registry->ApiProto), "%s", Proto);
    Registry->Instance = NmosStore_Text(Store, Label, strlen(Label));
    Registry->Url = NmosStore_Text(Store, Url, strlen(Url));
    Registry->ApiVersions =
        NmosStore_Text(Store, Service->Versions, strlen(Service->Versions));
    return Registry->Instance != NULL && Registry->Url != NULL &&
           Registry->ApiVersions != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsIpv4 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether text is an IPv4 address in dotted decimal.
//
static bool IsIpv4(const char* Text)
{
    NmosSpan Rest = NmosSpan_Of(Text);
    for (int Part = 0; Part < 4; ++Part)
    {
        NmosSpan Number;
        Rest = NmosSpan_Split(Rest, '.', &Number);
        uint32_t Value = 0;
        if (!NmosText_ParseU32(Number, 255, &Value) || (Part < 3) != (Rest.Data != NULL))
        {
            return false;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadDestination -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads "<IPv4 address>:<port>" into address and port; returns false when it is
// malformed.
//
static bool ReadDestination(const char* Text, char* Address, size_t Size, uint16_t* Port)
{
    const char* Colon = strrchr(Text, ':');
    if (Colon == NULL || (size_t)(Colon - Text) >= Size)
    {
        return false;
    }
    memcpy(Address, Text, (size_t)(Colon - Text));
    Address[Colon - Text] = '\0';
    if (!IsIpv4(Address))
    {
        return false;
    }
    uint32_t Value = 0;
    if (!NmosText_ParseU32(NmosSpan_Of(Colon + 1), 65535, &Value) || Value == 0)
    {
        return false;
    }
    *Port = (uint16_t)Value;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrepareUnicast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up the search of the DNS server, of the config or of the host; leaves it inactive
// when there is no server or no domain. Fails for a malformed server or domain.
//
static DtNmosResult PrepareUnicast(const DtNmosDiscoveryConfig* Config,
                                   const char* ServiceType, NmosSearch* Unicast)
{
    Unicast->Kind = DTNMOS_SEARCH_UNICAST;
    Unicast->Flags = DTNMOS_DNS_RECURSION_DESIRED;
    Unicast->Port = NMOS_DNS_PORT;
    if (Config->DnsServer != NULL &&
        !ReadDestination(Config->DnsServer, Unicast->Address, sizeof(Unicast->Address),
                         &Unicast->Port))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The DNS server %s is no <IPv4 address>:<port>.",
                              Config->DnsServer);
    }
    if (Config->Searches != 0 && (Config->Searches & DTNMOS_SEARCH_UNICAST) == 0)
    {
        return DTNMOS_OK;
    }
    char SystemServer[64] = "";
    char Domain[DTNMOS_DNS_NAME_SIZE] = "";
    if (Config->DnsServer == NULL || Config->DnsDomain == NULL)
    {
        NmosOs_SystemDns(SystemServer, sizeof(SystemServer), Domain, sizeof(Domain));
        if (Config->DnsServer == NULL)
        {
            snprintf(Unicast->Address, sizeof(Unicast->Address), "%s", SystemServer);
        }
    }
    if (Config->DnsDomain != NULL)
    {
        snprintf(Domain, sizeof(Domain), "%s", Config->DnsDomain);
    }
    size_t Length = strlen(Domain);
    if (Length > 1 && Domain[Length - 1] == '.')
    {
        Domain[--Length] = '\0';
    }
    if (Unicast->Address[0] == '\0' || Domain[0] == '\0')
    {
        LogMessage(Config, DTNMOS_LOG_DEBUG,
                   "The host names no DNS server or no domain; only multicast DNS is "
                   "asked.");
        return DTNMOS_OK;
    }
    // A name the query cannot hold is no domain.
    uint8_t Query[512];
    const NmosDnsQuestion Question = {Unicast->Service, DTNMOS_DNS_TYPE_PTR};
    if (snprintf(Unicast->Service, sizeof(Unicast->Service), "%s.%s", ServiceType,
                 Domain) >= (int)sizeof(Unicast->Service) ||
        NmosDns_WriteQuery(Query, sizeof(Query), 0, 0, &Question, 1) == 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The domain %s is no domain.",
                              Domain);
    }
    Unicast->Active = true;
    Unicast->Found.Service = Unicast->Service;
    LogMessage(Config, DTNMOS_LOG_DEBUG, "Asking the DNS server %s:%u for %s.",
               Unicast->Address, Unicast->Port, Unicast->Service);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListRegistries -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes the list of the complete instances that both searches found, sorted.
//
static DtNmosResult ListRegistries(const DtNmosDiscoveryConfig* Config,
                                   const NmosSearch* Searches, DtNmosRegistryList** List)
{
    const size_t Total =
        Searches[0].Found.InstanceCount + Searches[1].Found.InstanceCount;
    DtNmosRegistryList* ResultList = calloc(1, sizeof(*ResultList));
    if (ResultList == NULL ||
        (Total > 0 && (ResultList->Registries =
                           calloc(Total, sizeof(*ResultList->Registries))) == NULL))
    {
        free(ResultList);
        return NmosError_FailMemory();
    }
    for (int s = 0; s < 2; ++s)
    {
        const NmosGathered* Found = &Searches[s].Found;
        for (size_t i = 0; i < Found->InstanceCount; ++i)
        {
            const NmosInstance* Service = &Found->Instances[i];
            if (!Service->HasSrv)
            {
                LogMessage(Config, DTNMOS_LOG_DEBUG, "%s did not say where it is.",
                           Service->Name);
                continue;
            }
            DtNmosRegistryInfo* Registry = &ResultList->Registries[ResultList->Count];
            if (!Describe(Found, Service, Config->Service, &ResultList->Store, Registry))
            {
                DtNmosRegistryList_Free(ResultList);
                return NmosError_FailMemory();
            }
            Registry->FoundBy = Searches[s].Kind;
            ++ResultList->Count;
        }
    }
    // qsort() takes no null array, which an empty list has.
    if (ResultList->Count > 1)
    {
        qsort(ResultList->Registries, ResultList->Count, sizeof(*ResultList->Registries),
              CompareRegistries);
    }
    *List = ResultList;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddAskedOn -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds an address to set unless it has it already, or is full, which is logged.
//
static void AddAskedOn(const DtNmosDiscoveryConfig* Config, NmosInterfaceSet* Set,
                       const char* Address)
{
    for (size_t i = 0; i < Set->Count; ++i)
    {
        if (strcmp(Set->Addresses[i], Address) == 0)
        {
            return;
        }
    }
    if (Set->Count == NMOS_MAX_INTERFACES)
    {
        LogMessage(Config, DTNMOS_LOG_WARNING,
                   "Multicast DNS is asked on %d interfaces at most; %s is left out.",
                   (int)NMOS_MAX_INTERFACES, Address);
        return;
    }
    snprintf(Set->Addresses[Set->Count], sizeof(Set->Addresses[0]), "%s", Address);
    Set->Live[Set->Count++] = true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ChooseInterfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills set with the interfaces the config asks multicast DNS on. A config of 0.5.4,
// which ends before Interfaces, asks on one. Fails for a malformed address or choice.
//
static DtNmosResult ChooseInterfaces(const DtNmosDiscoveryConfig* Config,
                                     NmosInterfaceSet* Set)
{
    memset(Set, 0, sizeof(*Set));
    const bool HasChoice =
        Config->Size >=
        offsetof(DtNmosDiscoveryConfig, InterfaceCount) + sizeof(Config->InterfaceCount);
    const DtNmosInterfaces Which = HasChoice ? Config->Interfaces : DTNMOS_INTERFACES_ONE;
    if (Which == DTNMOS_INTERFACES_ONE)
    {
        if (Config->InterfaceAddress != NULL && !IsIpv4(Config->InterfaceAddress))
        {
            return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                  "The interface address %s is no IPv4 address.",
                                  Config->InterfaceAddress);
        }
        AddAskedOn(Config, Set,
                   Config->InterfaceAddress != NULL ? Config->InterfaceAddress : "");
        return DTNMOS_OK;
    }
    if (Which == DTNMOS_INTERFACES_LISTED)
    {
        if (Config->InterfaceAddresses == NULL || Config->InterfaceCount == 0)
        {
            return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                  "DTNMOS_INTERFACES_LISTED needs InterfaceAddresses.");
        }
        for (size_t i = 0; i < Config->InterfaceCount; ++i)
        {
            const char* Address = Config->InterfaceAddresses[i];
            if (Address == NULL || !IsIpv4(Address))
            {
                return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                      "The interface address %s is no IPv4 address.",
                                      Address != NULL ? Address : "(null)");
            }
            AddAskedOn(Config, Set, Address);
        }
        return DTNMOS_OK;
    }
    if (Which != DTNMOS_INTERFACES_ALL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "Interfaces is no DTNMOS_INTERFACES_* value.");
    }
    size_t Count = 0;
    NmosInterface* Interfaces = NmosOs_Interfaces(&Count);
    for (size_t i = 0; i < Count; ++i)
    {
        const char* Address = Interfaces[i].Address;
        if (IsIpv4(Address) && strncmp(Address, "127.", 4) != 0)
        {
            AddAskedOn(Config, Set, Address);
        }
    }
    free(Interfaces);
    if (Set->Count == 0)
    {
        // Without an interface of its own, the default route is all there is to try.
        LogMessage(Config, DTNMOS_LOG_DEBUG,
                   "The host has no IPv4 interface; the default route is asked.");
        AddAskedOn(Config, Set, "");
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoteLocalAddresses -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes into each registry that multicast DNS found with an address this host's address
// on the route to it.
//
static void NoteLocalAddresses(DtNmosRegistryList* List)
{
    for (size_t i = 0; i < List->Count; ++i)
    {
        DtNmosRegistryInfo* Registry = &List->Registries[i];
        if (Registry->FoundBy != DTNMOS_SEARCH_MULTICAST ||
            Registry->Address[0] == '\0' ||
            !NmosOs_AddressToward(Registry->Address, Registry->LocalAddress,
                                  sizeof(Registry->LocalAddress)))
        {
            Registry->LocalAddress[0] = '\0';
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_Discover -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_Discover(const DtNmosDiscoveryConfig* Config,
                             DtNmosRegistryList** List)
{
    if (List != NULL)
    {
        *List = NULL;
    }
    if (Config == NULL || List == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmos_Discover() needs a config and a list.");
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(Config, DtNmosDiscoveryConfig,
                                                 NMOS_DISCOVERY_CONFIG_SIZE_0_5_4);
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    if (Config->Service != DTNMOS_SERVICE_QUERY &&
        Config->Service != DTNMOS_SERVICE_REGISTRATION)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmos_Discover() needs a config of a known service.");
    }
    NmosSearch* Searches = calloc(2, sizeof(*Searches));
    uint8_t* Buffer = malloc(NMOS_MAX_MESSAGE);
    NmosInterfaceSet* Set = malloc(sizeof(*Set));
    if (Searches == NULL || Buffer == NULL || Set == NULL)
    {
        free(Searches);
        free(Buffer);
        free(Set);
        return NmosError_FailMemory();
    }
    const DtNmosResult Chosen = ChooseInterfaces(Config, Set);
    if (Chosen != DTNMOS_OK)
    {
        free(Searches);
        free(Buffer);
        free(Set);
        return Chosen;
    }
    const char* ServiceType = Config->Service == DTNMOS_SERVICE_QUERY
                                  ? "_nmos-query._tcp"
                                  : "_nmos-register._tcp";
    DtNmosResult Result = DTNMOS_OK;

    // The search of multicast DNS, in the domain local.
    NmosSearch* Multicast = &Searches[0];
    Multicast->Kind = DTNMOS_SEARCH_MULTICAST;
    Multicast->Active =
        Config->Searches == 0 || (Config->Searches & DTNMOS_SEARCH_MULTICAST) != 0;
    Multicast->Port = NMOS_MDNS_PORT;
    snprintf(Multicast->Address, sizeof(Multicast->Address), "%s", MdnsAddress);
    snprintf(Multicast->Service, sizeof(Multicast->Service), "%s.local", ServiceType);
    Multicast->Found.Service = Multicast->Service;
    if (Config->Destination != NULL &&
        !ReadDestination(Config->Destination, Multicast->Address,
                         sizeof(Multicast->Address), &Multicast->Port))
    {
        Result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                "The destination %s is no <IPv4 address>:<port>.",
                                Config->Destination);
    }
    if (Result == DTNMOS_OK)
    {
        Result = PrepareUnicast(Config, ServiceType, &Searches[1]);
    }
    // One socket for every interface: each query sets the interface it goes out from,
    // and the answers to all come back to its port. With one interface, it opens on it.
    const char* OpenOn =
        Set->Count == 1 && Set->Addresses[0][0] != '\0' ? Set->Addresses[0] : NULL;
    NmosUdp* Udp = NULL;
    if (Result == DTNMOS_OK && (Udp = NmosOs_UdpOpen(NULL, OpenOn)) == NULL)
    {
        Result = NmosError_Fail(
            DTNMOS_E_NETWORK, "No socket for multicast DNS could be opened%s%s.",
            OpenOn != NULL ? " on " : "", OpenOn != NULL ? OpenOn : "");
    }
    if (Result == DTNMOS_OK && Set->Count > 1)
    {
        LogMessage(Config, DTNMOS_LOG_DEBUG, "Asking multicast DNS on %zu interfaces.",
                   Set->Count);
    }
    const uint32_t Timeout =
        Config->TimeoutMs != 0 ? Config->TimeoutMs : NMOS_DEFAULT_TIMEOUT_MS;
    const uint16_t Id = (uint16_t)(NmosOs_MonotonicMs() | 1u);

    // The first query, three times within the timeout; to a DNS server till it answers.
    const uint64_t Start = NmosOs_MonotonicMs();
    for (int Send = 0; Result == DTNMOS_OK && Send < NMOS_SENDS; ++Send)
    {
        for (int s = 0; s < 2; ++s)
        {
            NmosSearch* One = &Searches[s];
            if (!One->Active || One->Answered)
            {
                continue;
            }
            uint8_t Query[512];
            const NmosDnsQuestion Question = {One->Service, DTNMOS_DNS_TYPE_PTR};
            const size_t Length =
                NmosDns_WriteQuery(Query, sizeof(Query), Id, One->Flags, &Question, 1);
            const bool Sent =
                One->Kind == DTNMOS_SEARCH_MULTICAST
                    ? SendMulticast(Config, Udp, Set, One, Query, Length)
                    : NmosOs_UdpSend(Udp, One->Address, One->Port, Query, Length);
            if (Sent || Send > 0)
            {
                continue;
            }
            if (One->Kind == DTNMOS_SEARCH_MULTICAST)
            {
                Result = NmosError_Fail(DTNMOS_E_NETWORK,
                                        "The query for %s could not be sent to %s:%u.",
                                        One->Service, One->Address, One->Port);
                break;
            }
            LogMessage(Config, DTNMOS_LOG_WARNING,
                       "The query for %s could not be sent to the DNS server %s:%u.",
                       One->Service, One->Address, One->Port);
            One->Active = false;
        }
        if (Result == DTNMOS_OK)
        {
            Collect(Config, Udp, Searches, Id, Buffer,
                    Start + (uint64_t)Timeout * (uint64_t)(Send + 1) / NMOS_SENDS);
        }
    }

    // One more query of each search for the records its answers left out.
    if (Result == DTNMOS_OK)
    {
        bool Asked = false;
        for (int s = 0; s < 2; ++s)
        {
            if (Searches[s].Active)
            {
                Asked |= AskMissing(Config, Udp, Set, &Searches[s], Id);
            }
        }
        if (Asked)
        {
            Collect(Config, Udp, Searches, Id, Buffer,
                    NmosOs_MonotonicMs() + Timeout / 2);
        }
    }
    NmosOs_UdpClose(Udp);

    if (Result == DTNMOS_OK)
    {
        Result = ListRegistries(Config, Searches, List);
    }
    if (Result == DTNMOS_OK)
    {
        NoteLocalAddresses(*List);
        LogMessage(Config, DTNMOS_LOG_INFO, "Found %zu instances of %s.",
                   DtNmosRegistryList_Count(*List), ServiceType);
    }
    free(Searches);
    free(Buffer);
    free(Set);
    return Result;
}

// A list of interfaces owns its entries.
struct DtNmosInterfaceList
{
    DtNmosInterfaceInfo* Interfaces;
    size_t Count;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_Interfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_Interfaces(DtNmosInterfaceList** List)
{
    if (List == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmos_Interfaces() needs a list.");
    }
    *List = NULL;
    size_t Count = 0;
    NmosInterface* Found = NmosOs_Interfaces(&Count);
    DtNmosInterfaceList* Result = calloc(1, sizeof(*Result));
    if (Result == NULL ||
        (Count > 0 &&
         (Result->Interfaces = calloc(Count, sizeof(*Result->Interfaces))) == NULL))
    {
        free(Found);
        free(Result);
        return NmosError_FailMemory();
    }
    for (size_t i = 0; i < Count; ++i)
    {
        if (!IsIpv4(Found[i].Address) || strncmp(Found[i].Address, "127.", 4) == 0)
        {
            continue;
        }
        DtNmosInterfaceInfo* Info = &Result->Interfaces[Result->Count++];
        snprintf(Info->Name, sizeof(Info->Name), "%s", Found[i].Name);
        snprintf(Info->Address, sizeof(Info->Address), "%s", Found[i].Address);
        snprintf(Info->PortId, sizeof(Info->PortId), "%s", Found[i].PortId);
    }
    free(Found);
    *List = Result;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosInterfaceList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtNmosInterfaceInfo* DtNmosInterfaceList_At(const DtNmosInterfaceList* List,
                                                  size_t Index)
{
    return List == NULL || Index >= List->Count ? NULL : &List->Interfaces[Index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosInterfaceList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtNmosInterfaceList_Count(const DtNmosInterfaceList* List)
{
    return List == NULL ? 0 : List->Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosInterfaceList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosInterfaceList_Free(DtNmosInterfaceList* List)
{
    if (List != NULL)
    {
        free(List->Interfaces);
        free(List);
    }
}
