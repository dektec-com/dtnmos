// SPDX-License-Identifier: BSD-3-Clause
//
// Finding registries with DNS-SD over a one-shot query of multicast DNS: the query for
// the PTR records of a service type, sent three times within the timeout; the PTR, SRV,
// TXT and A records of every answer, gathered by the name they belong to; one more query
// for what the answers left out; and the list of what was found, sorted by priority.

#include "dtnmos/discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dns.h"
#include "internal.h"
#include "platform.h"

enum
{
  max_instances = 64,
  max_hosts = 64,
  max_message = 9000,  // the largest answer multicast DNS allows
  sends = 3,           // of the first query, spread over the timeout
  default_timeout_ms = 1000,
  mdns_port = 5353
};

static const char* const mdns_address = "224.0.0.251";

// A service instance as its records describe it.
typedef struct instance
{
  char name[DTNMOS_DNS_NAME_SIZE];  // e.g. "Registry 1._nmos-query._tcp.local"
  int has_srv;
  int has_txt;
  char host[DTNMOS_DNS_NAME_SIZE];
  uint16_t port;
  int priority;
  char proto[16];
  char versions[128];
  int auth;
} instance;

typedef struct host_address
{
  char name[DTNMOS_DNS_NAME_SIZE];
  uint8_t address[4];
} host_address;

// What the answers told so far.
typedef struct gathered
{
  const char* service;  // e.g. "_nmos-query._tcp.local"
  instance instances[max_instances];
  size_t instance_count;
  host_address hosts[max_hosts];
  size_t host_count;
} gathered;

struct dtnmos_registry_list
{
  dtnmos_registry_info* registries;
  size_t count;
};

void dtnmos_registry_info_clear(dtnmos_registry_info* registry)
{
  if (registry == NULL)
  {
    return;
  }
  dtnmos_string_clear(&registry->instance);
  dtnmos_string_clear(&registry->host);
  dtnmos_string_clear(&registry->address);
  dtnmos_string_clear(&registry->url);
  dtnmos_string_clear(&registry->api_proto);
  dtnmos_string_clear(&registry->api_versions);
  memset(registry, 0, sizeof(*registry));
}

dtnmos_result dtnmos_registry_info_copy(dtnmos_registry_info* target,
                                        const dtnmos_registry_info* source)
{
  if (target == NULL || source == NULL)
  {
    return DTNMOS_E_INVALID_ARGUMENT;
  }
  if (target == source)
  {
    return DTNMOS_OK;
  }
  dtnmos_registry_info copy;
  memset(&copy, 0, sizeof(copy));
  copy.service = source->service;
  copy.port = source->port;
  copy.priority = source->priority;
  copy.auth = source->auth;
  copy.usable = source->usable;
  if (dtnmos_string_copy(&copy.instance, &source->instance) != DTNMOS_OK ||
      dtnmos_string_copy(&copy.host, &source->host) != DTNMOS_OK ||
      dtnmos_string_copy(&copy.address, &source->address) != DTNMOS_OK ||
      dtnmos_string_copy(&copy.url, &source->url) != DTNMOS_OK ||
      dtnmos_string_copy(&copy.api_proto, &source->api_proto) != DTNMOS_OK ||
      dtnmos_string_copy(&copy.api_versions, &source->api_versions) != DTNMOS_OK)
  {
    dtnmos_registry_info_clear(&copy);
    return DTNMOS_E_NO_MEMORY;
  }
  dtnmos_registry_info_clear(target);
  *target = copy;
  return DTNMOS_OK;
}

size_t dtnmos_registry_list_count(const dtnmos_registry_list* list)
{
  return list == NULL ? 0 : list->count;
}

const dtnmos_registry_info* dtnmos_registry_list_at(const dtnmos_registry_list* list,
                                                    size_t index)
{
  return list == NULL || index >= list->count ? NULL : &list->registries[index];
}

void dtnmos_registry_list_free(dtnmos_registry_list* list)
{
  if (list == NULL)
  {
    return;
  }
  for (size_t i = 0; i < list->count; ++i)
  {
    dtnmos_registry_info_clear(&list->registries[i]);
  }
  free(list->registries);
  free(list);
}

static void log_message(const dtnmos_discovery_config* config, dtnmos_log_level level,
                        const char* format, ...) DTNMOS_PRINTF(3, 4);

static void log_message(const dtnmos_discovery_config* config, dtnmos_log_level level,
                        const char* format, ...)
{
  if (config->log == NULL)
  {
    return;
  }
  char message[512];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  config->log(config->log_user, level, message);
}

// Whether name is an instance of the service, "<instance>.<service>".
static int is_instance_of(const char* name, const char* service)
{
  const size_t name_length = strlen(name);
  const size_t service_length = strlen(service);
  return name_length > service_length + 1 &&
         name[name_length - service_length - 1] == '.' &&
         dtnmos_dns_same_name(name + name_length - service_length, service);
}

