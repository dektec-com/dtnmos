// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosDns.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Writing and reading DNS messages (RFC 1035), as far as DNS-SD needs them
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    DTNMOS_DNS_TYPE_A = 1,    // record type: an IPv4 address
    DTNMOS_DNS_TYPE_PTR = 12, // record type: a pointer to another name
    DTNMOS_DNS_TYPE_TXT = 16, // record type: key=value text
    DTNMOS_DNS_TYPE_SRV = 33, // record type: a service's host and port
    DTNMOS_DNS_CLASS_IN = 1,  // the Internet class, the only one used
    // Header flag that asks a DNS server to resolve the name fully. A unicast query sets
    // it; a multicast DNS query does not.
    DTNMOS_DNS_RECURSION_DESIRED = 0x0100,
    // Response code: no error.
    DTNMOS_DNS_NO_ERROR = 0,
    // Size of a buffer for a name: the longest name, without the final dot, plus its
    // null character.
    DTNMOS_DNS_NAME_SIZE = 256
};

// A question in a query: the name to look up, e.g. "_nmos-query._tcp.local", and the
// type of record wanted.
typedef struct NmosDnsQuestion
{
    const char* Name; // the name, without the final dot
    uint16_t Type;    // a DTNMOS_DNS_TYPE_ value
} NmosDnsQuestion;

// Writes a DNS query with Count questions into Buffer. Returns its length, or 0 when it
// does not fit or a name is invalid.
size_t NmosDns_WriteQuery(uint8_t* Buffer, size_t Size, uint16_t Id, uint16_t Flags,
                          const NmosDnsQuestion* Questions, size_t Count);

// Reads the ID and the response code (RCODE) from a message's header. Returns false when
// the message is shorter than a header.
bool NmosDns_ReadHeader(const uint8_t* Message, size_t Length, uint16_t* Id,
                        unsigned* Rcode);

// Finds the DNS server and the search domain in the text of a resolv.conf file. Server
// gets the first IPv4 address of a nameserver line. Domain gets the first domain of the
// last search or domain line, as the resolver does. Each is left empty when the text has
// none, or when it does not fit.
void NmosDns_ReadResolvConf(const char* Text, char* Server, size_t ServerSize,
                            char* Domain, size_t DomainSize);

// A record found in a DNS response. Fields its type does not use are zero.
//
// Names are text without the final dot. A dot or backslash inside a label is written as
// "\." or "\\"; NmosDns_WriteQuery() takes names in the same form.
typedef struct NmosDnsRecord
{
    char Name[DTNMOS_DNS_NAME_SIZE];   // the name the record is about
    uint16_t Type;                     // a DTNMOS_DNS_TYPE_ value
    uint32_t Ttl;                      // how long the record is valid, in seconds
    char Target[DTNMOS_DNS_NAME_SIZE]; // PTR and SRV: the name it points to
    uint16_t Priority;                 // SRV: lower is preferred
    uint16_t Weight;                   // SRV: share among equal priorities
    uint16_t Port;                     // SRV: the service's port
    uint8_t Address[4];                // A: the IPv4 address
    const uint8_t* Txt;                // TXT: its data, pointing into the message
    size_t TxtLength;                  // TXT: the length of the data
} NmosDnsRecord;

// Reads a DNS response and calls Record() for each record in it, of any type and in any
// section. Returns false when the message is not a response or is malformed; Record()
// has then been called for the records before the error.
bool NmosDns_ReadResponse(const uint8_t* Message, size_t Length,
                          void (*Record)(void* User, const NmosDnsRecord* Found),
                          void* User);

// Looks up Key in the data of a TXT record and copies its value into Value. Returns
// false when the key is absent or the value does not fit. Keys are compared ignoring
// case; a key without "=" has an empty value.
bool NmosDns_TxtValue(const uint8_t* Txt, size_t Length, const char* Key, char* Value,
                      size_t Size);

// Returns whether two DNS names are equal, ignoring the case of ASCII letters.
bool NmosDns_SameName(const char* a, const char* b);
