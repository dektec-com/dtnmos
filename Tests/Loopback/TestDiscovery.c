// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestDiscovery.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of finding registries
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdlib.h>
#include <string.h>

#include "NmosDns.h"
#include "NmosOs.h"
#include "check.h"
#include "tests.h"

// A DNS message the test builds.
typedef struct NmosMessage
{
    uint8_t Bytes[1500];
    size_t Length;
} NmosMessage;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Put8(NmosMessage* m, unsigned Value)
{
    m->Bytes[m->Length++] = (uint8_t)Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Put16(NmosMessage* m, unsigned Value)
{
    Put8(m, Value >> 8);
    Put8(m, Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Put32(NmosMessage* m, uint32_t Value)
{
    Put16(m, (unsigned)(Value >> 16));
    Put16(m, (unsigned)Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the labels of name, split at every dot; returns where the name starts.
//
static size_t PutName(NmosMessage* m, const char* name)
{
    const size_t Start = m->Length;
    while (*name != '\0')
    {
        const char* End = strchr(name, '.');
        const size_t Length = End != NULL ? (size_t)(End - name) : strlen(name);
        Put8(m, (unsigned)Length);
        memcpy(m->Bytes + m->Length, name, Length);
        m->Length += Length;
        name += Length + (End != NULL ? 1 : 0);
    }
    Put8(m, 0);
    return Start;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutPointer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PutPointer(NmosMessage* m, size_t Offset)
{
    Put16(m, 0xC000u | (unsigned)Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BeginResponse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void BeginResponse(NmosMessage* m, unsigned Id, unsigned Answers,
                          unsigned Additional)
{
    m->Length = 0;
    Put16(m, Id);
    Put16(m, 0x8400); // a response, authoritative
    Put16(m, 0);
    Put16(m, Answers);
    Put16(m, 0);
    Put16(m, Additional);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutRecordHead -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the type, class, TTL and a placeholder for the length of the data of a record
// whose name is written; returns where the length goes.
//
static size_t PutRecordHead(NmosMessage* m, unsigned Type)
{
    Put16(m, Type);
    Put16(m, 1);
    Put32(m, 120);
    const size_t At = m->Length;
    Put16(m, 0);
    return At;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EndRecord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EndRecord(NmosMessage* m, size_t LengthAt)
{
    const size_t Length = m->Length - LengthAt - 2;
    m->Bytes[LengthAt] = (uint8_t)(Length >> 8);
    m->Bytes[LengthAt + 1] = (uint8_t)Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutTxt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The strings of a TXT record, in the data of the record.
//
static void PutTxt(NmosMessage* m, const char* const* Strings, size_t Count)
{
    for (size_t i = 0; i < Count; ++i)
    {
        Put8(m, (unsigned)strlen(Strings[i]));
        memcpy(m->Bytes + m->Length, Strings[i], strlen(Strings[i]));
        m->Length += strlen(Strings[i]);
    }
}

// An instance the responder announces.
typedef struct NmosAnnounced
{
    const char* Label; // e.g. "Registry B"
    const char* Host;  // e.g. "registry-b.local"
    unsigned Port;
    const char* Txt[4];
    size_t TxtCount;
    unsigned char Address[4];
} NmosAnnounced;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerWith -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the PTR, SRV, TXT and A records of the instances into an answer to id; with
// ptr_only, only the PTR records.
//
static void AnswerWith(NmosMessage* m, unsigned Id, const char* Service,
                       const NmosAnnounced* Instances, size_t Count, int PtrOnly)
{
    BeginResponse(m, Id, (unsigned)Count, PtrOnly ? 0 : (unsigned)(3 * Count));
    size_t ServiceAt = 0;
    size_t InstanceAt[8];
    for (size_t i = 0; i < Count; ++i)
    {
        // The first PTR writes the service in full; the others point to it.
        if (i == 0)
        {
            ServiceAt = PutName(m, Service);
        }
        else
        {
            PutPointer(m, ServiceAt);
        }
        const size_t LengthAt = PutRecordHead(m, DTNMOS_DNS_TYPE_PTR);
        InstanceAt[i] = m->Length;
        Put8(m, (unsigned)strlen(Instances[i].Label));
        memcpy(m->Bytes + m->Length, Instances[i].Label, strlen(Instances[i].Label));
        m->Length += strlen(Instances[i].Label);
        PutPointer(m, ServiceAt);
        EndRecord(m, LengthAt);
    }
    if (PtrOnly)
    {
        return;
    }
    for (size_t i = 0; i < Count; ++i)
    {
        PutPointer(m, InstanceAt[i]);
        size_t LengthAt = PutRecordHead(m, DTNMOS_DNS_TYPE_SRV);
        Put16(m, 0);
        Put16(m, 0);
        Put16(m, Instances[i].Port);
        const size_t HostAt = PutName(m, Instances[i].Host);
        EndRecord(m, LengthAt);

        PutPointer(m, InstanceAt[i]);
        LengthAt = PutRecordHead(m, DTNMOS_DNS_TYPE_TXT);
        PutTxt(m, Instances[i].Txt, Instances[i].TxtCount);
        EndRecord(m, LengthAt);

        PutPointer(m, HostAt);
        LengthAt = PutRecordHead(m, DTNMOS_DNS_TYPE_A);
        for (int b = 0; b < 4; ++b)
        {
            Put8(m, Instances[i].Address[b]);
        }
        EndRecord(m, LengthAt);
    }
}

// The records a read found.
typedef struct NmosRecords
{
    NmosDnsRecord Found[32];
    size_t Count;
} NmosRecords;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- KeepRecord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void KeepRecord(void* User, const NmosDnsRecord* Record)
{
    NmosRecords* Kept = User;
    if (Kept->Count < 32)
    {
        Kept->Found[Kept->Count++] = *Record;
    }
}

static const NmosAnnounced RegistryB = {
    "Registry B",
    "registry-b.local",
    8080,
    {"api_proto=http", "api_ver=v1.2,v1.3", "api_auth=false", "pri=10"},
    4,
    {127, 0, 0, 2}};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dns_writes_a_query -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dns_writes_a_query(void)
{
    uint8_t Buffer[512];
    const NmosDnsQuestion Question = {"_nmos-query._tcp.local", DTNMOS_DNS_TYPE_PTR};
    const size_t Length =
        NmosDns_WriteQuery(Buffer, sizeof(Buffer), 0x1234, 0, &Question, 1);
    static const uint8_t Expected[] = {
        0x12, 0x34, 0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   // header
        11,   '_',  'n', 'm', 'o', 's', '-', 'q', 'u', 'e', 'r', 'y', // _nmos-query
        4,    '_',  't', 'c', 'p', 5,   'l', 'o', 'c', 'a', 'l', 0,
        0,    12,   0,   1}; // PTR IN
    REQUIRE(Length == sizeof(Expected));
    CHECK(memcmp(Buffer, Expected, Length) == 0);
    CHECK_EQ(NmosDns_WriteQuery(Buffer, 20, 1, 0, &Question, 1), 0);
    const NmosDnsQuestion EmptyLabel = {"a..local", DTNMOS_DNS_TYPE_A};
    CHECK_EQ(NmosDns_WriteQuery(Buffer, sizeof(Buffer), 1, 0, &EmptyLabel, 1), 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dns_reads_records_and_compression -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dns_reads_records_and_compression(void)
{
    NmosMessage m;
    AnswerWith(&m, 7, "_nmos-query._tcp.local", &RegistryB, 1, 0);
    NmosRecords Kept;
    memset(&Kept, 0, sizeof(Kept));
    REQUIRE(NmosDns_ReadResponse(m.Bytes, m.Length, KeepRecord, &Kept));
    REQUIRE(Kept.Count == 4);
    CHECK_EQ(Kept.Found[0].Type, DTNMOS_DNS_TYPE_PTR);
    CHECK_STR(Kept.Found[0].name, "_nmos-query._tcp.local");
    CHECK_STR(Kept.Found[0].Target, "Registry B._nmos-query._tcp.local");
    CHECK_EQ(Kept.Found[1].Type, DTNMOS_DNS_TYPE_SRV);
    CHECK_STR(Kept.Found[1].name, "Registry B._nmos-query._tcp.local");
    CHECK_STR(Kept.Found[1].Target, "registry-b.local");
    CHECK_EQ(Kept.Found[1].Port, 8080);
    CHECK_EQ(Kept.Found[1].Ttl, 120);
    CHECK_EQ(Kept.Found[2].Type, DTNMOS_DNS_TYPE_TXT);
    char Value[32];
    REQUIRE(NmosDns_TxtValue(Kept.Found[2].Txt, Kept.Found[2].TxtLength, "API_VER", Value,
                             sizeof(Value)));
    CHECK_STR(Value, "v1.2,v1.3");
    REQUIRE(NmosDns_TxtValue(Kept.Found[2].Txt, Kept.Found[2].TxtLength, "pri", Value,
                             sizeof(Value)));
    CHECK_STR(Value, "10");
    CHECK(!NmosDns_TxtValue(Kept.Found[2].Txt, Kept.Found[2].TxtLength, "api", Value,
                            sizeof(Value)));
    CHECK(!NmosDns_TxtValue(Kept.Found[2].Txt, Kept.Found[2].TxtLength, "pri", Value, 2));
    CHECK_EQ(Kept.Found[3].Type, DTNMOS_DNS_TYPE_A);
    CHECK_STR(Kept.Found[3].name, "registry-b.local");
    CHECK_EQ(Kept.Found[3].Address[3], 2);

    // A key without "=" has an empty value.
    static const uint8_t Flag[] = {4, 'f', 'l', 'a', 'g'};
    REQUIRE(NmosDns_TxtValue(Flag, sizeof(Flag), "flag", Value, sizeof(Value)));
    CHECK_STR(Value, "");
    CHECK(NmosDns_SameName("Registry-B.LOCAL", "registry-b.local"));
    CHECK(!NmosDns_SameName("registry-b.local", "registry-b.local.x"));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dns_escapes_dots_within_labels -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dns_escapes_dots_within_labels(void)
{
    // An instance whose label holds a dot, "Registry v1.3".
    NmosMessage m;
    BeginResponse(&m, 1, 1, 0);
    const size_t ServiceAt = PutName(&m, "_nmos-query._tcp.local");
    const size_t LengthAt = PutRecordHead(&m, DTNMOS_DNS_TYPE_PTR);
    Put8(&m, 13);
    memcpy(m.Bytes + m.Length, "Registry v1.3", 13);
    m.Length += 13;
    PutPointer(&m, ServiceAt);
    EndRecord(&m, LengthAt);
    NmosRecords Kept;
    memset(&Kept, 0, sizeof(Kept));
    REQUIRE(NmosDns_ReadResponse(m.Bytes, m.Length, KeepRecord, &Kept));
    REQUIRE(Kept.Count == 1);
    CHECK_STR(Kept.Found[0].Target, "Registry v1\\.3._nmos-query._tcp.local");

    // Written back, the escaped dot stays within its label.
    uint8_t Buffer[128];
    const NmosDnsQuestion Question = {Kept.Found[0].Target, DTNMOS_DNS_TYPE_SRV};
    const size_t Length = NmosDns_WriteQuery(Buffer, sizeof(Buffer), 1, 0, &Question, 1);
    REQUIRE(Length > 12 + 14);
    CHECK_EQ(Buffer[12], 13);
    CHECK(memcmp(Buffer + 13, "Registry v1.3", 13) == 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dns_refuses_malformed_messages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dns_refuses_malformed_messages(void)
{
    NmosMessage m;
    AnswerWith(&m, 7, "_nmos-query._tcp.local", &RegistryB, 1, 0);
    NmosRecords Kept;
    // Every message cut short is refused, whatever it read before.
    for (size_t Length = 0; Length < m.Length; ++Length)
    {
        memset(&Kept, 0, sizeof(Kept));
        CHECK(!NmosDns_ReadResponse(m.Bytes, Length, KeepRecord, &Kept));
    }
    // A query is no response.
    NmosMessage Query = m;
    Query.Bytes[2] = 0;
    CHECK(!NmosDns_ReadResponse(Query.Bytes, Query.Length, KeepRecord, &Kept));
    // A name that points to itself.
    NmosMessage Loop;
    BeginResponse(&Loop, 1, 1, 0);
    PutPointer(&Loop, 12);
    PutRecordHead(&Loop, DTNMOS_DNS_TYPE_A);
    CHECK(!NmosDns_ReadResponse(Loop.Bytes, Loop.Length, KeepRecord, &Kept));
    // An A record of the wrong length.
    NmosMessage Wrong;
    BeginResponse(&Wrong, 1, 1, 0);
    PutName(&Wrong, "host.local");
    const size_t LengthAt = PutRecordHead(&Wrong, DTNMOS_DNS_TYPE_A);
    Put8(&Wrong, 1);
    EndRecord(&Wrong, LengthAt);
    CHECK(!NmosDns_ReadResponse(Wrong.Bytes, Wrong.Length, KeepRecord, &Kept));
}

// A responder on 127.0.0.1 that answers queries on a thread of its own.
typedef struct NmosResponder
{
    NmosUdp* Socket;
    NmosThread* Thread;
    NmosMutex* Mutex;
    int Stop;        // guarded by mutex
    int Queries;     // queries received, guarded by mutex
    int AskedSrvTxt; // a query asked for SRV and TXT records, guarded by mutex
    int Faults;      // queries a DNS server counts as faults, guarded by mutex
    // Set before the thread starts and only read by it afterwards.
    const NmosAnnounced* Instances;
    size_t Count;
    int Answer; // 0: none, 1: everything, 2: PTR first, the rest when asked
    char Destination[32];
    const char* Service; // that it announces
    // A DNS server, which counts a query without recursion desired or of more than one
    // question as a fault, and answers it with FORMERR.
    int Dns;
} NmosResponder;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadQuery -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the type of the first question of a query and whether any asks for SRV or TXT.
//
static int ReadQuery(const uint8_t* Bytes, size_t Length, int* AsksSrvTxt)
{
    if (Length < 12 || (Bytes[2] & 0x80) != 0)
    {
        return 0;
    }
    const unsigned Questions = ((unsigned)Bytes[4] << 8) | Bytes[5];
    size_t At = 12;
    *AsksSrvTxt = 0;
    for (unsigned q = 0; q < Questions; ++q)
    {
        while (At < Length && Bytes[At] != 0)
        {
            At += 1u + Bytes[At];
        }
        if (At + 5 > Length)
        {
            return 0;
        }
        const unsigned Type = ((unsigned)Bytes[At + 1] << 8) | Bytes[At + 2];
        *AsksSrvTxt |= Type == DTNMOS_DNS_TYPE_SRV || Type == DTNMOS_DNS_TYPE_TXT;
        At += 5;
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Respond -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Respond(void* Argument)
{
    NmosResponder* r = Argument;
    for (;;)
    {
        NmosOs_MutexLock(r->Mutex);
        const int Stop = r->Stop;
        NmosOs_MutexUnlock(r->Mutex);
        if (Stop)
        {
            return;
        }
        uint8_t Bytes[1500];
        char From[64];
        uint16_t FromPort = 0;
        const int Received = NmosOs_UdpReceive(r->Socket, Bytes, sizeof(Bytes), 50, From,
                                               sizeof(From), &FromPort);
        int AsksSrvTxt = 0;
        if (Received <= 0 || !ReadQuery(Bytes, (size_t)Received, &AsksSrvTxt))
        {
            continue;
        }
        const int Fault =
            r->Dns && ((Bytes[2] & 0x01) == 0 || Bytes[4] != 0 || Bytes[5] != 1);
        NmosOs_MutexLock(r->Mutex);
        ++r->Queries;
        r->AskedSrvTxt |= AsksSrvTxt;
        r->Faults += Fault;
        NmosOs_MutexUnlock(r->Mutex);
        if (Fault)
        {
            const uint8_t Formerr[12] = {Bytes[0], Bytes[1], 0x81, 0x01};
            NmosOs_UdpSend(r->Socket, From, FromPort, Formerr, sizeof(Formerr));
            continue;
        }
        if (r->Answer == 0)
        {
            continue;
        }
        // A one-shot query is answered with its ID, to the port it came from.
        NmosMessage m;
        const unsigned Id = ((unsigned)Bytes[0] << 8) | Bytes[1];
        AnswerWith(&m, Id, r->Service, r->Instances, r->Count,
                   r->Answer == 2 && !AsksSrvTxt);
        NmosOs_UdpSend(r->Socket, From, FromPort, m.Bytes, m.Length);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartResponderAs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Starts a responder that announces service, as a DNS server when dns is set. Everything
// the thread reads without the mutex is set before it starts.
//
static int StartResponderAs(NmosResponder* r, const NmosAnnounced* Instances,
                            size_t Count, int Answer, const char* Service, int Dns)
{
    memset(r, 0, sizeof(*r));
    r->Instances = Instances;
    r->Count = Count;
    r->Answer = Answer;
    r->Service = Service;
    r->Dns = Dns;
    r->Socket = NmosOs_UdpOpen("127.0.0.1", NULL);
    r->Mutex = NmosOs_MutexCreate();
    if (r->Socket == NULL || r->Mutex == NULL)
    {
        return 0;
    }
    snprintf(r->Destination, sizeof(r->Destination), "127.0.0.1:%u",
             NmosOs_UdpPort(r->Socket));
    r->Thread = NmosOs_ThreadStart(Respond, r);
    return r->Thread != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartResponder -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Starts a responder that announces "_nmos-query._tcp.local" over multicast DNS.
//
static int StartResponder(NmosResponder* r, const NmosAnnounced* Instances, size_t Count,
                          int Answer)
{
    return StartResponderAs(r, Instances, Count, Answer, "_nmos-query._tcp.local", 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopResponder -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void StopResponder(NmosResponder* r)
{
    if (r->Thread != NULL)
    {
        NmosOs_MutexLock(r->Mutex);
        r->Stop = 1;
        NmosOs_MutexUnlock(r->Mutex);
        NmosOs_ThreadJoin(r->Thread);
    }
    NmosOs_UdpClose(r->Socket);
    NmosOs_MutexFree(r->Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosDiscoveryConfig ConfigFor(const NmosResponder* r, uint32_t TimeoutMs)
{
    DtNmosDiscoveryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Service = DTNMOS_SERVICE_QUERY;
    Config.Destination = r->Destination;
    Config.Searches = DTNMOS_SEARCH_MULTICAST;
    Config.TimeoutMs = TimeoutMs;
    return Config;
}

// .-.-.-.-.-.-.-.-.-.-.- discovery_finds_registries_by_priority -.-.-.-.-.-.-.-.-.-.-.-.-
//
void discovery_finds_registries_by_priority(void)
{
    static const NmosAnnounced Instances[] = {
        {"Registry B",
         "registry-b.local",
         8080,
         {"api_proto=http", "api_ver=v1.2,v1.3", "api_auth=false", "pri=10"},
         4,
         {127, 0, 0, 2}},
        {"Registry A",
         "registry-a.local",
         443,
         {"api_proto=https", "api_ver=v1.3", "api_auth=false", "pri=0"},
         4,
         {127, 0, 0, 3}},
        {"Old",
         "old.local",
         80,
         {"api_proto=http", "api_ver=v1.0,v1.1,v1.2", "pri=0"},
         3,
         {127, 0, 0, 4}},
        {"No priority", "nopri.local", 8235, {"api_ver=v1.3"}, 1, {127, 0, 0, 5}},
    };
    NmosResponder r;
    REQUIRE(StartResponder(&r, Instances, 4, 1));
    const DtNmosDiscoveryConfig Config = ConfigFor(&r, 300);
    DtNmosRegistryList* List = NULL;
    const DtNmosResult Result = DtNmos_Discover(&Config, &List);
    StopResponder(&r);
    REQUIRE(Result == DTNMOS_OK);
    REQUIRE(DtNmosRegistryList_Count(List) == 4);
    const DtNmosRegistryInfo* First = DtNmosRegistryList_At(List, 0);
    CHECK_STR(First->Instance, "Registry A");
    // https keeps the host name, for its certificate.
    CHECK_STR(First->Url, "https://registry-a.local:443");
    CHECK_STR(First->Address, "127.0.0.3");
    CHECK_EQ(First->Priority, 0);
    CHECK(First->Usable);
    const DtNmosRegistryInfo* Second = DtNmosRegistryList_At(List, 1);
    CHECK_STR(Second->Instance, "Registry B");
    CHECK_STR(Second->Url, "http://127.0.0.2:8080");
    CHECK_STR(Second->ApiVersions, "v1.2,v1.3");
    CHECK_EQ(Second->Service, DTNMOS_SERVICE_QUERY);
    const DtNmosRegistryInfo* Third = DtNmosRegistryList_At(List, 2);
    CHECK_STR(Third->Instance, "No priority");
    CHECK_EQ(Third->Priority, -1);
    CHECK_STR(Third->ApiProto, "http");
    CHECK(Third->Usable);
    const DtNmosRegistryInfo* Last = DtNmosRegistryList_At(List, 3);
    CHECK_STR(Last->Instance, "Old");
    CHECK(!Last->Usable);
    CHECK(DtNmosRegistryList_At(List, 4) == NULL);

    // The arrays of an info are copied with it, and outlive the list.
    const DtNmosRegistryInfo Copy = *Second;
    DtNmosRegistryList_Free(List);
    CHECK_STR(Copy.Host, "registry-b.local");
    CHECK_STR(Copy.Address, "127.0.0.2");
}

// .-.-.-.-.-.-.-.-.-.-.- discovery_asks_again_for_what_is_missing -.-.-.-.-.-.-.-.-.-.-.-
//
void discovery_asks_again_for_what_is_missing(void)
{
    NmosResponder r;
    REQUIRE(StartResponder(&r, &RegistryB, 1, 2));
    const DtNmosDiscoveryConfig Config = ConfigFor(&r, 300);
    DtNmosRegistryList* List = NULL;
    const DtNmosResult Result = DtNmos_Discover(&Config, &List);
    NmosOs_MutexLock(r.Mutex);
    const int Queries = r.Queries;
    const int Asked = r.AskedSrvTxt;
    NmosOs_MutexUnlock(r.Mutex);
    StopResponder(&r);
    REQUIRE(Result == DTNMOS_OK);
    CHECK_EQ(Queries, 4); // three sends of the first query, and the one that asks again
    CHECK(Asked);
    REQUIRE(DtNmosRegistryList_Count(List) == 1);
    CHECK_STR(DtNmosRegistryList_At(List, 0)->Url, "http://127.0.0.2:8080");
    DtNmosRegistryList_Free(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.- discovery_finds_nothing_in_silence -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void discovery_finds_nothing_in_silence(void)
{
    NmosResponder r;
    REQUIRE(StartResponder(&r, NULL, 0, 0));
    const DtNmosDiscoveryConfig Config = ConfigFor(&r, 200);
    DtNmosRegistryList* List = NULL;
    const uint64_t Start = NmosOs_MonotonicMs();
    const DtNmosResult Result = DtNmos_Discover(&Config, &List);
    const uint64_t Took = NmosOs_MonotonicMs() - Start;
    StopResponder(&r);
    REQUIRE(Result == DTNMOS_OK);
    CHECK_EQ(DtNmosRegistryList_Count(List), 0);
    CHECK(Took >= 190 && Took < 2000);
    DtNmosRegistryList_Free(List);

    // What it refuses.
    DtNmosDiscoveryConfig Wrong = Config;
    Wrong.Destination = "224.0.0.251";
    CHECK(DtNmos_Discover(&Wrong, &List) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(List == NULL);
    Wrong.Destination = "no-address:5353";
    CHECK(DtNmos_Discover(&Wrong, &List) == DTNMOS_E_INVALID_ARGUMENT);
    Wrong.Destination = "127.0.0.1:0";
    CHECK(DtNmos_Discover(&Wrong, &List) == DTNMOS_E_INVALID_ARGUMENT);
    Wrong = Config;
    Wrong.InterfaceAddress = "10.0.0";
    CHECK(DtNmos_Discover(&Wrong, &List) == DTNMOS_E_INVALID_ARGUMENT);
    // An address that is no interface of this host cannot send.
    Wrong.InterfaceAddress = "192.0.2.1";
    CHECK(DtNmos_Discover(&Wrong, &List) == DTNMOS_E_NETWORK);
    CHECK(DtNmos_Discover(&Config, NULL) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dns_reads_resolv_conf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dns_reads_resolv_conf(void)
{
    char Server[64];
    char Domain[256];
    // The first IPv4 nameserver, and the first domain of the last search or domain line.
    NmosDns_ReadResolvConf("# written by DHCP\n"
                           "nameserver 2001:db8::1\n"
                           "nameserver 192.0.2.53\n"
                           "nameserver 192.0.2.54\n"
                           "domain first.example\n"
                           "search\tstudio.example. other.example\n"
                           "options edns0\n",
                           Server, sizeof(Server), Domain, sizeof(Domain));
    CHECK_STR(Server, "192.0.2.53");
    CHECK_STR(Domain, "studio.example");
    // No server, and the root, which is no domain.
    NmosDns_ReadResolvConf("search .\r\n", Server, sizeof(Server), Domain,
                           sizeof(Domain));
    CHECK_STR(Server, "");
    CHECK_STR(Domain, "");
    // Nothing that fits, and nothing at all.
    NmosDns_ReadResolvConf("nameserver 192.0.2.53\ndomain studio.example", Server, 8,
                           Domain, 8);
    CHECK_STR(Server, "");
    CHECK_STR(Domain, "");
    NmosDns_ReadResolvConf("", Server, sizeof(Server), Domain, sizeof(Domain));
    CHECK_STR(Server, "");
    // The query to a DNS server asks it to recurse.
    uint8_t Buffer[64];
    const NmosDnsQuestion Question = {"_nmos-query._tcp.studio.example",
                                      DTNMOS_DNS_TYPE_PTR};
    REQUIRE(NmosDns_WriteQuery(Buffer, sizeof(Buffer), 7, DTNMOS_DNS_RECURSION_DESIRED,
                               &Question, 1) > 0);
    CHECK_EQ(Buffer[2], 0x01);
    uint16_t Id = 0;
    unsigned Rcode = 9;
    const uint8_t Refused[12] = {0x12, 0x34, 0x81, 0x85};
    REQUIRE(NmosDns_ReadHeader(Refused, sizeof(Refused), &Id, &Rcode));
    CHECK_EQ(Id, 0x1234);
    CHECK_EQ(Rcode, 5);
    CHECK(!NmosDns_ReadHeader(Refused, 11, &Id, &Rcode));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- discovery_asks_a_dns_server_too -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Multicast DNS and a DNS server announce a registry each of the same priority: both
// come back, the one of the DNS server first, which the DNS server gave the PTR record
// first, and its other records one question at a time.
//
void discovery_asks_a_dns_server_too(void)
{
    static const NmosAnnounced OnLink = {
        "On link", "on-link.local", 8080, {"api_proto=http", "api_ver=v1.3", "pri=10"},
        3,         {127, 0, 0, 2}};
    static const NmosAnnounced InDns[] = {
        {"Studio",
         "registry.studio.example",
         8081,
         {"api_proto=http", "api_ver=v1.3", "pri=10"},
         3,
         {127, 0, 0, 3}},
        {"Backup",
         "backup.studio.example",
         8082,
         {"api_proto=http", "api_ver=v1.3", "pri=20"},
         3,
         {127, 0, 0, 4}},
    };
    NmosResponder Mdns;
    REQUIRE(StartResponder(&Mdns, &OnLink, 1, 1));
    NmosResponder Dns;
    REQUIRE(StartResponderAs(&Dns, InDns, 2, 2, "_nmos-query._tcp.studio.example", 1));

    DtNmosDiscoveryConfig Config = ConfigFor(&Mdns, 300);
    Config.Searches = 0;
    Config.DnsServer = Dns.Destination;
    Config.DnsDomain = "studio.example.";
    DtNmosRegistryList* List = NULL;
    const DtNmosResult Result = DtNmos_Discover(&Config, &List);
    NmosOs_MutexLock(Dns.Mutex);
    const int Faults = Dns.Faults;
    const int Asked = Dns.AskedSrvTxt;
    NmosOs_MutexUnlock(Dns.Mutex);
    StopResponder(&Dns);
    StopResponder(&Mdns);
    REQUIRE(Result == DTNMOS_OK);
    CHECK_EQ(Faults, 0);
    CHECK(Asked);
    REQUIRE(DtNmosRegistryList_Count(List) == 3);
    const DtNmosRegistryInfo* First = DtNmosRegistryList_At(List, 0);
    CHECK_STR(First->Instance, "Studio");
    CHECK_STR(First->Url, "http://127.0.0.3:8081");
    CHECK_EQ(First->FoundBy, DTNMOS_SEARCH_UNICAST);
    const DtNmosRegistryInfo* Second = DtNmosRegistryList_At(List, 1);
    CHECK_STR(Second->Instance, "On link");
    CHECK_EQ(Second->FoundBy, DTNMOS_SEARCH_MULTICAST);
    const DtNmosRegistryInfo* Third = DtNmosRegistryList_At(List, 2);
    CHECK_STR(Third->Instance, "Backup");
    CHECK_STR(Third->Host, "backup.studio.example");
    CHECK_EQ(Third->FoundBy, DTNMOS_SEARCH_UNICAST);
    DtNmosRegistryList_Free(List);

    // What it refuses.
    Config.DnsServer = "no-address:53";
    CHECK(DtNmos_Discover(&Config, &List) == DTNMOS_E_INVALID_ARGUMENT);
    Config.DnsServer = "127.0.0.1:53";
    Config.DnsDomain = "studio..example";
    CHECK(DtNmos_Discover(&Config, &List) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "studio..example") != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.- discovery_takes_only_the_dns_server -.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only the unicast search: the multicast responder is not asked, and records of another
// domain than the one asked for are not taken.
//
void discovery_takes_only_the_dns_server(void)
{
    NmosResponder Mdns;
    REQUIRE(StartResponder(&Mdns, &RegistryB, 1, 1));
    NmosResponder Dns;
    REQUIRE(
        StartResponderAs(&Dns, &RegistryB, 1, 1, "_nmos-query._tcp.studio.example", 1));
    DtNmosDiscoveryConfig Config = ConfigFor(&Mdns, 200);
    Config.Searches = DTNMOS_SEARCH_UNICAST;
    Config.DnsServer = Dns.Destination;
    Config.DnsDomain = "studio.example";
    DtNmosRegistryList* List = NULL;
    REQUIRE(DtNmos_Discover(&Config, &List) == DTNMOS_OK);
    NmosOs_MutexLock(Mdns.Mutex);
    const int MulticastQueries = Mdns.Queries;
    NmosOs_MutexUnlock(Mdns.Mutex);
    NmosOs_MutexLock(Dns.Mutex);
    const int DnsQueries = Dns.Queries;
    NmosOs_MutexUnlock(Dns.Mutex);
    CHECK_EQ(MulticastQueries, 0);
    // It answered the first query at once, so that was sent once.
    CHECK_EQ(DnsQueries, 1);
    REQUIRE(DtNmosRegistryList_Count(List) == 1);
    CHECK_EQ(DtNmosRegistryList_At(List, 0)->FoundBy, DTNMOS_SEARCH_UNICAST);
    DtNmosRegistryList_Free(List);

    // Records of another domain than the one asked for are not taken.
    Config.DnsDomain = "elsewhere.example";
    REQUIRE(DtNmos_Discover(&Config, &List) == DTNMOS_OK);
    CHECK_EQ(DtNmosRegistryList_Count(List), 0);
    DtNmosRegistryList_Free(List);
    StopResponder(&Dns);
    StopResponder(&Mdns);
}
