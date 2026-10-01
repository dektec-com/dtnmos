// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosSha1.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - SHA-1 (RFC 3174), with which name-based UUIDs of version 5 are hashed
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "NmosInternal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rotate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t Rotate(uint32_t Value, int Bits)
{
    return (Value << Bits) | (Value >> (32 - Bits));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Process -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Process(NmosSha1* Sha1, const uint8_t Block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (uint32_t)Block[4 * i] << 24 | (uint32_t)Block[4 * i + 1] << 16 |
               (uint32_t)Block[4 * i + 2] << 8 | (uint32_t)Block[4 * i + 3];
    }
    for (int i = 16; i < 80; ++i)
    {
        w[i] = Rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = Sha1->State[0];
    uint32_t b = Sha1->State[1];
    uint32_t c = Sha1->State[2];
    uint32_t d = Sha1->State[3];
    uint32_t e = Sha1->State[4];
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
        const uint32_t Next = Rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotate(b, 30);
        b = a;
        a = Next;
    }
    Sha1->State[0] += a;
    Sha1->State[1] += b;
    Sha1->State[2] += c;
    Sha1->State[3] += d;
    Sha1->State[4] += e;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSha1_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosSha1_Init(NmosSha1* Sha1)
{
    memset(Sha1, 0, sizeof(*Sha1));
    Sha1->State[0] = 0x67452301u;
    Sha1->State[1] = 0xEFCDAB89u;
    Sha1->State[2] = 0x98BADCFEu;
    Sha1->State[3] = 0x10325476u;
    Sha1->State[4] = 0xC3D2E1F0u;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSha1_Update -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosSha1_Update(NmosSha1* Sha1, const void* Data, size_t Length)
{
    const uint8_t* Bytes = Data;
    Sha1->Length += Length;
    while (Length > 0)
    {
        size_t Take = 64 - Sha1->Used;
        if (Take > Length)
        {
            Take = Length;
        }
        memcpy(Sha1->Block + Sha1->Used, Bytes, Take);
        Sha1->Used += Take;
        Bytes += Take;
        Length -= Take;
        if (Sha1->Used == 64)
        {
            Process(Sha1, Sha1->Block);
            Sha1->Used = 0;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSha1_Final -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosSha1_Final(NmosSha1* Sha1, uint8_t Digest[20])
{
    const uint64_t Bits = Sha1->Length * 8;
    const uint8_t One = 0x80;
    const uint8_t Zero = 0;
    NmosSha1_Update(Sha1, &One, 1);
    while (Sha1->Used != 56)
    {
        NmosSha1_Update(Sha1, &Zero, 1);
    }
    uint8_t Length[8];
    for (int i = 0; i < 8; ++i)
    {
        Length[i] = (uint8_t)(Bits >> (56 - 8 * i));
    }
    NmosSha1_Update(Sha1, Length, 8);
    for (int i = 0; i < 5; ++i)
    {
        Digest[4 * i] = (uint8_t)(Sha1->State[i] >> 24);
        Digest[4 * i + 1] = (uint8_t)(Sha1->State[i] >> 16);
        Digest[4 * i + 2] = (uint8_t)(Sha1->State[i] >> 8);
        Digest[4 * i + 3] = (uint8_t)Sha1->State[i];
    }
}
