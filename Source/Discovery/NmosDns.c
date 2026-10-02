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
    NMOS_HEADER_SIZE = 12,
    NMOS_FLAG_RESPONSE = 0x8000,
    NMOS_FLAG_QR_BYTE =
        0x80,               // QR in the first byte of the flags, the third of the header
    NMOS_RCODE_MASK = 0x0F, // RCODE in the second byte of the flags, the fourth
    NMOS_MAX_POINTERS = 64  // name compression pointers followed within one name
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Put16(uint8_t* At, uint16_t Value)
{
    At[0] = (uint8_t)(Value >> 8);
    At[1] = (uint8_t)Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Get16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint16_t Get16(const uint8_t* At)
{
    return (uint16_t)((At[0] << 8) | At[1]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Get32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t Get32(const uint8_t* At)
{
    return ((uint32_t)At[0] << 24) | ((uint32_t)At[1] << 16) | ((uint32_t)At[2] << 8) |
           At[3];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes name, in which "\." and "\\" stand for a dot and a backslash within a label, as
// labels at buffer + *Offset; returns false when it does not fit or a label is empty or
// longer than 63 bytes.
//
static bool WriteName(uint8_t* Buffer, size_t Size, size_t* Offset, const char* Name)
{
    const char* At = Name;
    while (*At != '\0')
    {
        uint8_t Label[63];
        size_t Length = 0;
        while (*At != '\0' && *At != '.')
        {
            char c = *At++;
            if (c == '\\' && *At != '\0')
            {
                c = *At++;
            }
            if (Length == sizeof(Label))
            {
                return false;
            }
            Label[Length++] = (uint8_t)c;
        }
        if (Length == 0 || *Offset + 1 + Length > Size)
        {
            return false;
        }
        if (*At == '.')
        {
            ++At;
        }
        Buffer[(*Offset)++] = (uint8_t)Length;
        memcpy(Buffer + *Offset, Label, Length);
        *Offset += Length;
    }
    if (*Offset + 1 > Size)
    {
        return false;
    }
    Buffer[(*Offset)++] = 0;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_WriteQuery -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t NmosDns_WriteQuery(uint8_t* Buffer, size_t Size, uint16_t Id, uint16_t Flags,
                          const NmosDnsQuestion* Questions, size_t Count)
{
    if (Buffer == NULL || Size < NMOS_HEADER_SIZE || Count == 0 || Count > 0xFFFF)
    {
        return 0;
    }
    memset(Buffer, 0, NMOS_HEADER_SIZE);
    Put16(Buffer, Id);
    Put16(Buffer + 2, (uint16_t)(Flags & ~NMOS_FLAG_RESPONSE));
    Put16(Buffer + 4, (uint16_t)Count);
    size_t Offset = NMOS_HEADER_SIZE;
    for (size_t i = 0; i < Count; ++i)
    {
        if (Questions[i].Name == NULL ||
            !WriteName(Buffer, Size, &Offset, Questions[i].Name) || Offset + 4 > Size)
        {
            return 0;
        }
        Put16(Buffer + Offset, Questions[i].Type);
        Put16(Buffer + Offset + 2, DTNMOS_DNS_CLASS_IN);
        Offset += 4;
    }
    return Offset;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the name at *Offset into text, following compression pointers, and moves *Offset
// past the name as it stands there. Returns false when the name is malformed or too long.
//
static bool ReadName(const uint8_t* Message, size_t Length, size_t* Offset, char* Text)
{
    size_t At = *Offset;
    size_t Used = 0;
    int Pointers = 0;
    bool Jumped = false;
    for (;;)
    {
        if (At >= Length)
        {
            return false;
        }
        const uint8_t First = Message[At];
        if ((First & 0xC0) == 0xC0)
        {
            if (At + 1 >= Length || ++Pointers > NMOS_MAX_POINTERS)
            {
                return false;
            }
            const size_t Target = ((size_t)(First & 0x3F) << 8) | Message[At + 1];
            if (!Jumped)
            {
                *Offset = At + 2;
                Jumped = true;
            }
            At = Target;
            continue;
        }
        if ((First & 0xC0) != 0)
        {
            return false; // the extended label types are not used
        }
        if (First == 0)
        {
            if (!Jumped)
            {
                *Offset = At + 1;
            }
            Text[Used] = '\0';
            return true;
        }
        if (At + 1 + First > Length)
        {
            return false;
        }
        if (Used > 0)
        {
            Text[Used++] = '.';
        }
        // A dot or backslash within a label is escaped, as in the text form of names.
        for (size_t i = 0; i < First; ++i)
        {
            const char c = (char)Message[At + 1 + i];
            if (Used + 3 >= DTNMOS_DNS_NAME_SIZE)
            {
                return false;
            }
            if (c == '.' || c == '\\')
            {
                Text[Used++] = '\\';
            }
            Text[Used++] = c;
        }
        At += 1 + (size_t)First;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_ReadResponse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosDns_ReadResponse(const uint8_t* Message, size_t Length,
                          void (*Record)(void* User, const NmosDnsRecord* Found),
                          void* User)
{
    // The flags of the header are tested in their bytes rather than through Get16(): QR
    // is the top bit of the third byte (RFC 1035, 4.1.1). MSVC 19.51, of Visual Studio
    // 2026, compiles a mask of what Get16() returns wrongly in a release build, and the
    // test failed for every response.
    if (Message == NULL || Length < NMOS_HEADER_SIZE ||
        (Message[2] & NMOS_FLAG_QR_BYTE) == 0)
    {
        return false;
    }
    const unsigned Questions = Get16(Message + 4);
    const unsigned Records =
        (unsigned)Get16(Message + 6) + Get16(Message + 8) + Get16(Message + 10);
    size_t Offset = NMOS_HEADER_SIZE;
    char Name[DTNMOS_DNS_NAME_SIZE];
    for (unsigned i = 0; i < Questions; ++i)
    {
        if (!ReadName(Message, Length, &Offset, Name) || Offset + 4 > Length)
        {
            return false;
        }
        Offset += 4;
    }
    for (unsigned i = 0; i < Records; ++i)
    {
        NmosDnsRecord Found;
        memset(&Found, 0, sizeof(Found));
        if (!ReadName(Message, Length, &Offset, Found.Name) || Offset + 10 > Length)
        {
            return false;
        }
        Found.Type = Get16(Message + Offset);
        Found.Ttl = Get32(Message + Offset + 4);
        const size_t DataLength = Get16(Message + Offset + 8);
        const size_t Data = Offset + 10;
        if (Data + DataLength > Length)
        {
            return false;
        }
        size_t At = Data;
        switch (Found.Type)
        {
        case DTNMOS_DNS_TYPE_PTR:
            if (!ReadName(Message, Length, &At, Found.Target))
            {
                return false;
            }
            break;
        case DTNMOS_DNS_TYPE_SRV:
            if (DataLength < 7)
            {
                return false;
            }
            Found.Priority = Get16(Message + Data);
            Found.Weight = Get16(Message + Data + 2);
            Found.Port = Get16(Message + Data + 4);
            At = Data + 6;
            if (!ReadName(Message, Length, &At, Found.Target))
            {
                return false;
            }
            break;
        case DTNMOS_DNS_TYPE_A:
            if (DataLength != 4)
            {
                return false;
            }
            memcpy(Found.Address, Message + Data, 4);
            break;
        case DTNMOS_DNS_TYPE_TXT:
            Found.Txt = Message + Data;
            Found.TxtLength = DataLength;
            break;
        default:
            break;
        }
        Record(User, &Found);
        Offset = Data + DataLength;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Lower -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int Lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_SameName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosDns_SameName(const char* a, const char* b)
{
    while (*a != '\0' && Lower((unsigned char)*a) == Lower((unsigned char)*b))
    {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_TxtValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosDns_TxtValue(const uint8_t* Txt, size_t Length, const char* Key, char* Value,
                      size_t Size)
{
    const size_t KeyLength = strlen(Key);
    size_t At = 0;
    while (At < Length)
    {
        const size_t StringLength = Txt[At];
        const uint8_t* String = Txt + At + 1;
        if (At + 1 + StringLength > Length)
        {
            return false;
        }
        At += 1 + StringLength;
        if (StringLength < KeyLength)
        {
            continue;
        }
        bool Same = true;
        for (size_t i = 0; Same && i < KeyLength; ++i)
        {
            Same = Lower(String[i]) == Lower((unsigned char)Key[i]);
        }
        if (!Same || (StringLength > KeyLength && String[KeyLength] != '='))
        {
            continue;
        }
        const size_t ValueLength =
            StringLength > KeyLength ? StringLength - KeyLength - 1 : 0;
        if (ValueLength + 1 > Size)
        {
            return false;
        }
        memcpy(Value, String + KeyLength + (StringLength > KeyLength ? 1 : 0),
               ValueLength);
        Value[ValueLength] = '\0';
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_ReadHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosDns_ReadHeader(const uint8_t* Message, size_t Length, uint16_t* Id,
                        unsigned* Rcode)
{
    if (Message == NULL || Length < NMOS_HEADER_SIZE)
    {
        return false;
    }
    *Id = Get16(Message);
    // RCODE is the low four bits of the fourth byte, read there for the reason
    // NmosDns_ReadResponse() gives.
    *Rcode = Message[3] & NMOS_RCODE_MASK;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSpace -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies the word at *At, up to the end of the line, into word, and moves *At past it;
// returns its length, 0 at the end of the line, or when it does not fit.
//
static size_t NextWord(const char** At, char* Word, size_t Size)
{
    while (IsSpace(**At))
    {
        ++*At;
    }
    const char* Start = *At;
    while (**At != '\0' && **At != '\n' && !IsSpace(**At))
    {
        ++*At;
    }
    const size_t Length = (size_t)(*At - Start);
    if (Length == 0 || Length >= Size)
    {
        return 0;
    }
    memcpy(Word, Start, Length);
    Word[Length] = '\0';
    return Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsIpv4Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether text is an IPv4 address in dotted decimal, four numbers up to 255.
//
static bool IsIpv4Text(const char* Text)
{
    unsigned Parts[4];
    char Rest = '\0';
    return sscanf(Text, "%3u.%3u.%3u.%3u%c", &Parts[0], &Parts[1], &Parts[2], &Parts[3],
                  &Rest) == 4 &&
           Parts[0] <= 255 && Parts[1] <= 255 && Parts[2] <= 255 && Parts[3] <= 255;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosDns_ReadResolvConf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosDns_ReadResolvConf(const char* Text, char* Server, size_t ServerSize,
                            char* Domain, size_t DomainSize)
{
    Server[0] = '\0';
    Domain[0] = '\0';
    const char* At = Text;
    while (At != NULL && *At != '\0')
    {
        char Keyword[16];
        char Word[DTNMOS_DNS_NAME_SIZE];
        if (NextWord(&At, Keyword, sizeof(Keyword)) > 0 &&
            NextWord(&At, Word, sizeof(Word)) > 0)
        {
            if (strcmp(Keyword, "nameserver") == 0 && Server[0] == '\0' &&
                IsIpv4Text(Word) && strlen(Word) < ServerSize)
            {
                memcpy(Server, Word, strlen(Word) + 1);
            }
            else if (strcmp(Keyword, "search") == 0 || strcmp(Keyword, "domain") == 0)
            {
                // The last line wins, and "." is the root, which is no domain to browse.
                size_t Length = strlen(Word);
                if (Length > 1 && Word[Length - 1] == '.')
                {
                    Word[--Length] = '\0';
                }
                const bool Fits = Length < DomainSize && strcmp(Word, ".") != 0;
                memcpy(Domain, Fits ? Word : "", Fits ? Length + 1 : 1);
            }
        }
        At = strchr(At, '\n');
        At = At != NULL ? At + 1 : NULL;
    }
}
