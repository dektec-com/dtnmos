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
typedef struct message
{
    uint8_t bytes[1500];
    size_t length;
} message;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void put8(message* m, unsigned value)
{
    m->bytes[m->length++] = (uint8_t)value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void put16(message* m, unsigned value)
{
    put8(m, value >> 8);
    put8(m, value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void put32(message* m, uint32_t value)
{
    put16(m, (unsigned)(value >> 16));
    put16(m, (unsigned)value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put_name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the labels of name, split at every dot; returns where the name starts.
//
static size_t put_name(message* m, const char* name)
{
    const size_t start = m->length;
    while (*name != '\0')
    {
        const char* end = strchr(name, '.');
        const size_t length = end != NULL ? (size_t)(end - name) : strlen(name);
        put8(m, (unsigned)length);
        memcpy(m->bytes + m->length, name, length);
        m->length += length;
        name += length + (end != NULL ? 1 : 0);
    }
    put8(m, 0);
    return start;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put_pointer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void put_pointer(message* m, size_t offset)
{
    put16(m, 0xC000u | (unsigned)offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- begin_response -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void begin_response(message* m, unsigned id, unsigned answers, unsigned additional)
{
    m->length = 0;
    put16(m, id);
    put16(m, 0x8400); // a response, authoritative
    put16(m, 0);
    put16(m, answers);
    put16(m, 0);
    put16(m, additional);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put_record_head -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the type, class, TTL and a placeholder for the length of the data of a record
// whose name is written; returns where the length goes.
//
static size_t put_record_head(message* m, unsigned type)
{
    put16(m, type);
    put16(m, 1);
    put32(m, 120);
    const size_t at = m->length;
    put16(m, 0);
    return at;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- end_record -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void end_record(message* m, size_t length_at)
{
    const size_t length = m->length - length_at - 2;
    m->bytes[length_at] = (uint8_t)(length >> 8);
    m->bytes[length_at + 1] = (uint8_t)length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put_txt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The strings of a TXT record, in the data of the record.
//
static void put_txt(message* m, const char* const* strings, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        put8(m, (unsigned)strlen(strings[i]));
        memcpy(m->bytes + m->length, strings[i], strlen(strings[i]));
        m->length += strlen(strings[i]);
    }
}

// An instance the responder announces.
typedef struct announced
{
    const char* label; // e.g. "Registry B"
    const char* host;  // e.g. "registry-b.local"
    unsigned port;
    const char* txt[4];
    size_t txt_count;
    unsigned char address[4];
} announced;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_with -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the PTR, SRV, TXT and A records of the instances into an answer to id; with
// ptr_only, only the PTR records.
//
static void answer_with(message* m, unsigned id, const char* service,
                        const announced* instances, size_t count, int ptr_only)
{
    begin_response(m, id, (unsigned)count, ptr_only ? 0 : (unsigned)(3 * count));
    size_t service_at = 0;
    size_t instance_at[8];
    for (size_t i = 0; i < count; ++i)
    {
        // The first PTR writes the service in full; the others point to it.
        if (i == 0)
        {
            service_at = put_name(m, service);
        }
        else
        {
            put_pointer(m, service_at);
        }
        const size_t length_at = put_record_head(m, DTNMOS_DNS_TYPE_PTR);
        instance_at[i] = m->length;
        put8(m, (unsigned)strlen(instances[i].label));
        memcpy(m->bytes + m->length, instances[i].label, strlen(instances[i].label));
        m->length += strlen(instances[i].label);
        put_pointer(m, service_at);
        end_record(m, length_at);
    }
    if (ptr_only)
    {
        return;
    }
    for (size_t i = 0; i < count; ++i)
    {
        put_pointer(m, instance_at[i]);
        size_t length_at = put_record_head(m, DTNMOS_DNS_TYPE_SRV);
        put16(m, 0);
        put16(m, 0);
        put16(m, instances[i].port);
        const size_t host_at = put_name(m, instances[i].host);
        end_record(m, length_at);

        put_pointer(m, instance_at[i]);
        length_at = put_record_head(m, DTNMOS_DNS_TYPE_TXT);
        put_txt(m, instances[i].txt, instances[i].txt_count);
        end_record(m, length_at);

        put_pointer(m, host_at);
        length_at = put_record_head(m, DTNMOS_DNS_TYPE_A);
        for (int b = 0; b < 4; ++b)
        {
            put8(m, instances[i].address[b]);
        }
        end_record(m, length_at);
    }
}

// The records a read found.
typedef struct records
{
    dtnmos_dns_record found[32];
    size_t count;
} records;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- keep_record -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void keep_record(void* user, const dtnmos_dns_record* record)
{
    records* kept = user;
    if (kept->count < 32)
    {
        kept->found[kept->count++] = *record;
    }
}

static const announced registry_b = {
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
    uint8_t buffer[512];
    const dtnmos_dns_question question = {"_nmos-query._tcp.local", DTNMOS_DNS_TYPE_PTR};
    const size_t length =
        dtnmos_dns_write_query(buffer, sizeof(buffer), 0x1234, 0, &question, 1);
    static const uint8_t expected[] = {
        0x12, 0x34, 0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   // header
        11,   '_',  'n', 'm', 'o', 's', '-', 'q', 'u', 'e', 'r', 'y', // _nmos-query
        4,    '_',  't', 'c', 'p', 5,   'l', 'o', 'c', 'a', 'l', 0,
        0,    12,   0,   1}; // PTR IN
    REQUIRE(length == sizeof(expected));
    CHECK(memcmp(buffer, expected, length) == 0);
    CHECK_EQ(dtnmos_dns_write_query(buffer, 20, 1, 0, &question, 1), 0);
    const dtnmos_dns_question empty_label = {"a..local", DTNMOS_DNS_TYPE_A};
    CHECK_EQ(dtnmos_dns_write_query(buffer, sizeof(buffer), 1, 0, &empty_label, 1), 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dns_reads_records_and_compression -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dns_reads_records_and_compression(void)
{
    message m;
    answer_with(&m, 7, "_nmos-query._tcp.local", &registry_b, 1, 0);
    records kept;
    memset(&kept, 0, sizeof(kept));
    REQUIRE(dtnmos_dns_read_response(m.bytes, m.length, keep_record, &kept));
    REQUIRE(kept.count == 4);
    CHECK_EQ(kept.found[0].type, DTNMOS_DNS_TYPE_PTR);
    CHECK_STR(kept.found[0].name, "_nmos-query._tcp.local");
    CHECK_STR(kept.found[0].target, "Registry B._nmos-query._tcp.local");
    CHECK_EQ(kept.found[1].type, DTNMOS_DNS_TYPE_SRV);
    CHECK_STR(kept.found[1].name, "Registry B._nmos-query._tcp.local");
    CHECK_STR(kept.found[1].target, "registry-b.local");
    CHECK_EQ(kept.found[1].port, 8080);
    CHECK_EQ(kept.found[1].ttl, 120);
    CHECK_EQ(kept.found[2].type, DTNMOS_DNS_TYPE_TXT);
    char value[32];
    REQUIRE(dtnmos_dns_txt_value(kept.found[2].txt, kept.found[2].txt_length, "API_VER",
                                 value, sizeof(value)));
    CHECK_STR(value, "v1.2,v1.3");
    REQUIRE(dtnmos_dns_txt_value(kept.found[2].txt, kept.found[2].txt_length, "pri",
                                 value, sizeof(value)));
    CHECK_STR(value, "10");
    CHECK(!dtnmos_dns_txt_value(kept.found[2].txt, kept.found[2].txt_length, "api", value,
                                sizeof(value)));
    CHECK(!dtnmos_dns_txt_value(kept.found[2].txt, kept.found[2].txt_length, "pri", value,
                                2));
    CHECK_EQ(kept.found[3].type, DTNMOS_DNS_TYPE_A);
    CHECK_STR(kept.found[3].name, "registry-b.local");
    CHECK_EQ(kept.found[3].address[3], 2);

    // A key without "=" has an empty value.
    static const uint8_t flag[] = {4, 'f', 'l', 'a', 'g'};
    REQUIRE(dtnmos_dns_txt_value(flag, sizeof(flag), "flag", value, sizeof(value)));
    CHECK_STR(value, "");
    CHECK(dtnmos_dns_same_name("Registry-B.LOCAL", "registry-b.local"));
    CHECK(!dtnmos_dns_same_name("registry-b.local", "registry-b.local.x"));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dns_escapes_dots_within_labels -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dns_escapes_dots_within_labels(void)
{
    // An instance whose label holds a dot, "Registry v1.3".
    message m;
    begin_response(&m, 1, 1, 0);
    const size_t service_at = put_name(&m, "_nmos-query._tcp.local");
    const size_t length_at = put_record_head(&m, DTNMOS_DNS_TYPE_PTR);
    put8(&m, 13);
    memcpy(m.bytes + m.length, "Registry v1.3", 13);
    m.length += 13;
    put_pointer(&m, service_at);
    end_record(&m, length_at);
    records kept;
    memset(&kept, 0, sizeof(kept));
    REQUIRE(dtnmos_dns_read_response(m.bytes, m.length, keep_record, &kept));
    REQUIRE(kept.count == 1);
    CHECK_STR(kept.found[0].target, "Registry v1\\.3._nmos-query._tcp.local");

    // Written back, the escaped dot stays within its label.
    uint8_t buffer[128];
    const dtnmos_dns_question question = {kept.found[0].target, DTNMOS_DNS_TYPE_SRV};
    const size_t length =
        dtnmos_dns_write_query(buffer, sizeof(buffer), 1, 0, &question, 1);
    REQUIRE(length > 12 + 14);
    CHECK_EQ(buffer[12], 13);
    CHECK(memcmp(buffer + 13, "Registry v1.3", 13) == 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dns_refuses_malformed_messages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dns_refuses_malformed_messages(void)
{
    message m;
    answer_with(&m, 7, "_nmos-query._tcp.local", &registry_b, 1, 0);
    records kept;
    // Every message cut short is refused, whatever it read before.
    for (size_t length = 0; length < m.length; ++length)
    {
        memset(&kept, 0, sizeof(kept));
        CHECK(!dtnmos_dns_read_response(m.bytes, length, keep_record, &kept));
    }
    // A query is no response.
    message query = m;
    query.bytes[2] = 0;
    CHECK(!dtnmos_dns_read_response(query.bytes, query.length, keep_record, &kept));
    // A name that points to itself.
    message loop;
    begin_response(&loop, 1, 1, 0);
    put_pointer(&loop, 12);
    put_record_head(&loop, DTNMOS_DNS_TYPE_A);
    CHECK(!dtnmos_dns_read_response(loop.bytes, loop.length, keep_record, &kept));
    // An A record of the wrong length.
    message wrong;
    begin_response(&wrong, 1, 1, 0);
    put_name(&wrong, "host.local");
    const size_t length_at = put_record_head(&wrong, DTNMOS_DNS_TYPE_A);
    put8(&wrong, 1);
    end_record(&wrong, length_at);
    CHECK(!dtnmos_dns_read_response(wrong.bytes, wrong.length, keep_record, &kept));
}

// A responder on 127.0.0.1 that answers queries on a thread of its own.
typedef struct responder
{
    dtnmos_udp* socket;
    dtnmos_thread* thread;
    dtnmos_mutex* mutex;
    int stop;          // guarded by mutex
    int queries;       // queries received, guarded by mutex
    int asked_srv_txt; // a query asked for SRV and TXT records, guarded by mutex
    int faults;        // queries a DNS server counts as faults, guarded by mutex
    // Set before the thread starts and only read by it afterwards.
    const announced* instances;
    size_t count;
    int answer; // 0: none, 1: everything, 2: PTR first, the rest when asked
    char destination[32];
    const char* service; // that it announces
    // A DNS server, which counts a query without recursion desired or of more than one
    // question as a fault, and answers it with FORMERR.
    int dns;
} responder;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_query -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the type of the first question of a query and whether any asks for SRV or TXT.
//
static int read_query(const uint8_t* bytes, size_t length, int* asks_srv_txt)
{
    if (length < 12 || (bytes[2] & 0x80) != 0)
    {
        return 0;
    }
    const unsigned questions = ((unsigned)bytes[4] << 8) | bytes[5];
    size_t at = 12;
    *asks_srv_txt = 0;
    for (unsigned q = 0; q < questions; ++q)
    {
        while (at < length && bytes[at] != 0)
        {
            at += 1u + bytes[at];
        }
        if (at + 5 > length)
        {
            return 0;
        }
        const unsigned type = ((unsigned)bytes[at + 1] << 8) | bytes[at + 2];
        *asks_srv_txt |= type == DTNMOS_DNS_TYPE_SRV || type == DTNMOS_DNS_TYPE_TXT;
        at += 5;
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- respond -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void respond(void* argument)
{
    responder* r = argument;
    for (;;)
    {
        dtnmos_mutex_lock(r->mutex);
        const int stop = r->stop;
        dtnmos_mutex_unlock(r->mutex);
        if (stop)
        {
            return;
        }
        uint8_t bytes[1500];
        char from[64];
        uint16_t from_port = 0;
        const int received = dtnmos_udp_receive(r->socket, bytes, sizeof(bytes), 50, from,
                                                sizeof(from), &from_port);
        int asks_srv_txt = 0;
        if (received <= 0 || !read_query(bytes, (size_t)received, &asks_srv_txt))
        {
            continue;
        }
        const int fault =
            r->dns && ((bytes[2] & 0x01) == 0 || bytes[4] != 0 || bytes[5] != 1);
        dtnmos_mutex_lock(r->mutex);
        ++r->queries;
        r->asked_srv_txt |= asks_srv_txt;
        r->faults += fault;
        dtnmos_mutex_unlock(r->mutex);
        if (fault)
        {
            const uint8_t formerr[12] = {bytes[0], bytes[1], 0x81, 0x01};
            dtnmos_udp_send(r->socket, from, from_port, formerr, sizeof(formerr));
            continue;
        }
        if (r->answer == 0)
        {
            continue;
        }
        // A one-shot query is answered with its ID, to the port it came from.
        message m;
        const unsigned id = ((unsigned)bytes[0] << 8) | bytes[1];
        answer_with(&m, id, r->service, r->instances, r->count,
                    r->answer == 2 && !asks_srv_txt);
        dtnmos_udp_send(r->socket, from, from_port, m.bytes, m.length);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- start_responder_as -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Starts a responder that announces service, as a DNS server when dns is set. Everything
// the thread reads without the mutex is set before it starts.
//
static int start_responder_as(responder* r, const announced* instances, size_t count,
                              int answer, const char* service, int dns)
{
    memset(r, 0, sizeof(*r));
    r->instances = instances;
    r->count = count;
    r->answer = answer;
    r->service = service;
    r->dns = dns;
    r->socket = dtnmos_udp_open("127.0.0.1", NULL);
    r->mutex = dtnmos_mutex_create();
    if (r->socket == NULL || r->mutex == NULL)
    {
        return 0;
    }
    snprintf(r->destination, sizeof(r->destination), "127.0.0.1:%u",
             dtnmos_udp_port(r->socket));
    r->thread = dtnmos_thread_start(respond, r);
    return r->thread != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- start_responder -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Starts a responder that announces "_nmos-query._tcp.local" over multicast DNS.
//
static int start_responder(responder* r, const announced* instances, size_t count,
                           int answer)
{
    return start_responder_as(r, instances, count, answer, "_nmos-query._tcp.local", 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- stop_responder -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void stop_responder(responder* r)
{
    if (r->thread != NULL)
    {
        dtnmos_mutex_lock(r->mutex);
        r->stop = 1;
        dtnmos_mutex_unlock(r->mutex);
        dtnmos_thread_join(r->thread);
    }
    dtnmos_udp_close(r->socket);
    dtnmos_mutex_free(r->mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- config_for -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosDiscoveryConfig config_for(const responder* r, uint32_t timeout_ms)
{
    DtNmosDiscoveryConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.Service = DTNMOS_SERVICE_QUERY;
    config.Destination = r->destination;
    config.Searches = DTNMOS_SEARCH_MULTICAST;
    config.TimeoutMs = timeout_ms;
    return config;
}

// .-.-.-.-.-.-.-.-.-.-.- discovery_finds_registries_by_priority -.-.-.-.-.-.-.-.-.-.-.-.-
//
void discovery_finds_registries_by_priority(void)
{
    static const announced instances[] = {
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
    responder r;
    REQUIRE(start_responder(&r, instances, 4, 1));
    const DtNmosDiscoveryConfig config = config_for(&r, 300);
    DtNmosRegistryList* list = NULL;
    DtNmosError error = {DTNMOS_OK, ""};
    const DtNmosResult result = DtNmos_Discover(&config, &list, &error);
    stop_responder(&r);
    REQUIRE(result == DTNMOS_OK);
    REQUIRE(DtNmosRegistryList_Count(list) == 4);
    const DtNmosRegistryInfo* first = DtNmosRegistryList_At(list, 0);
    CHECK_STR(DtNmosString_Get(&first->Instance), "Registry A");
    // https keeps the host name, for its certificate.
    CHECK_STR(DtNmosString_Get(&first->Url), "https://registry-a.local:443");
    CHECK_STR(DtNmosString_Get(&first->Address), "127.0.0.3");
    CHECK_EQ(first->Priority, 0);
    CHECK(first->Usable);
    const DtNmosRegistryInfo* second = DtNmosRegistryList_At(list, 1);
    CHECK_STR(DtNmosString_Get(&second->Instance), "Registry B");
    CHECK_STR(DtNmosString_Get(&second->Url), "http://127.0.0.2:8080");
    CHECK_STR(DtNmosString_Get(&second->ApiVersions), "v1.2,v1.3");
    CHECK_EQ(second->Service, DTNMOS_SERVICE_QUERY);
    const DtNmosRegistryInfo* third = DtNmosRegistryList_At(list, 2);
    CHECK_STR(DtNmosString_Get(&third->Instance), "No priority");
    CHECK_EQ(third->Priority, -1);
    CHECK_STR(DtNmosString_Get(&third->ApiProto), "http");
    CHECK(third->Usable);
    const DtNmosRegistryInfo* last = DtNmosRegistryList_At(list, 3);
    CHECK_STR(DtNmosString_Get(&last->Instance), "Old");
    CHECK(!last->Usable);
    CHECK(DtNmosRegistryList_At(list, 4) == NULL);

    DtNmosRegistryInfo copy;
    memset(&copy, 0, sizeof(copy));
    REQUIRE(DtNmosRegistryInfo_Copy(&copy, second) == DTNMOS_OK);
    DtNmosRegistryList_Free(list);
    CHECK_STR(DtNmosString_Get(&copy.Host), "registry-b.local");
    DtNmosRegistryInfo_Clear(&copy);
}

// .-.-.-.-.-.-.-.-.-.-.- discovery_asks_again_for_what_is_missing -.-.-.-.-.-.-.-.-.-.-.-
//
void discovery_asks_again_for_what_is_missing(void)
{
    responder r;
    REQUIRE(start_responder(&r, &registry_b, 1, 2));
    const DtNmosDiscoveryConfig config = config_for(&r, 300);
    DtNmosRegistryList* list = NULL;
    const DtNmosResult result = DtNmos_Discover(&config, &list, NULL);
    dtnmos_mutex_lock(r.mutex);
    const int queries = r.queries;
    const int asked = r.asked_srv_txt;
    dtnmos_mutex_unlock(r.mutex);
    stop_responder(&r);
    REQUIRE(result == DTNMOS_OK);
    CHECK_EQ(queries, 4); // three sends of the first query, and the one that asks again
    CHECK(asked);
    REQUIRE(DtNmosRegistryList_Count(list) == 1);
    CHECK_STR(DtNmosString_Get(&DtNmosRegistryList_At(list, 0)->Url),
              "http://127.0.0.2:8080");
    DtNmosRegistryList_Free(list);
}

// .-.-.-.-.-.-.-.-.-.-.-.- discovery_finds_nothing_in_silence -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void discovery_finds_nothing_in_silence(void)
{
    responder r;
    REQUIRE(start_responder(&r, NULL, 0, 0));
    const DtNmosDiscoveryConfig config = config_for(&r, 200);
    DtNmosRegistryList* list = NULL;
    const uint64_t start = dtnmos_monotonic_ms();
    const DtNmosResult result = DtNmos_Discover(&config, &list, NULL);
    const uint64_t took = dtnmos_monotonic_ms() - start;
    stop_responder(&r);
    REQUIRE(result == DTNMOS_OK);
    CHECK_EQ(DtNmosRegistryList_Count(list), 0);
    CHECK(took >= 190 && took < 2000);
    DtNmosRegistryList_Free(list);

    // What it refuses.
    DtNmosDiscoveryConfig wrong = config;
    wrong.Destination = "224.0.0.251";
    DtNmosError error = {DTNMOS_OK, ""};
    CHECK(DtNmos_Discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(list == NULL);
    wrong.Destination = "no-address:5353";
    CHECK(DtNmos_Discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
    wrong.Destination = "127.0.0.1:0";
    CHECK(DtNmos_Discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
    wrong = config;
    wrong.InterfaceAddress = "10.0.0";
    CHECK(DtNmos_Discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
    // An address that is no interface of this host cannot send.
    wrong.InterfaceAddress = "192.0.2.1";
    CHECK(DtNmos_Discover(&wrong, &list, &error) == DTNMOS_E_NETWORK);
    CHECK(DtNmos_Discover(&config, NULL, &error) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dns_reads_resolv_conf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dns_reads_resolv_conf(void)
{
    char server[64];
    char domain[256];
    // The first IPv4 nameserver, and the first domain of the last search or domain line.
    dtnmos_dns_read_resolv_conf("# written by DHCP\n"
                                "nameserver 2001:db8::1\n"
                                "nameserver 192.0.2.53\n"
                                "nameserver 192.0.2.54\n"
                                "domain first.example\n"
                                "search\tstudio.example. other.example\n"
                                "options edns0\n",
                                server, sizeof(server), domain, sizeof(domain));
    CHECK_STR(server, "192.0.2.53");
    CHECK_STR(domain, "studio.example");
    // No server, and the root, which is no domain.
    dtnmos_dns_read_resolv_conf("search .\r\n", server, sizeof(server), domain,
                                sizeof(domain));
    CHECK_STR(server, "");
    CHECK_STR(domain, "");
    // Nothing that fits, and nothing at all.
    dtnmos_dns_read_resolv_conf("nameserver 192.0.2.53\ndomain studio.example", server, 8,
                                domain, 8);
    CHECK_STR(server, "");
    CHECK_STR(domain, "");
    dtnmos_dns_read_resolv_conf("", server, sizeof(server), domain, sizeof(domain));
    CHECK_STR(server, "");
    // The query to a DNS server asks it to recurse.
    uint8_t buffer[64];
    const dtnmos_dns_question question = {"_nmos-query._tcp.studio.example",
                                          DTNMOS_DNS_TYPE_PTR};
    REQUIRE(dtnmos_dns_write_query(buffer, sizeof(buffer), 7,
                                   DTNMOS_DNS_RECURSION_DESIRED, &question, 1) > 0);
    CHECK_EQ(buffer[2], 0x01);
    uint16_t id = 0;
    unsigned rcode = 9;
    const uint8_t refused[12] = {0x12, 0x34, 0x81, 0x85};
    REQUIRE(dtnmos_dns_read_header(refused, sizeof(refused), &id, &rcode));
    CHECK_EQ(id, 0x1234);
    CHECK_EQ(rcode, 5);
    CHECK(!dtnmos_dns_read_header(refused, 11, &id, &rcode));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- discovery_asks_a_dns_server_too -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Multicast DNS and a DNS server announce a registry each of the same priority: both
// come back, the one of the DNS server first, which the DNS server gave the PTR record
// first, and its other records one question at a time.
//
void discovery_asks_a_dns_server_too(void)
{
    static const announced on_link = {
        "On link", "on-link.local", 8080, {"api_proto=http", "api_ver=v1.3", "pri=10"},
        3,         {127, 0, 0, 2}};
    static const announced in_dns[] = {
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
    responder mdns;
    REQUIRE(start_responder(&mdns, &on_link, 1, 1));
    responder dns;
    REQUIRE(start_responder_as(&dns, in_dns, 2, 2, "_nmos-query._tcp.studio.example", 1));

    DtNmosDiscoveryConfig config = config_for(&mdns, 300);
    config.Searches = 0;
    config.DnsServer = dns.destination;
    config.DnsDomain = "studio.example.";
    DtNmosRegistryList* list = NULL;
    DtNmosError error = {DTNMOS_OK, ""};
    const DtNmosResult result = DtNmos_Discover(&config, &list, &error);
    dtnmos_mutex_lock(dns.mutex);
    const int faults = dns.faults;
    const int asked = dns.asked_srv_txt;
    dtnmos_mutex_unlock(dns.mutex);
    stop_responder(&dns);
    stop_responder(&mdns);
    REQUIRE(result == DTNMOS_OK);
    CHECK_EQ(faults, 0);
    CHECK(asked);
    REQUIRE(DtNmosRegistryList_Count(list) == 3);
    const DtNmosRegistryInfo* first = DtNmosRegistryList_At(list, 0);
    CHECK_STR(DtNmosString_Get(&first->Instance), "Studio");
    CHECK_STR(DtNmosString_Get(&first->Url), "http://127.0.0.3:8081");
    CHECK_EQ(first->FoundBy, DTNMOS_SEARCH_UNICAST);
    const DtNmosRegistryInfo* second = DtNmosRegistryList_At(list, 1);
    CHECK_STR(DtNmosString_Get(&second->Instance), "On link");
    CHECK_EQ(second->FoundBy, DTNMOS_SEARCH_MULTICAST);
    const DtNmosRegistryInfo* third = DtNmosRegistryList_At(list, 2);
    CHECK_STR(DtNmosString_Get(&third->Instance), "Backup");
    CHECK_STR(DtNmosString_Get(&third->Host), "backup.studio.example");
    CHECK_EQ(third->FoundBy, DTNMOS_SEARCH_UNICAST);
    DtNmosRegistryInfo copy;
    memset(&copy, 0, sizeof(copy));
    REQUIRE(DtNmosRegistryInfo_Copy(&copy, third) == DTNMOS_OK);
    CHECK_EQ(copy.FoundBy, DTNMOS_SEARCH_UNICAST);
    DtNmosRegistryInfo_Clear(&copy);
    DtNmosRegistryList_Free(list);

    // What it refuses.
    config.DnsServer = "no-address:53";
    CHECK(DtNmos_Discover(&config, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
    config.DnsServer = "127.0.0.1:53";
    config.DnsDomain = "studio..example";
    CHECK(DtNmos_Discover(&config, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(error.Message, "studio..example") != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.- discovery_takes_only_the_dns_server -.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only the unicast search: the multicast responder is not asked, and records of another
// domain than the one asked for are not taken.
//
void discovery_takes_only_the_dns_server(void)
{
    responder mdns;
    REQUIRE(start_responder(&mdns, &registry_b, 1, 1));
    responder dns;
    REQUIRE(start_responder_as(&dns, &registry_b, 1, 1, "_nmos-query._tcp.studio.example",
                               1));
    DtNmosDiscoveryConfig config = config_for(&mdns, 200);
    config.Searches = DTNMOS_SEARCH_UNICAST;
    config.DnsServer = dns.destination;
    config.DnsDomain = "studio.example";
    DtNmosRegistryList* list = NULL;
    REQUIRE(DtNmos_Discover(&config, &list, NULL) == DTNMOS_OK);
    dtnmos_mutex_lock(mdns.mutex);
    const int multicast_queries = mdns.queries;
    dtnmos_mutex_unlock(mdns.mutex);
    dtnmos_mutex_lock(dns.mutex);
    const int dns_queries = dns.queries;
    dtnmos_mutex_unlock(dns.mutex);
    CHECK_EQ(multicast_queries, 0);
    // It answered the first query at once, so that was sent once.
    CHECK_EQ(dns_queries, 1);
    REQUIRE(DtNmosRegistryList_Count(list) == 1);
    CHECK_EQ(DtNmosRegistryList_At(list, 0)->FoundBy, DTNMOS_SEARCH_UNICAST);
    DtNmosRegistryList_Free(list);

    // Records of another domain than the one asked for are not taken.
    config.DnsDomain = "elsewhere.example";
    REQUIRE(DtNmos_Discover(&config, &list, NULL) == DTNMOS_OK);
    CHECK_EQ(DtNmosRegistryList_Count(list), 0);
    DtNmosRegistryList_Free(list);
    stop_responder(&dns);
    stop_responder(&mdns);
}
