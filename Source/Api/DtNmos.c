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
void DtNmos_Version(int* major, int* minor, int* patch)
{
    if (major != NULL)
    {
        *major = DTNMOS_VERSION_MAJOR;
    }
    if (minor != NULL)
    {
        *minor = DTNMOS_VERSION_MINOR;
    }
    if (patch != NULL)
    {
        *patch = DTNMOS_VERSION_PATCH;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosResult_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosResult_Name(DtNmosResult result)
{
    switch (result)
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
const char* DtNmosMedia_Name(DtNmosMedia media)
{
    switch (media)
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
static NMOS_THREAD_LOCAL char last_error[512];

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_GetLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmos_GetLastError(void)
{
    return last_error;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_SetLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_SetLastError(DtNmosResult result, const char* message)
{
    snprintf(last_error, sizeof(last_error), "%s", message != NULL ? message : "");
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_fail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult dtnmos_fail(DtNmosResult code, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(last_error, sizeof(last_error), format, arguments);
    va_end(arguments);
    return code;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_clear_error -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_clear_error(void)
{
    last_error[0] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_fail_memory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult dtnmos_fail_memory(void)
{
    return dtnmos_fail(DTNMOS_E_NO_MEMORY, "Out of memory.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_uuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the 16 bytes of a UUID in its text form; returns 0 when text is no UUID.
//
static int read_uuid(const char* text, uint8_t bytes[16])
{
    if (strlen(text) != 36)
    {
        return 0;
    }
    size_t byte = 0;
    for (size_t i = 0; i < 36;)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (text[i] != '-')
            {
                return 0;
            }
            ++i;
            continue;
        }
        const dtnmos_span pair = {text + i, 2};
        char hex[5] = {'0', 'x', pair.data[0], pair.data[1], '\0'};
        uint8_t value = 0;
        if (!dtnmos_parse_byte(dtnmos_span_of(hex), &value))
        {
            return 0;
        }
        bytes[byte++] = value;
        i += 2;
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosId_FromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosId_FromName(const DtNmosId* namespace_id, const char* name,
                               DtNmosId* id)
{
    if (namespace_id == NULL || name == NULL || id == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosId_FromName() needs a namespace, a name and an ID.");
    }
    uint8_t space[16];
    if (!read_uuid(namespace_id->Text, space))
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT, "The namespace '%.40s' is no UUID.",
                           namespace_id->Text);
    }
    dtnmos_sha1 sha1;
    dtnmos_sha1_init(&sha1);
    dtnmos_sha1_update(&sha1, space, sizeof(space));
    dtnmos_sha1_update(&sha1, name, strlen(name));
    uint8_t digest[20];
    dtnmos_sha1_final(&sha1, digest);
    // Version 5 in the high nibble of byte 6, and the variant of RFC 9562 in byte 8.
    digest[6] = (uint8_t)((digest[6] & 0x0F) | 0x50);
    digest[8] = (uint8_t)((digest[8] & 0x3F) | 0x80);
    snprintf(id->Text, sizeof(id->Text),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6],
             digest[7], digest[8], digest[9], digest[10], digest[11], digest[12],
             digest[13], digest[14], digest[15]);
    return DTNMOS_OK;
}