// Returns the instance of that name, adding it when it is new; null when there is no
// room.
static instance* instance_named(gathered* found, const char* name)
{
  for (size_t i = 0; i < found->instance_count; ++i)
  {
    if (dtnmos_dns_same_name(found->instances[i].name, name))
    {
      return &found->instances[i];
    }
  }
  if (found->instance_count == max_instances)
  {
    return NULL;
  }
  instance* added = &found->instances[found->instance_count++];
  memset(added, 0, sizeof(*added));
  snprintf(added->name, sizeof(added->name), "%s", name);
  added->priority = -1;
  return added;
}

static const host_address* host_named(const gathered* found, const char* name)
{
  for (size_t i = 0; i < found->host_count; ++i)
  {
    if (dtnmos_dns_same_name(found->hosts[i].name, name))
    {
      return &found->hosts[i];
    }
  }
  return NULL;
}

static void take_record(void* user, const dtnmos_dns_record* record)
{
  gathered* found = user;
  if (record->type == DTNMOS_DNS_TYPE_PTR &&
      dtnmos_dns_same_name(record->name, found->service))
  {
    if (is_instance_of(record->target, found->service))
    {
      (void)instance_named(found, record->target);
    }
    return;
  }
  if (record->type == DTNMOS_DNS_TYPE_A)
  {
    if (host_named(found, record->name) == NULL && found->host_count < max_hosts)
    {
      host_address* added = &found->hosts[found->host_count++];
      snprintf(added->name, sizeof(added->name), "%s", record->name);
      memcpy(added->address, record->address, 4);
    }
    return;
  }
  if ((record->type != DTNMOS_DNS_TYPE_SRV && record->type != DTNMOS_DNS_TYPE_TXT) ||
      !is_instance_of(record->name, found->service))
  {
    return;
  }
  instance* service = instance_named(found, record->name);
  if (service == NULL)
  {
    return;
  }
  if (record->type == DTNMOS_DNS_TYPE_SRV)
  {
    service->has_srv = 1;
    snprintf(service->host, sizeof(service->host), "%s", record->target);
    service->port = record->port;
    return;
  }
  service->has_txt = 1;
  char value[128];
  if (dtnmos_dns_txt_value(record->txt, record->txt_length, "pri", value, sizeof(value)))
  {
    service->priority = atoi(value);
  }
  if (dtnmos_dns_txt_value(record->txt, record->txt_length, "api_proto", value,
                           sizeof(value)))
  {
    // A value too long for http or https is neither.
    const size_t length = strlen(value);
    memcpy(service->proto, length < sizeof(service->proto) ? value : "unknown",
           length < sizeof(service->proto) ? length + 1 : sizeof("unknown"));
  }
  if (dtnmos_dns_txt_value(record->txt, record->txt_length, "api_ver", value,
                           sizeof(value)))
  {
    snprintf(service->versions, sizeof(service->versions), "%s", value);
  }
  if (dtnmos_dns_txt_value(record->txt, record->txt_length, "api_auth", value,
                           sizeof(value)))
  {
    service->auth = strcmp(value, "true") == 0;
  }
}

// Receives answers until deadline_ms and gathers their records.
static void collect(const dtnmos_discovery_config* config, dtnmos_udp* udp,
                    gathered* found, uint8_t* buffer, uint64_t deadline_ms)
{
  for (;;)
  {
    const uint64_t now = dtnmos_monotonic_ms();
    if (now >= deadline_ms)
    {
      return;
    }
    char from[64];
    const long received =
        dtnmos_udp_receive(udp, buffer, max_message, (uint32_t)(deadline_ms - now), from,
                           sizeof(from), NULL);
    if (received < 0)
    {
      return;
    }
    if (received > 0 &&
        !dtnmos_dns_read_response(buffer, (size_t)received, take_record, found))
    {
      log_message(config, DTNMOS_LOG_DEBUG, "An answer from %s is not a DNS response.",
                  from);
    }
  }
}

// Writes the name of an instance as people read it: its first label, unescaped.
static void instance_label(const char* name, const char* service, char* label,
                           size_t size)
{
  const size_t length = strlen(name) - strlen(service) - 1;
  size_t used = 0;
  for (size_t i = 0; i < length && used + 1 < size; ++i)
  {
    if (name[i] == '\\' && i + 1 < length)
    {
      ++i;
    }
    label[used++] = name[i];
  }
  label[used] = '\0';
}

// Whether versions, e.g. "v1.2,v1.3", holds version.
static int has_version(const char* versions, const char* version)
{
  const size_t length = strlen(version);
  for (const char* at = versions; *at != '\0';)
  {
    const char* end = strchr(at, ',');
    const size_t token = end != NULL ? (size_t)(end - at) : strlen(at);
    if (token == length && strncmp(at, version, length) == 0)
    {
      return 1;
    }
    at += token + (end != NULL ? 1 : 0);
  }
  return 0;
}

