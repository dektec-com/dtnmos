// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosSha1.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - SHA-1 (RFC 3174), with which name-based UUIDs of version 5 are hashed
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "NmosInternal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- rotate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t rotate(uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- process -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void process(dtnmos_sha1* sha1, const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 |
               (uint32_t)block[4 * i + 2] << 8 | (uint32_t)block[4 * i + 3];
    }
    for (int i = 16; i < 80; ++i)
    {
        w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = sha1->state[0];
    uint32_t b = sha1->state[1];
    uint32_t c = sha1->state[2];
    uint32_t d = sha1->state[3];
    uint32_t e = sha1->state[4];
    for (int i = 0; i < 80; ++i)
    {
        uint32_t f = 0;
        uint32_t k = 0;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const uint32_t next = rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotate(b, 30);
        b = a;
        a = next;
    }
    sha1->state[0] += a;
    sha1->state[1] += b;
    sha1->state[2] += c;
    sha1->state[3] += d;
    sha1->state[4] += e;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_sha1_init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_sha1_init(dtnmos_sha1* sha1)
{
    memset(sha1, 0, sizeof(*sha1));
    sha1->state[0] = 0x67452301u;
    sha1->state[1] = 0xEFCDAB89u;
    sha1->state[2] = 0x98BADCFEu;
    sha1->state[3] = 0x10325476u;
    sha1->state[4] = 0xC3D2E1F0u;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_sha1_update -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_sha1_update(dtnmos_sha1* sha1, const void* data, size_t length)
{
    const uint8_t* bytes = data;
    sha1->length += length;
    while (length > 0)
    {
        size_t take = 64 - sha1->used;
        if (take > length)
        {
            take = length;
        }
        memcpy(sha1->block + sha1->used, bytes, take);
        sha1->used += take;
        bytes += take;
        length -= take;
        if (sha1->used == 64)
        {
            process(sha1, sha1->block);
            sha1->used = 0;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_sha1_final -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dtnmos_sha1_final(dtnmos_sha1* sha1, uint8_t digest[20])
{
    const uint64_t bits = sha1->length * 8;
    const uint8_t one = 0x80;
    const uint8_t zero = 0;
    dtnmos_sha1_update(sha1, &one, 1);
    while (sha1->used != 56)
    {
        dtnmos_sha1_update(sha1, &zero, 1);
    }
    uint8_t length[8];
    for (int i = 0; i < 8; ++i)
    {
        length[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    dtnmos_sha1_update(sha1, length, 8);
    for (int i = 0; i < 5; ++i)
    {
        digest[4 * i] = (uint8_t)(sha1->state[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(sha1->state[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(sha1->state[i] >> 8);
        digest[4 * i + 3] = (uint8_t)sha1->state[i];
    }
}
