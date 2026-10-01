// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosDiscovery.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Finding registries with DNS-SD over a one-shot query of multicast DNS
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosDns.h"
#include "NmosInternal.h"
#include "NmosOs.h"

enum
{
    max_instances = 64,
    max_hosts = 64,
    max_message = 9000, // the largest answer multicast DNS allows
    sends = 3,          // of the first query, spread over the timeout
    default_timeout_ms = 1000,
    mdns_port = 5353,
    dns_port = 53
};

static const char* const mdns_address = "224.0.0.251";

// A service instance as its records describe it.
typedef struct instance
{
    char name[DTNMOS_DNS_NAME_SIZE]; // e.g. "Registry 1._nmos-query._tcp.local"
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
    const char* service; // e.g. "_nmos-query._tcp.local"
    instance instances[max_instances];
    size_t instance_count;
    host_address hosts[max_hosts];
    size_t host_count;
} gathered;

// One of the searches: where its queries go, and what its answers told.
typedef struct search
{
    int active;
    DtNmosSearch kind;
    char address[64];
    uint16_t port;
    uint16_t flags;                     // of its queries
    int answered;                       // the DNS server answered its first query
    char service[DTNMOS_DNS_NAME_SIZE]; // e.g. "_nmos-query._tcp.local"
    gathered found;
} search;

