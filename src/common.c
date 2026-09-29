// SPDX-License-Identifier: BSD-3-Clause
//
// The version, the names of results and media, failures, and name-based UUIDs.

#include <stdio.h>
#include <string.h>

#include "dtnmos/sdp.h"
#include "internal.h"

void dtnmos_version(int* major, int* minor, int* patch)
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

const char* dtnmos_result_name(dtnmos_result result)
{
  switch (result)
  {
    case DTNMOS_OK:
      return "DTNMOS_OK";
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
  }
  return "unknown dtnmos_result";
}

const char* dtnmos_media_name(dtnmos_media media)
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

dtnmos_result dtnmos_fail(dtnmos_error* error, dtnmos_result code, const char* format,
                          ...)
{
  if (error != NULL)
  {
    error->code = code;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof(error->message), format, arguments);
    va_end(arguments);
  }
  return code;
}

dtnmos_result dtnmos_fail_memory(dtnmos_error* error)
{
  return dtnmos_fail(error, DTNMOS_E_NO_MEMORY, "Out of memory.");
}

// Reads the 16 bytes of a UUID in its text form; returns 0 when text is no UUID.
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

dtnmos_result dtnmos_id_from_name(const dtnmos_id* namespace_id, const char* name,
                                  dtnmos_id* id, dtnmos_error* error)
{
  if (namespace_id == NULL || name == NULL || id == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "dtnmos_id_from_name() needs a namespace, a name and an ID.");
  }
  uint8_t space[16];
  if (!read_uuid(namespace_id->text, space))
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "The namespace '%.40s' is no UUID.", namespace_id->text);
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
  snprintf(id->text, sizeof(id->text),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6],
           digest[7], digest[8], digest[9], digest[10], digest[11], digest[12],
           digest[13], digest[14], digest[15]);
  return DTNMOS_OK;
}
