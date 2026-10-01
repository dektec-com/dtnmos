// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosDns.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - DNS messages (RFC 1035) as far as DNS-SD over multicast and unicast needs them
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

enum
{
    DTNMOS_DNS_TYPE_A = 1,
    DTNMOS_DNS_TYPE_PTR = 12,
    DTNMOS_DNS_TYPE_TXT = 16,
    DTNMOS_DNS_TYPE_SRV = 33,
    DTNMOS_DNS_CLASS_IN = 1,
    // The flag of a query that asks a DNS server to recurse, which a unicast query needs
    // and a query of multicast DNS leaves out.
    DTNMOS_DNS_RECURSION_DESIRED = 0x0100,
    // The response code of an answer without error.
    DTNMOS_DNS_NO_ERROR = 0,
    // The longest name as text, without the final dot, and its null character.
    DTNMOS_DNS_NAME_SIZE = 256
};

// A question of a query: a name, e.g. "_nmos-query._tcp.local", and the type of record.
typedef struct NmosDnsQuestion
{
    const char* name;
    uint16_t type;
} NmosDnsQuestion;

// Writes a query with id, flags and count questions into buffer; returns its length, or
// 0 when it does not fit or a name is not valid.
size_t NmosDns_WriteQuery(uint8_t* buffer, size_t size, uint16_t id, uint16_t flags,
                          const NmosDnsQuestion* questions, size_t count);

// Reads the ID and the response code (RCODE) of the header of a message; returns 0 when
// it is shorter than a header.
int NmosDns_ReadHeader(const uint8_t* message, size_t length, uint16_t* id,
                       unsigned* rcode);

// Reads the text of a resolv.conf: the first IPv4 address of a nameserver line into
// server, and the first domain of the last search or domain line into domain, as the
// resolver takes them. Each is left empty when the text has none, or when it does not
// fit.
void NmosDns_ReadResolvConf(const char* text, char* server, size_t server_size,
                            char* domain, size_t domain_size);

// A record of a message. Names are text without the final dot, a dot or backslash within
// a label written as "\." or "\\", as NmosDns_WriteQuery() takes them. What a type
// does not use stays zero.
typedef struct NmosDnsRecord
{
    char name[DTNMOS_DNS_NAME_SIZE];
    uint16_t type;
    uint32_t ttl;
    char target[DTNMOS_DNS_NAME_SIZE]; // of a PTR or SRV record
    uint16_t priority;                 // of an SRV record
    uint16_t weight;
    uint16_t port;
    uint8_t address[4]; // of an A record
    const uint8_t* txt; // of a TXT record: its data, within the message
    size_t txt_length;
} NmosDnsRecord;

// Reads a response of length bytes and calls record() for each record of its answer,
// authority and additional sections, of any type. Returns 0, having called record() for
// the records before, when the message is no response or is malformed.
int NmosDns_ReadResponse(const uint8_t* message, size_t length,
                         void (*record)(void* user, const NmosDnsRecord* found),
                         void* user);

// Writes the value of key in the data of a TXT record into value; returns 0 when the
// record has no such key, or when the value does not fit. Keys compare without regard
// to case; a key without "=" has an empty value.
int NmosDns_TxtValue(const uint8_t* txt, size_t length, const char* key, char* value,
                     size_t size);

// Whether two names are equal, comparing ASCII letters without regard to case.
int NmosDns_SameName(const char* a, const char* b);
