// SPDX-License-Identifier: BSD-3-Clause
//
// Tests of finding registries: DNS queries written and responses read as bytes, with and
// without name compression, escaped dots and malformed messages; and dtnmos_discover()
// against a responder that the test runs on 127.0.0.1 and gives as the destination, so
// that no multicast leaves the host: the order of priority, the question asked again for
// what an answer left out, and a search that finds nothing.

#include "dtnmos/discovery.h"

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dns.h"
#include "platform.h"
#include "tests.h"

// A DNS message the test builds.
typedef struct message
{
  uint8_t bytes[1500];
  size_t length;
} message;

static void put8(message* m, unsigned value)
{
  m->bytes[m->length++] = (uint8_t)value;
}

static void put16(message* m, unsigned value)
{
  put8(m, value >> 8);
  put8(m, value);
}

static void put32(message* m, unsigned long value)
{
  put16(m, (unsigned)(value >> 16));
  put16(m, (unsigned)value);
}

// Writes the labels of name, split at every dot; returns where the name starts.
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

static void put_pointer(message* m, size_t offset)
{
  put16(m, 0xC000u | (unsigned)offset);
}

static void begin_response(message* m, unsigned id, unsigned answers, unsigned additional)
{
  m->length = 0;
  put16(m, id);
  put16(m, 0x8400);  // a response, authoritative
  put16(m, 0);
  put16(m, answers);
  put16(m, 0);
  put16(m, additional);
}

// Writes the type, class, TTL and a placeholder for the length of the data of a record
// whose name is written; returns where the length goes.
static size_t put_record_head(message* m, unsigned type)
{
  put16(m, type);
  put16(m, 1);
  put32(m, 120);
  const size_t at = m->length;
  put16(m, 0);
  return at;
}

static void end_record(message* m, size_t length_at)
{
  const size_t length = m->length - length_at - 2;
  m->bytes[length_at] = (uint8_t)(length >> 8);
  m->bytes[length_at + 1] = (uint8_t)length;
}

// The strings of a TXT record, in the data of the record.
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
  const char* label;  // e.g. "Registry B"
  const char* host;   // e.g. "registry-b.local"
  unsigned port;
  const char* txt[4];
  size_t txt_count;
  unsigned char address[4];
} announced;

// Writes the PTR, SRV, TXT and A records of the instances into an answer to id; with
// ptr_only, only the PTR records.
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

void dns_writes_a_query(void)
{
  uint8_t buffer[512];
  const dtnmos_dns_question question = {"_nmos-query._tcp.local", DTNMOS_DNS_TYPE_PTR};
  const size_t length =
      dtnmos_dns_write_query(buffer, sizeof(buffer), 0x1234, &question, 1);
  static const uint8_t expected[] = {
      0x12, 0x34, 0,   0,   0,   1,   0,   0,   0,   0,   0,   0,    // header
      11,   '_',  'n', 'm', 'o', 's', '-', 'q', 'u', 'e', 'r', 'y',  // _nmos-query
      4,    '_',  't', 'c', 'p', 5,   'l', 'o', 'c', 'a', 'l', 0,
      0,    12,   0,   1};  // PTR IN
  REQUIRE(length == sizeof(expected));
  CHECK(memcmp(buffer, expected, length) == 0);
  CHECK_EQ(dtnmos_dns_write_query(buffer, 20, 1, &question, 1), 0);
  const dtnmos_dns_question empty_label = {"a..local", DTNMOS_DNS_TYPE_A};
  CHECK_EQ(dtnmos_dns_write_query(buffer, sizeof(buffer), 1, &empty_label, 1), 0);
}

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
  REQUIRE(dtnmos_dns_txt_value(kept.found[2].txt, kept.found[2].txt_length, "pri", value,
                               sizeof(value)));
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
  const size_t length = dtnmos_dns_write_query(buffer, sizeof(buffer), 1, &question, 1);
  REQUIRE(length > 12 + 14);
  CHECK_EQ(buffer[12], 13);
  CHECK(memcmp(buffer + 13, "Registry v1.3", 13) == 0);
}

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
  int stop;           // guarded by mutex
  int queries;        // queries received, guarded by mutex
  int asked_srv_txt;  // a query asked for SRV and TXT records, guarded by mutex
  const announced* instances;
  size_t count;
  int answer;  // 0: none, 1: everything, 2: PTR first, the rest when asked
  char destination[32];
} responder;

// Reads the type of the first question of a query and whether any asks for SRV or TXT.
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
    const long received = dtnmos_udp_receive(r->socket, bytes, sizeof(bytes), 50, from,
                                             sizeof(from), &from_port);
    int asks_srv_txt = 0;
    if (received <= 0 || !read_query(bytes, (size_t)received, &asks_srv_txt))
    {
      continue;
    }
    dtnmos_mutex_lock(r->mutex);
    ++r->queries;
    r->asked_srv_txt |= asks_srv_txt;
    dtnmos_mutex_unlock(r->mutex);
    if (r->answer == 0)
    {
      continue;
    }
    // A one-shot query is answered with its ID, to the port it came from.
    message m;
    const unsigned id = ((unsigned)bytes[0] << 8) | bytes[1];
    answer_with(&m, id, "_nmos-query._tcp.local", r->instances, r->count,
                r->answer == 2 && !asks_srv_txt);
    dtnmos_udp_send(r->socket, from, from_port, m.bytes, m.length);
  }
}