static int compare_registries(const void* a, const void* b)
{
  const dtnmos_registry_info* left = a;
  const dtnmos_registry_info* right = b;
  if (left->usable != right->usable)
  {
    return left->usable ? -1 : 1;
  }
  if ((left->priority < 0) != (right->priority < 0))
  {
    return left->priority < 0 ? 1 : -1;
  }
  if (left->priority != right->priority)
  {
    return left->priority < right->priority ? -1 : 1;
  }
  return strcmp(dtnmos_string_get(&left->instance), dtnmos_string_get(&right->instance));
}

// Fills registry from a complete instance; returns 0 when out of memory.
static int describe(const gathered* found, const instance* service, dtnmos_service kind,
                    dtnmos_registry_info* registry)
{
  memset(registry, 0, sizeof(*registry));
  registry->service = kind;
  registry->port = service->port;
  registry->priority = service->priority;
  registry->auth = service->auth;
  // IS-04 made api_proto required with v1.1; an announcement without it offers http.
  const char* proto = service->proto[0] != '\0' ? service->proto : "http";
  const int https = strcmp(proto, "https") == 0;
  registry->usable = (https || strcmp(proto, "http") == 0) && !service->auth &&
                     has_version(service->versions, "v1.3");

  char label[DTNMOS_DNS_NAME_SIZE];
  instance_label(service->name, found->service, label, sizeof(label));
  char address[16] = "";
  const host_address* host = host_named(found, service->host);
  if (host != NULL)
  {
    snprintf(address, sizeof(address), "%u.%u.%u.%u", host->address[0], host->address[1],
             host->address[2], host->address[3]);
  }
  // https checks the host name against the certificate, so it keeps the name.
  char url[DTNMOS_DNS_NAME_SIZE + 32];
  snprintf(url, sizeof(url), "%s://%s:%u", proto,
           address[0] != '\0' && !https ? address : service->host, service->port);
  return dtnmos_string_set_text(&registry->instance, label) == DTNMOS_OK &&
         dtnmos_string_set_text(&registry->host, service->host) == DTNMOS_OK &&
         dtnmos_string_set_text(&registry->address, address) == DTNMOS_OK &&
         dtnmos_string_set_text(&registry->url, url) == DTNMOS_OK &&
         dtnmos_string_set_text(&registry->api_proto, proto) == DTNMOS_OK &&
         dtnmos_string_set_text(&registry->api_versions, service->versions) == DTNMOS_OK;
}

// Whether text is an IPv4 address in dotted decimal.
static int is_ipv4(const char* text)
{
  dtnmos_span rest = dtnmos_span_of(text);
  for (int part = 0; part < 4; ++part)
  {
    dtnmos_span number;
    rest = dtnmos_span_split(rest, '.', &number);
    uint32_t value = 0;
    if (!dtnmos_parse_u32(number, 255, &value) || (part < 3) != (rest.data != NULL))
    {
      return 0;
    }
  }
  return 1;
}

// Reads "<IPv4 address>:<port>" into address and port; returns 0 when it is malformed.
static int read_destination(const char* text, char* address, size_t size, uint16_t* port)
{
  const char* colon = strrchr(text, ':');
  if (colon == NULL || (size_t)(colon - text) >= size)
  {
    return 0;
  }
  memcpy(address, text, (size_t)(colon - text));
  address[colon - text] = '\0';
  if (!is_ipv4(address))
  {
    return 0;
  }
  uint32_t value = 0;
  if (!dtnmos_parse_u32(dtnmos_span_of(colon + 1), 65535, &value) || value == 0)
  {
    return 0;
  }
  *port = (uint16_t)value;
  return 1;
}

