// SPDX-License-Identifier: BSD-3-Clause
//
// DNS messages for DNS-SD: a query written label by label, and the records of a response
// read with the pointers of name compression followed, each within the message and at
// most a bounded number of times, so that a malformed message cannot loop or overrun.

#include "dns.h"

#include <string.h>

// The header of a message is 12 bytes; its flags have the QR bit for a response.
enum
{
  header_size = 12,
  flag_response = 0x8000,
  max_pointers = 64  // name compression pointers followed within one name
};

static void put16(uint8_t* at, uint16_t value)
{
  at[0] = (uint8_t)(value >> 8);
  at[1] = (uint8_t)value;
}

static uint16_t get16(const uint8_t* at)
{
  return (uint16_t)((at[0] << 8) | at[1]);
}

static uint32_t get32(const uint8_t* at)
{
  return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) | ((uint32_t)at[2] << 8) |
         at[3];
}

// Writes name, in which "\." and "\\" stand for a dot and a backslash within a label, as
// labels at buffer + *offset; returns 0 when it does not fit or a label is empty or
// longer than 63 bytes.
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

size_t dtnmos_dns_write_query(uint8_t* buffer, size_t size, uint16_t id,
                              const dtnmos_dns_question* questions, size_t count)
{
  if (buffer == NULL || size < header_size || count == 0 || count > 0xFFFF)
  {
    return 0;
  }
  memset(buffer, 0, header_size);
  put16(buffer, id);
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

// Reads the name at *offset into text, following compression pointers, and moves *offset
// past the name as it stands there. Returns 0 when the name is malformed or too long.
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
      return 0;  // the extended label types are not used
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

int dtnmos_dns_read_response(const uint8_t* message, size_t length,
                             void (*record)(void* user, const dtnmos_dns_record* found),
                             void* user)
{
  if (message == NULL || length < header_size ||
      (get16(message + 2) & flag_response) == 0)
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
    dtnmos_dns_record found;
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

static int lower(int c)
{
  return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

int dtnmos_dns_same_name(const char* a, const char* b)
{
  while (*a != '\0' && lower((unsigned char)*a) == lower((unsigned char)*b))
  {
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

int dtnmos_dns_txt_value(const uint8_t* txt, size_t length, const char* key, char* value,
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