static int start_responder(responder* r, const announced* instances, size_t count,
                           int answer)
{
  memset(r, 0, sizeof(*r));
  r->instances = instances;
  r->count = count;
  r->answer = answer;
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

static dtnmos_discovery_config config_for(const responder* r, uint32_t timeout_ms)
{
  dtnmos_discovery_config config;
  memset(&config, 0, sizeof(config));
  config.size = sizeof(config);
  config.service = DTNMOS_SERVICE_QUERY;
  config.destination = r->destination;
  config.timeout_ms = timeout_ms;
  return config;
}

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
  const dtnmos_discovery_config config = config_for(&r, 300);
  dtnmos_registry_list* list = NULL;
  dtnmos_error error = {DTNMOS_OK, ""};
  const dtnmos_result result = dtnmos_discover(&config, &list, &error);
  stop_responder(&r);
  REQUIRE(result == DTNMOS_OK);
  REQUIRE(dtnmos_registry_list_count(list) == 4);
  const dtnmos_registry_info* first = dtnmos_registry_list_at(list, 0);
  CHECK_STR(dtnmos_string_get(&first->instance), "Registry A");
  // https keeps the host name, for its certificate.
  CHECK_STR(dtnmos_string_get(&first->url), "https://registry-a.local:443");
  CHECK_STR(dtnmos_string_get(&first->address), "127.0.0.3");
  CHECK_EQ(first->priority, 0);
  CHECK(first->usable);
  const dtnmos_registry_info* second = dtnmos_registry_list_at(list, 1);
  CHECK_STR(dtnmos_string_get(&second->instance), "Registry B");
  CHECK_STR(dtnmos_string_get(&second->url), "http://127.0.0.2:8080");
  CHECK_STR(dtnmos_string_get(&second->api_versions), "v1.2,v1.3");
  CHECK_EQ(second->service, DTNMOS_SERVICE_QUERY);
  const dtnmos_registry_info* third = dtnmos_registry_list_at(list, 2);
  CHECK_STR(dtnmos_string_get(&third->instance), "No priority");
  CHECK_EQ(third->priority, -1);
  CHECK_STR(dtnmos_string_get(&third->api_proto), "http");
  CHECK(third->usable);
  const dtnmos_registry_info* last = dtnmos_registry_list_at(list, 3);
  CHECK_STR(dtnmos_string_get(&last->instance), "Old");
  CHECK(!last->usable);
  CHECK(dtnmos_registry_list_at(list, 4) == NULL);

  dtnmos_registry_info copy;
  memset(&copy, 0, sizeof(copy));
  REQUIRE(dtnmos_registry_info_copy(&copy, second) == DTNMOS_OK);
  dtnmos_registry_list_free(list);
  CHECK_STR(dtnmos_string_get(&copy.host), "registry-b.local");
  dtnmos_registry_info_clear(&copy);
}

void discovery_asks_again_for_what_is_missing(void)
{
  responder r;
  REQUIRE(start_responder(&r, &registry_b, 1, 2));
  const dtnmos_discovery_config config = config_for(&r, 300);
  dtnmos_registry_list* list = NULL;
  const dtnmos_result result = dtnmos_discover(&config, &list, NULL);
  dtnmos_mutex_lock(r.mutex);
  const int queries = r.queries;
  const int asked = r.asked_srv_txt;
  dtnmos_mutex_unlock(r.mutex);
  stop_responder(&r);
  REQUIRE(result == DTNMOS_OK);
  CHECK_EQ(queries, 4);  // three sends of the first query, and the one that asks again
  CHECK(asked);
  REQUIRE(dtnmos_registry_list_count(list) == 1);
  CHECK_STR(dtnmos_string_get(&dtnmos_registry_list_at(list, 0)->url),
            "http://127.0.0.2:8080");
  dtnmos_registry_list_free(list);
}

void discovery_finds_nothing_in_silence(void)
{
  responder r;
  REQUIRE(start_responder(&r, NULL, 0, 0));
  const dtnmos_discovery_config config = config_for(&r, 200);
  dtnmos_registry_list* list = NULL;
  const uint64_t start = dtnmos_monotonic_ms();
  const dtnmos_result result = dtnmos_discover(&config, &list, NULL);
  const uint64_t took = dtnmos_monotonic_ms() - start;
  stop_responder(&r);
  REQUIRE(result == DTNMOS_OK);
  CHECK_EQ(dtnmos_registry_list_count(list), 0);
  CHECK(took >= 190 && took < 2000);
  dtnmos_registry_list_free(list);

  // What it refuses.
  dtnmos_discovery_config wrong = config;
  wrong.destination = "224.0.0.251";
  dtnmos_error error = {DTNMOS_OK, ""};
  CHECK(dtnmos_discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
  CHECK(list == NULL);
  wrong.destination = "no-address:5353";
  CHECK(dtnmos_discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
  wrong.destination = "127.0.0.1:0";
  CHECK(dtnmos_discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
  wrong = config;
  wrong.interface_address = "10.0.0";
  CHECK(dtnmos_discover(&wrong, &list, &error) == DTNMOS_E_INVALID_ARGUMENT);
  // An address that is no interface of this host cannot send.
  wrong.interface_address = "192.0.2.1";
  CHECK(dtnmos_discover(&wrong, &list, &error) == DTNMOS_E_NETWORK);
  CHECK(dtnmos_discover(&config, NULL, &error) == DTNMOS_E_INVALID_ARGUMENT);
}
