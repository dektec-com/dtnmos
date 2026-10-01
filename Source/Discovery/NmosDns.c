// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* NmosDns.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - DNS messages for DNS-SD
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "NmosDns.h"

#include <stdio.h>
#include <string.h>

// The header of a message is 12 bytes; its flags have the QR bit for a response.
enum
{
    header_size = 12,
    flag_response = 0x8000,
    flag_qr_byte = 0x80, // QR in the first byte of the flags, the third of the header
    rcode_mask = 0x0F,   // RCODE in the second byte of the flags, the fourth
    max_pointers = 64    // name compression pointers followed within one name
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- put16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void put16(uint8_t* at, uint16_t value)
{
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint16_t get16(const uint8_t* at)
{
    return (uint16_t)((at[0] << 8) | at[1]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- get32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t get32(const uint8_t* at)
{
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) | ((uint32_t)at[2] << 8) |
           at[3];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes name, in which "\." and "\\" stand for a dot and a backslash within a label, as
// labels at buffer + *offset; returns 0 when it does not fit or a label is empty or
// longer than 63 bytes.
//
static int write_name(uint8_t* buffer, size_t size, size_t* offset, const char* name)
{
    const char* at = name;
    while (*at != '\0')
    {
        uint8_t label[63];
        size_t length = 0;
        while (*at != '\0' && *at != '.')
        {
            char c = *at++;
            if (c == '\\' && *at != '\0')
            {
                c = *at++;
            }
            if (length == sizeof(label))
            {
                return 0;
            }
            label[length++] = (uint8_t)c;
        }
        if (length == 0 || *offset + 1 + length > size)
        {
            return 0;
        }
        if (*at == '.')
        {
            ++at;
        }
        buffer[(*offset)++] = (uint8_t)length;
        memcpy(buffer + *offset, label, length);
        *offset += length;
    }
    if (*offset + 1 > size)
    {
        return 0;
    }
    buffer[(*offset)++] = 0;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_WriteQuery -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t NmosDns_WriteQuery(uint8_t* buffer, size_t size, uint16_t id, uint16_t flags,
                          const NmosDnsQuestion* questions, size_t count)
{
    if (buffer == NULL || size < header_size || count == 0 || count > 0xFFFF)
    {
        return 0;
    }
    memset(buffer, 0, header_size);
    put16(buffer, id);
    put16(buffer + 2, (uint16_t)(flags & ~flag_response));
    put16(buffer + 4, (uint16_t)count);
    size_t offset = header_size;
    for (size_t i = 0; i < count; ++i)
    {
        if (questions[i].name == NULL ||
            !write_name(buffer, size, &offset, questions[i].name) || offset + 4 > size)
        {
            return 0;
        }
        put16(buffer + offset, questions[i].type);
        put16(buffer + offset + 2, DTNMOS_DNS_CLASS_IN);
        offset += 4;
    }
    return offset;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the name at *offset into text, following compression pointers, and moves *offset
// past the name as it stands there. Returns 0 when the name is malformed or too long.
//
static int read_name(const uint8_t* message, size_t length, size_t* offset, char* text)
{
    size_t at = *offset;
    size_t used = 0;
    int pointers = 0;
    int jumped = 0;
    for (;;)
    {
        if (at >= length)
        {
            return 0;
        }
        const uint8_t first = message[at];
        if ((first & 0xC0) == 0xC0)
        {
            if (at + 1 >= length || ++pointers > max_pointers)
            {
                return 0;
            }
            const size_t target = ((size_t)(first & 0x3F) << 8) | message[at + 1];
            if (!jumped)
            {
                *offset = at + 2;
                jumped = 1;
            }
            at = target;
            continue;
        }
        if ((first & 0xC0) != 0)
        {
            return 0; // the extended label types are not used
        }
        if (first == 0)
        {
            if (!jumped)
            {
                *offset = at + 1;
            }
            text[used] = '\0';
            return 1;
        }
        if (at + 1 + first > length)
        {
            return 0;
        }
        if (used > 0)
        {
            text[used++] = '.';
        }
        // A dot or backslash within a label is escaped, as in the text form of names.
        for (size_t i = 0; i < first; ++i)
        {
            const char c = (char)message[at + 1 + i];
            if (used + 3 >= DTNMOS_DNS_NAME_SIZE)
            {
                return 0;
            }
            if (c == '.' || c == '\\')
            {
                text[used++] = '\\';
            }
            text[used++] = c;
        }
        at += 1 + (size_t)first;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_ReadResponse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosDns_ReadResponse(const uint8_t* message, size_t length,
                         void (*record)(void* user, const NmosDnsRecord* found),
                         void* user)
{
    // The flags of the header are tested in their bytes rather than through get16(): QR
    // is the top bit of the third byte (RFC 1035, 4.1.1). MSVC 19.51, of Visual Studio
    // 2026, compiles a mask of what get16() returns wrongly in a release build, and the
    // test failed for every response.
    if (message == NULL || length < header_size || (message[2] & flag_qr_byte) == 0)
    {
        return 0;
    }
    const unsigned questions = get16(message + 4);
    const unsigned records =
        (unsigned)get16(message + 6) + get16(message + 8) + get16(message + 10);
    size_t offset = header_size;
    char name[DTNMOS_DNS_NAME_SIZE];
    for (unsigned i = 0; i < questions; ++i)
    {
        if (!read_name(message, length, &offset, name) || offset + 4 > length)
        {
            return 0;
        }
        offset += 4;
    }
    for (unsigned i = 0; i < records; ++i)
    {
        NmosDnsRecord found;
        memset(&found, 0, sizeof(found));
        if (!read_name(message, length, &offset, found.name) || offset + 10 > length)
        {
            return 0;
        }
        found.type = get16(message + offset);
        found.ttl = get32(message + offset + 4);
        const size_t data_length = get16(message + offset + 8);
        const size_t data = offset + 10;
        if (data + data_length > length)
        {
            return 0;
        }
        size_t at = data;
        switch (found.type)
        {
        case DTNMOS_DNS_TYPE_PTR:
            if (!read_name(message, length, &at, found.target))
            {
                return 0;
            }
            break;
        case DTNMOS_DNS_TYPE_SRV:
            if (data_length < 7)
            {
                return 0;
            }
            found.priority = get16(message + data);
            found.weight = get16(message + data + 2);
            found.port = get16(message + data + 4);
            at = data + 6;
            if (!read_name(message, length, &at, found.target))
            {
                return 0;
            }
            break;
        case DTNMOS_DNS_TYPE_A:
            if (data_length != 4)
            {
                return 0;
            }
            memcpy(found.address, message + data, 4);
            break;
        case DTNMOS_DNS_TYPE_TXT:
            found.txt = message + data;
            found.txt_length = data_length;
            break;
        default:
            break;
        }
        record(user, &found);
        offset = data + data_length;
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- lower -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_SameName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosDns_SameName(const char* a, const char* b)
{
    while (*a != '\0' && lower((unsigned char)*a) == lower((unsigned char)*b))
    {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_TxtValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosDns_TxtValue(const uint8_t* txt, size_t length, const char* key, char* value,
                     size_t size)
{
    const size_t key_length = strlen(key);
    size_t at = 0;
    while (at < length)
    {
        const size_t string_length = txt[at];
        const uint8_t* string = txt + at + 1;
        if (at + 1 + string_length > length)
        {
            return 0;
        }
        at += 1 + string_length;
        if (string_length < key_length)
        {
            continue;
        }
        int same = 1;
        for (size_t i = 0; same && i < key_length; ++i)
        {
            same = lower(string[i]) == lower((unsigned char)key[i]);
        }
        if (!same || (string_length > key_length && string[key_length] != '='))
        {
            continue;
        }
        const size_t value_length =
            string_length > key_length ? string_length - key_length - 1 : 0;
        if (value_length + 1 > size)
        {
            return 0;
        }
        memcpy(value, string + key_length + (string_length > key_length ? 1 : 0),
               value_length);
        value[value_length] = '\0';
        return 1;
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_ReadHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosDns_ReadHeader(const uint8_t* message, size_t length, uint16_t* id,
                       unsigned* rcode)
{
    if (message == NULL || length < header_size)
    {
        return 0;
    }
    *id = get16(message);
    // RCODE is the low four bits of the fourth byte, read there for the reason
    // NmosDns_ReadResponse() gives.
    *rcode = message[3] & rcode_mask;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_space -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_word -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies the word at *at, up to the end of the line, into word, and moves *at past it;
// returns its length, 0 at the end of the line, or when it does not fit.
//
static size_t next_word(const char** at, char* word, size_t size)
{
    while (is_space(**at))
    {
        ++*at;
    }
    const char* start = *at;
    while (**at != '\0' && **at != '\n' && !is_space(**at))
    {
        ++*at;
    }
    const size_t length = (size_t)(*at - start);
    if (length == 0 || length >= size)
    {
        return 0;
    }
    memcpy(word, start, length);
    word[length] = '\0';
    return length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_ipv4_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether text is an IPv4 address in dotted decimal, four numbers up to 255.
//
static int is_ipv4_text(const char* text)
{
    unsigned parts[4];
    char rest = '\0';
    return sscanf(text, "%3u.%3u.%3u.%3u%c", &parts[0], &parts[1], &parts[2], &parts[3],
                  &rest) == 4 &&
           parts[0] <= 255 && parts[1] <= 255 && parts[2] <= 255 && parts[3] <= 255;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_ReadResolvConf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosDns_ReadResolvConf(const char* text, char* server, size_t server_size,
                            char* domain, size_t domain_size)
{
    server[0] = '\0';
    domain[0] = '\0';
    const char* at = text;
    while (at != NULL && *at != '\0')
    {
        char keyword[16];
        char word[DTNMOS_DNS_NAME_SIZE];
        if (next_word(&at, keyword, sizeof(keyword)) > 0 &&
            next_word(&at, word, sizeof(word)) > 0)
        {
            if (strcmp(keyword, "nameserver") == 0 && server[0] == '\0' &&
                is_ipv4_text(word) && strlen(word) < server_size)
            {
                memcpy(server, word, strlen(word) + 1);
            }
            else if (strcmp(keyword, "search") == 0 || strcmp(keyword, "domain") == 0)
            {
                // The last line wins, and "." is the root, which is no domain to browse.
                size_t length = strlen(word);
                if (length > 1 && word[length - 1] == '.')
                {
                    word[--length] = '\0';
                }
                const int fits = length < domain_size && strcmp(word, ".") != 0;
                memcpy(domain, fits ? word : "", fits ? length + 1 : 1);
            }
        }
        at = strchr(at, '\n');
        at = at != NULL ? at + 1 : NULL;
    }
}