struct DtNmosRegistryList
{
    DtNmosRegistryInfo* registries;
    size_t count;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryInfo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosRegistryInfo_Clear(DtNmosRegistryInfo* registry)
{
    if (registry == NULL)
    {
        return;
    }
    DtNmosString_Clear(&registry->Instance);
    DtNmosString_Clear(&registry->Host);
    DtNmosString_Clear(&registry->Address);
    DtNmosString_Clear(&registry->Url);
    DtNmosString_Clear(&registry->ApiProto);
    DtNmosString_Clear(&registry->ApiVersions);
    memset(registry, 0, sizeof(*registry));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryInfo_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosRegistryInfo_Copy(DtNmosRegistryInfo* target,
                                     const DtNmosRegistryInfo* source)
{
    if (target == NULL || source == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    DtNmosRegistryInfo copy;
    memset(&copy, 0, sizeof(copy));
    copy.Service = source->Service;
    copy.Port = source->Port;
    copy.Priority = source->Priority;
    copy.Auth = source->Auth;
    copy.Usable = source->Usable;
    copy.FoundBy = source->FoundBy;
    if (DtNmosString_Copy(&copy.Instance, &source->Instance) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Host, &source->Host) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Address, &source->Address) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.Url, &source->Url) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.ApiProto, &source->ApiProto) != DTNMOS_OK ||
        DtNmosString_Copy(&copy.ApiVersions, &source->ApiVersions) != DTNMOS_OK)
    {
        DtNmosRegistryInfo_Clear(&copy);
        return DTNMOS_E_NO_MEMORY;
    }
    DtNmosRegistryInfo_Clear(target);
    *target = copy;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosRegistryList_Count(const DtNmosRegistryList* list)
{
    return list == NULL ? 0 : list->count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosRegistryInfo* DtNmosRegistryList_At(const DtNmosRegistryList* list,
                                                size_t index)
{
    return list == NULL || index >= list->count ? NULL : &list->registries[index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRegistryList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosRegistryList_Free(DtNmosRegistryList* list)
{
    if (list == NULL)
    {
        return;
    }
    for (size_t i = 0; i < list->count; ++i)
    {
        DtNmosRegistryInfo_Clear(&list->registries[i]);
    }
    free(list->registries);
    free(list);
}

static void log_message(const DtNmosDiscoveryConfig* config, DtNmosLogLevel level,
                        const char* format, ...) DTNMOS_PRINTF(3, 4);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- log_message -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void log_message(const DtNmosDiscoveryConfig* config, DtNmosLogLevel level,
                        const char* format, ...)
{
    if (config->Log == NULL)
    {
        return;
    }
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    config->Log(config->LogUser, level, message);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_instance_of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether name is an instance of the service, "<instance>.<service>".
//
static int is_instance_of(const char* name, const char* service)
{
    const size_t name_length = strlen(name);
    const size_t service_length = strlen(service);
    return name_length > service_length + 1 &&
           name[name_length - service_length - 1] == '.' &&
           dtnmos_dns_same_name(name + name_length - service_length, service);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- instance_named -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the instance of that name, adding it when it is new; null when there is no
// room.
//
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- host_named -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- take_record -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
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
    if (dtnmos_dns_txt_value(record->txt, record->txt_length, "pri", value,
                             sizeof(value)))
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- collect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Receives answers until deadline_ms and gathers their records into the search they
// answer: that of the DNS server when they come from it with the ID of the query, and
// that of multicast DNS otherwise.
//
static void collect(const DtNmosDiscoveryConfig* config, dtnmos_udp* udp,
                    search* searches, uint16_t id, uint8_t* buffer, uint64_t deadline_ms)
{
    search* unicast = &searches[1];
    for (;;)
    {
        const uint64_t now = dtnmos_monotonic_ms();
        if (now >= deadline_ms)
        {
            return;
        }
        char from[64];
        uint16_t from_port = 0;
        const int received =
            dtnmos_udp_receive(udp, buffer, max_message, (uint32_t)(deadline_ms - now),
                               from, sizeof(from), &from_port);
        if (received < 0)
        {
            return;
        }
        if (received == 0)
        {
            continue;
        }
        search* target = &searches[0];
        if (unicast->active && from_port == unicast->port &&
            strcmp(from, unicast->address) == 0)
        {
            uint16_t answer_id = 0;
            unsigned rcode = 0;
            if (!dtnmos_dns_read_header(buffer, (size_t)received, &answer_id, &rcode) ||
                answer_id != id)
            {
                log_message(config, DTNMOS_LOG_DEBUG,
                            "An answer of the DNS server %s:%u is not to the query.",
                            from, from_port);
                continue;
            }
            unicast->answered = 1;
            if (rcode != DTNMOS_DNS_NO_ERROR)
            {
                // 3 is a name the server does not know, 5 a query it refuses.
                log_message(config, DTNMOS_LOG_DEBUG,
                            "The DNS server %s:%u answered with response code %u.", from,
                            from_port, rcode);
                continue;
            }
            target = unicast;
        }
        else if (!searches[0].active)
        {
            continue;
        }
        if (!dtnmos_dns_read_response(buffer, (size_t)received, take_record,
                                      &target->found))
        {
            log_message(config, DTNMOS_LOG_DEBUG,
                        "An answer from %s is not a DNS response.", from);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ask_missing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Asks one search for the records its answers left out: multicast DNS in one query, and
// the DNS server one question per query, as it answers no more. Returns whether it asked.
//
static int ask_missing(const DtNmosDiscoveryConfig* config, dtnmos_udp* udp,
                       const search* one, uint16_t id)
{
    const gathered* found = &one->found;
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
    if (missing_count == 0)
    {
        return 0;
    }
    log_message(config, DTNMOS_LOG_DEBUG, "Asking again for %zu records of %s.",
                missing_count, one->service);
    const size_t per_query = one->kind == DTNMOS_SEARCH_UNICAST ? 1 : missing_count;
    uint8_t* query = malloc(max_message);
    int asked = 0;
    for (size_t first = 0; query != NULL && first < missing_count; first += per_query)
    {
        const size_t length = dtnmos_dns_write_query(query, max_message, id, one->flags,
                                                     missing + first, per_query);
        asked |=
            length > 0 && dtnmos_udp_send(udp, one->address, one->port, query, length);
    }
    free(query);
    return asked;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- instance_label -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the name of an instance as people read it: its first label, unescaped.
//
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- has_version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether versions, e.g. "v1.2,v1.3", holds version.
//
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- compare_registries -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int compare_registries(const void* a, const void* b)
{
    const DtNmosRegistryInfo* left = a;
    const DtNmosRegistryInfo* right = b;
    if (left->Usable != right->Usable)
    {
        return left->Usable ? -1 : 1;
    }
    if ((left->Priority < 0) != (right->Priority < 0))
    {
        return left->Priority < 0 ? 1 : -1;
    }
    if (left->Priority != right->Priority)
    {
        return left->Priority < right->Priority ? -1 : 1;
    }
    if (left->FoundBy != right->FoundBy)
    {
        return left->FoundBy == DTNMOS_SEARCH_UNICAST ? -1 : 1;
    }
    return strcmp(DtNmosString_Get(&left->Instance), DtNmosString_Get(&right->Instance));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- describe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills registry from a complete instance; returns 0 when out of memory.
//
static int describe(const gathered* found, const instance* service, DtNmosService kind,
                    DtNmosRegistryInfo* registry)
{
    memset(registry, 0, sizeof(*registry));
    registry->Service = kind;
    registry->Port = service->port;
    registry->Priority = service->priority;
    registry->Auth = service->auth;
    // IS-04 made api_proto required with v1.1; an announcement without it offers http.
    const char* proto = service->proto[0] != '\0' ? service->proto : "http";
    const int https = strcmp(proto, "https") == 0;
    registry->Usable = (https || strcmp(proto, "http") == 0) && !service->auth &&
                       has_version(service->versions, "v1.3");

    char label[DTNMOS_DNS_NAME_SIZE];
    instance_label(service->name, found->service, label, sizeof(label));
    char address[16] = "";
    const host_address* host = host_named(found, service->host);
    if (host != NULL)
    {
        snprintf(address, sizeof(address), "%u.%u.%u.%u", host->address[0],
                 host->address[1], host->address[2], host->address[3]);
    }
    // https checks the host name against the certificate, so it keeps the name.
    char url[DTNMOS_DNS_NAME_SIZE + 32];
    snprintf(url, sizeof(url), "%s://%s:%u", proto,
             address[0] != '\0' && !https ? address : service->host, service->port);
    return DtNmosString_SetText(&registry->Instance, label) == DTNMOS_OK &&
           DtNmosString_SetText(&registry->Host, service->host) == DTNMOS_OK &&
           DtNmosString_SetText(&registry->Address, address) == DTNMOS_OK &&
           DtNmosString_SetText(&registry->Url, url) == DTNMOS_OK &&
           DtNmosString_SetText(&registry->ApiProto, proto) == DTNMOS_OK &&
           DtNmosString_SetText(&registry->ApiVersions, service->versions) == DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_ipv4 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether text is an IPv4 address in dotted decimal.
//
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_destination -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads "<IPv4 address>:<port>" into address and port; returns 0 when it is malformed.
//
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- prepare_unicast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets up the search of the DNS server, of the config or of the host; leaves it inactive
// when there is no server or no domain. Fails for a malformed server or domain.
//
static DtNmosResult prepare_unicast(const DtNmosDiscoveryConfig* config,
                                    const char* service_type, search* unicast)
{
    unicast->kind = DTNMOS_SEARCH_UNICAST;
    unicast->flags = DTNMOS_DNS_RECURSION_DESIRED;
    unicast->port = dns_port;
    if (config->DnsServer != NULL &&
        !read_destination(config->DnsServer, unicast->address, sizeof(unicast->address),
                          &unicast->port))
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "The DNS server %s is no <IPv4 address>:<port>.",
                           config->DnsServer);
    }
    if (config->Searches != 0 && (config->Searches & DTNMOS_SEARCH_UNICAST) == 0)
    {
        return DTNMOS_OK;
    }
    char system_server[64] = "";
    char domain[DTNMOS_DNS_NAME_SIZE] = "";
    if (config->DnsServer == NULL || config->DnsDomain == NULL)
    {
        dtnmos_system_dns(system_server, sizeof(system_server), domain, sizeof(domain));
        if (config->DnsServer == NULL)
        {
            snprintf(unicast->address, sizeof(unicast->address), "%s", system_server);
        }
    }
    if (config->DnsDomain != NULL)
    {
        snprintf(domain, sizeof(domain), "%s", config->DnsDomain);
    }
    size_t length = strlen(domain);
    if (length > 1 && domain[length - 1] == '.')
    {
        domain[--length] = '\0';
    }
    if (unicast->address[0] == '\0' || domain[0] == '\0')
    {
        log_message(config, DTNMOS_LOG_DEBUG,
                    "The host names no DNS server or no domain; only multicast DNS is "
                    "asked.");
        return DTNMOS_OK;
    }
    // A name the query cannot hold is no domain.
    uint8_t query[512];
    const dtnmos_dns_question question = {unicast->service, DTNMOS_DNS_TYPE_PTR};
    if (snprintf(unicast->service, sizeof(unicast->service), "%s.%s", service_type,
                 domain) >= (int)sizeof(unicast->service) ||
        dtnmos_dns_write_query(query, sizeof(query), 0, 0, &question, 1) == 0)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT, "The domain %s is no domain.",
                           domain);
    }
    unicast->active = 1;
    unicast->found.service = unicast->service;
    log_message(config, DTNMOS_LOG_DEBUG, "Asking the DNS server %s:%u for %s.",
                unicast->address, unicast->port, unicast->service);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- list_registries -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes the list of the complete instances that both searches found, sorted.
//
static DtNmosResult list_registries(const DtNmosDiscoveryConfig* config,
                                    const search* searches, DtNmosRegistryList** list)
{
    const size_t total =
        searches[0].found.instance_count + searches[1].found.instance_count;
    DtNmosRegistryList* result_list = calloc(1, sizeof(*result_list));
    if (result_list == NULL ||
        (total > 0 && (result_list->registries =
                           calloc(total, sizeof(*result_list->registries))) == NULL))
    {
        free(result_list);
        return dtnmos_fail_memory();
    }
    for (int s = 0; s < 2; ++s)
    {
        const gathered* found = &searches[s].found;
        for (size_t i = 0; i < found->instance_count; ++i)
        {
            const instance* service = &found->instances[i];
            if (!service->has_srv)
            {
                log_message(config, DTNMOS_LOG_DEBUG, "%s did not say where it is.",
                            service->name);
                continue;
            }
            DtNmosRegistryInfo* registry = &result_list->registries[result_list->count];
            if (!describe(found, service, config->Service, registry))
            {
                DtNmosRegistryInfo_Clear(registry);
                DtNmosRegistryList_Free(result_list);
                return dtnmos_fail_memory();
            }
            registry->FoundBy = searches[s].kind;
            ++result_list->count;
        }
    }
    // qsort() takes no null array, which an empty list has.
    if (result_list->count > 1)
    {
        qsort(result_list->registries, result_list->count,
              sizeof(*result_list->registries), compare_registries);
    }
    *list = result_list;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_Discover -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_Discover(const DtNmosDiscoveryConfig* config,
                             DtNmosRegistryList** list)
{
    if (list != NULL)
    {
        *list = NULL;
    }
    if (config == NULL || list == NULL || config->Size < sizeof(DtNmosDiscoveryConfig) ||
        (config->Service != DTNMOS_SERVICE_QUERY &&
         config->Service != DTNMOS_SERVICE_REGISTRATION))
    {
        return dtnmos_fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmos_Discover() needs a config of a known service and a list.");
    }
    if (config->InterfaceAddress != NULL && !is_ipv4(config->InterfaceAddress))
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "The interface address %s is no IPv4 address.",
                           config->InterfaceAddress);
    }
    search* searches = calloc(2, sizeof(*searches));
    uint8_t* buffer = malloc(max_message);
    if (searches == NULL || buffer == NULL)
    {
        free(searches);
        free(buffer);
        return dtnmos_fail_memory();
    }
    const char* service_type = config->Service == DTNMOS_SERVICE_QUERY
                                   ? "_nmos-query._tcp"
                                   : "_nmos-register._tcp";
    DtNmosResult result = DTNMOS_OK;

    // The search of multicast DNS, in the domain local.
    search* multicast = &searches[0];
    multicast->kind = DTNMOS_SEARCH_MULTICAST;
    multicast->active =
        config->Searches == 0 || (config->Searches & DTNMOS_SEARCH_MULTICAST) != 0;
    multicast->port = mdns_port;
    snprintf(multicast->address, sizeof(multicast->address), "%s", mdns_address);
    snprintf(multicast->service, sizeof(multicast->service), "%s.local", service_type);
    multicast->found.service = multicast->service;
    if (config->Destination != NULL &&
        !read_destination(config->Destination, multicast->address,
                          sizeof(multicast->address), &multicast->port))
    {
        result = dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                             "The destination %s is no <IPv4 address>:<port>.",
                             config->Destination);
    }
    if (result == DTNMOS_OK)
    {
        result = prepare_unicast(config, service_type, &searches[1]);
    }
    dtnmos_udp* udp = NULL;
    if (result == DTNMOS_OK &&
        (udp = dtnmos_udp_open(NULL, config->InterfaceAddress)) == NULL)
    {
        result = dtnmos_fail(
            DTNMOS_E_NETWORK, "No socket for multicast DNS could be opened%s%s.",
            config->InterfaceAddress != NULL ? " on " : "",
            config->InterfaceAddress != NULL ? config->InterfaceAddress : "");
    }
    const uint32_t timeout =
        config->TimeoutMs != 0 ? config->TimeoutMs : default_timeout_ms;
    const uint16_t id = (uint16_t)(dtnmos_monotonic_ms() | 1u);

    // The first query, three times within the timeout; to a DNS server till it answers.
    const uint64_t start = dtnmos_monotonic_ms();
    for (int send = 0; result == DTNMOS_OK && send < sends; ++send)
    {
        for (int s = 0; s < 2; ++s)
        {
            search* one = &searches[s];
            if (!one->active || one->answered)
            {
                continue;
            }
            uint8_t query[512];
            const dtnmos_dns_question question = {one->service, DTNMOS_DNS_TYPE_PTR};
            const size_t length = dtnmos_dns_write_query(query, sizeof(query), id,
                                                         one->flags, &question, 1);
            if (dtnmos_udp_send(udp, one->address, one->port, query, length) || send > 0)
            {
                continue;
            }
            if (one->kind == DTNMOS_SEARCH_MULTICAST)
            {
                result = dtnmos_fail(DTNMOS_E_NETWORK,
                                     "The query for %s could not be sent to %s:%u.",
                                     one->service, one->address, one->port);
                break;
            }
            log_message(config, DTNMOS_LOG_WARNING,
                        "The query for %s could not be sent to the DNS server %s:%u.",
                        one->service, one->address, one->port);
            one->active = 0;
        }
        if (result == DTNMOS_OK)
        {
            collect(config, udp, searches, id, buffer,
                    start + (uint64_t)timeout * (uint64_t)(send + 1) / sends);
        }
    }

    // One more query of each search for the records its answers left out.
    if (result == DTNMOS_OK)
    {
        int asked = 0;
        for (int s = 0; s < 2; ++s)
        {
            if (searches[s].active)
            {
                asked |= ask_missing(config, udp, &searches[s], id);
            }
        }
        if (asked)
        {
            collect(config, udp, searches, id, buffer,
                    dtnmos_monotonic_ms() + timeout / 2);
        }
    }
    dtnmos_udp_close(udp);

    if (result == DTNMOS_OK)
    {
        result = list_registries(config, searches, list);
    }
    if (result == DTNMOS_OK)
    {
        log_message(config, DTNMOS_LOG_INFO, "Found %zu instances of %s.",
                    DtNmosRegistryList_Count(*list), service_type);
    }
    free(searches);
    free(buffer);
    return result;
}
