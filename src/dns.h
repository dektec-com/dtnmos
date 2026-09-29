// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* dns.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - DNS messages (RFC 1035) as far as DNS-SD over multicast DNS needs them
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
    // The longest name as text, without the final dot, and its null character.
    DTNMOS_DNS_NAME_SIZE = 256
};

// A question of a query: a name, e.g. "_nmos-query._tcp.local", and the type of record.
typedef struct dtnmos_dns_question
{
    const char* name;
    uint16_t type;
} dtnmos_dns_question;

// Writes a query with id and count questions into buffer; returns its length, or 0 when
// it does not fit or a name is not valid.
size_t dtnmos_dns_write_query(uint8_t* buffer, size_t size, uint16_t id,
                              const dtnmos_dns_question* questions, size_t count);

// A record of a message. Names are text without the final dot, a dot or backslash within
// a label written as "\." or "\\", as dtnmos_dns_write_query() takes them. What a type
// does not use stays zero.
typedef struct dtnmos_dns_record
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
} dtnmos_dns_record;

// Reads a response of length bytes and calls record() for each record of its answer,
// authority and additional sections, of any type. Returns 0, having called record() for
// the records before, when the message is no response or is malformed.
int dtnmos_dns_read_response(const uint8_t* message, size_t length,
                             void (*record)(void* user, const dtnmos_dns_record* found),
                             void* user);

// Writes the value of key in the data of a TXT record into value; returns 0 when the
// record has no such key, or when the value does not fit. Keys compare without regard
// to case; a key without "=" has an empty value.
int dtnmos_dns_txt_value(const uint8_t* txt, size_t length, const char* key, char* value,
                         size_t size);

// Whether two names are equal, comparing ASCII letters without regard to case.
int dtnmos_dns_same_name(const char* a, const char* b);
