// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmos.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The version, the names of results and media, failures, and name-based UUIDs
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <string.h>

#include "NmosInternal.h"
#include "dtnmos_sdp.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_Version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmos_Version(int* Major, int* Minor, int* Patch)
{
    if (Major != NULL)
    {
        *Major = DTNMOS_VERSION_MAJOR;
    }
    if (Minor != NULL)
    {
        *Minor = DTNMOS_VERSION_MINOR;
    }
    if (Patch != NULL)
    {
        *Patch = DTNMOS_VERSION_PATCH;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosResult_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosResult_Name(DtNmosResult Result)
{
    switch (Result)
    {
    case DTNMOS_OK:
        return "DTNMOS_OK";
    case DTNMOS_E:
        return "DTNMOS_E";
    case DTNMOS_E_INVALID_ARGUMENT:
        return "DTNMOS_E_INVALID_ARGUMENT";
    case DTNMOS_E_PARSE:
        return "DTNMOS_E_PARSE";
    case DTNMOS_E_NOT_FOUND:
        return "DTNMOS_E_NOT_FOUND";
    case DTNMOS_E_AMBIGUOUS:
        return "DTNMOS_E_AMBIGUOUS";
    case DTNMOS_E_HTTP:
        return "DTNMOS_E_HTTP";
    case DTNMOS_E_TIMEOUT:
        return "DTNMOS_E_TIMEOUT";
    case DTNMOS_E_STATE:
        return "DTNMOS_E_STATE";
    case DTNMOS_E_NO_MEMORY:
        return "DTNMOS_E_NO_MEMORY";
    case DTNMOS_E_INTERNAL:
        return "DTNMOS_E_INTERNAL";
    case DTNMOS_E_NETWORK:
        return "DTNMOS_E_NETWORK";
    case DTNMOS_E_BUFFER_TOO_SMALL:
        return "DTNMOS_E_BUFFER_TOO_SMALL";
    }
    return "unknown DtNmosResult";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosMedia_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosMedia_Name(DtNmosMedia Media)
{
    switch (Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        return "video";
    case DTNMOS_MEDIA_AUDIO:
        return "audio";
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return "compressed video";
    case DTNMOS_MEDIA_ANC:
        return "ancillary data";
    case DTNMOS_MEDIA_OTHER:
        return "other";
    }
    return "unknown";
}

// The message of the last failure on each thread. A fixed array, so that a failure never
// allocates, and thread-local, as CDTAPI's GetLastException is.
#if defined(_MSC_VER)
    #define NMOS_THREAD_LOCAL __declspec(thread)
#else
    #define NMOS_THREAD_LOCAL _Thread_local
#endif
static NMOS_THREAD_LOCAL char LastError[512];

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_GetLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmos_GetLastError(void)
{
    return LastError;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_SetLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_SetLastError(DtNmosResult Result, const char* Message)
{
    snprintf(LastError, sizeof(LastError), "%s", Message != NULL ? Message : "");
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosError_Fail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosError_Fail(DtNmosResult Code, const char* Format, ...)
{
    va_list Arguments;
    va_start(Arguments, Format);
    vsnprintf(LastError, sizeof(LastError), Format, Arguments);
    va_end(Arguments);
    return Code;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosError_CheckSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosError_CheckSize(size_t Size, size_t First, size_t Current,
                                 const char* What)
{
    if (Size == 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The Size of the %s is not set; set it to sizeof(%s).",
                              What, What);
    }
    if (Size < First)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "The Size of the %s is %zu, smaller than any version of it.", What, Size);
    }
    if (Size > Current)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "The Size of the %s is %zu, larger than the %zu this library "
            "knows: the library is older than the header.",
            What, Size, Current);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosError_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosError_Clear(void)
{
    LastError[0] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosError_FailMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosError_FailMemory(void)
{
    return NmosError_Fail(DTNMOS_E_NO_MEMORY, "Out of memory.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadUuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the 16 bytes of a UUID in its text form; returns 0 when text is no UUID.
//
static int ReadUuid(const char* Text, uint8_t Bytes[16])
{
    if (strlen(Text) != 36)
    {
        return 0;
    }
    size_t Byte = 0;
    for (size_t i = 0; i < 36;)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (Text[i] != '-')
            {
                return 0;
            }
            ++i;
            continue;
        }
        const NmosSpan Pair = {Text + i, 2};
        char Hex[5] = {'0', 'x', Pair.Data[0], Pair.Data[1], '\0'};
        uint8_t Value = 0;
        if (!NmosText_ParseByte(NmosSpan_Of(Hex), &Value))
        {
            return 0;
        }
        Bytes[Byte++] = Value;
        i += 2;
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosId_FromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosId_FromName(const DtNmosId* NamespaceId, const char* Name,
                               DtNmosId* Id)
{
    if (NamespaceId == NULL || Name == NULL || Id == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosId_FromName() needs a namespace, a name and an ID.");
    }
    uint8_t Space[16];
    if (!ReadUuid(NamespaceId->Text, Space))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The namespace '%.40s' is no UUID.", NamespaceId->Text);
    }
    NmosSha1 Sha1;
    NmosSha1_Init(&Sha1);
    NmosSha1_Update(&Sha1, Space, sizeof(Space));
    NmosSha1_Update(&Sha1, Name, strlen(Name));
    uint8_t Digest[20];
    NmosSha1_Final(&Sha1, Digest);
    // Version 5 in the high nibble of byte 6, and the variant of RFC 9562 in byte 8.
    Digest[6] = (uint8_t)((Digest[6] & 0x0F) | 0x50);
    Digest[8] = (uint8_t)((Digest[8] & 0x3F) | 0x80);
    snprintf(Id->Text, sizeof(Id->Text),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             Digest[0], Digest[1], Digest[2], Digest[3], Digest[4], Digest[5], Digest[6],
             Digest[7], Digest[8], Digest[9], Digest[10], Digest[11], Digest[12],
             Digest[13], Digest[14], Digest[15]);
    return DTNMOS_OK;
}