dtnmos_result dtnmos_discover(const dtnmos_discovery_config* config,
                              dtnmos_registry_list** list, dtnmos_error* error)
{
  if (list != NULL)
  {
    *list = NULL;
  }
  if (config == NULL || list == NULL || config->size < sizeof(dtnmos_discovery_config) ||
      (config->service != DTNMOS_SERVICE_QUERY &&
       config->service != DTNMOS_SERVICE_REGISTRATION))
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "dtnmos_discover() needs a config of a known service and a list.");
  }
  char address[64];
  uint16_t port = mdns_port;
  snprintf(address, sizeof(address), "%s", mdns_address);
  if (config->destination != NULL &&
      !read_destination(config->destination, address, sizeof(address), &port))
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "The destination %s is no <IPv4 address>:<port>.",
                       config->destination);
  }
  if (config->interface_address != NULL && !is_ipv4(config->interface_address))
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "The interface address %s is no IPv4 address.",
                       config->interface_address);
  }
  gathered* found = calloc(1, sizeof(*found));
  uint8_t* buffer = malloc(max_message);
  if (found == NULL || buffer == NULL)
  {
    free(found);
    free(buffer);
    return dtnmos_fail_memory(error);
  }
  found->service = config->service == DTNMOS_SERVICE_QUERY ? "_nmos-query._tcp.local"
                                                           : "_nmos-register._tcp.local";
  dtnmos_result result = DTNMOS_OK;
  dtnmos_udp* udp = dtnmos_udp_open(NULL, config->interface_address);
  if (udp == NULL)
  {
    result = dtnmos_fail(
        error, DTNMOS_E_NETWORK, "No socket for multicast DNS could be opened%s%s.",
        config->interface_address != NULL ? " on " : "",
        config->interface_address != NULL ? config->interface_address : "");
  }
  const uint32_t timeout =
      config->timeout_ms != 0 ? config->timeout_ms : default_timeout_ms;
  const uint16_t id = (uint16_t)(dtnmos_monotonic_ms() | 1u);

  // The first query, three times within the timeout.
  const dtnmos_dns_question question = {found->service, DTNMOS_DNS_TYPE_PTR};
  const uint64_t start = dtnmos_monotonic_ms();
  for (int send = 0; result == DTNMOS_OK && send < sends; ++send)
  {
    uint8_t query[512];
    const size_t length = dtnmos_dns_write_query(query, sizeof(query), id, &question, 1);
    if (!dtnmos_udp_send(udp, address, port, query, length) && send == 0)
    {
      result = dtnmos_fail(error, DTNMOS_E_NETWORK,
                           "The query for %s could not be sent to %s:%u.", found->service,
                           address, port);
      break;
    }
    collect(config, udp, found, buffer,
            start + (uint64_t)timeout * (uint64_t)(send + 1) / sends);
  }

  // One more query for the records the answers left out.
  if (result == DTNMOS_OK)
  {
    dtnmos_dns_question missing[2 * max_instances];
    size_t missing_count = 0;
    for (size_t i = 0; i < found->instance_count; ++i)
    {
      const instance* service = &found->instances[i];
      if (!service->has_srv)
      {
        missing[missing_count++] =
            (dtnmos_dns_question){service->name, DTNMOS_DNS_TYPE_SRV};
      }
      if (!service->has_txt)
      {
        missing[missing_count++] =
            (dtnmos_dns_question){service->name, DTNMOS_DNS_TYPE_TXT};
      }
      else if (service->has_srv && host_named(found, service->host) == NULL &&
               missing_count < 2 * max_instances)
      {
        missing[missing_count++] =
            (dtnmos_dns_question){service->host, DTNMOS_DNS_TYPE_A};
      }
    }
    if (missing_count > 0)
    {
      log_message(config, DTNMOS_LOG_DEBUG, "Asking again for %zu records of %s.",
                  missing_count, found->service);
      const size_t length =
          dtnmos_dns_write_query(buffer, max_message, id, missing, missing_count);
      if (length > 0 && dtnmos_udp_send(udp, address, port, buffer, length))
      {
        collect(config, udp, found, buffer, dtnmos_monotonic_ms() + timeout / 2);
      }
    }
  }
  dtnmos_udp_close(udp);

  // The list of the complete instances.
  dtnmos_registry_list* result_list = NULL;
  if (result == DTNMOS_OK)
  {
    result_list = calloc(1, sizeof(*result_list));
    if (result_list == NULL ||
        (found->instance_count > 0 &&
         (result_list->registries =
              calloc(found->instance_count, sizeof(*result_list->registries))) == NULL))
    {
      free(result_list);
      result_list = NULL;
      result = dtnmos_fail_memory(error);
    }
  }
  for (size_t i = 0; result == DTNMOS_OK && i < found->instance_count; ++i)
  {
    const instance* service = &found->instances[i];
    if (!service->has_srv)
    {
      log_message(config, DTNMOS_LOG_DEBUG, "%s did not say where it is.", service->name);
      continue;
    }
    if (!describe(found, service, config->service,
                  &result_list->registries[result_list->count]))
    {
      dtnmos_registry_info_clear(&result_list->registries[result_list->count]);
      result = dtnmos_fail_memory(error);
      break;
    }
    ++result_list->count;
  }
  if (result == DTNMOS_OK)
  {
    // qsort() takes no null array, which an empty list has.
    if (result_list->count > 1)
    {
      qsort(result_list->registries, result_list->count, sizeof(*result_list->registries),
            compare_registries);
    }
    log_message(config, DTNMOS_LOG_INFO, "Found %zu instances of %s.", result_list->count,
                found->service);
    *list = result_list;
  }
  else
  {
    dtnmos_registry_list_free(result_list);
  }
  free(found);
  free(buffer);
  return result;
}
