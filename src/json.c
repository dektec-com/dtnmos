// SPDX-License-Identifier: BSD-3-Clause
//
// A parser of JSON (RFC 8259) into a tree of values, recursive to a limited depth, and
// the escaping of strings that dtnmos writes.

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// How deep arrays and objects may nest, which bounds the recursion.
#define DTNMOS_JSON_MAX_DEPTH 64

typedef struct reader
{
    const char* text;
    size_t length;
    size_t position;
    dtnmos_error* error;
} reader;

static dtnmos_result fail(reader* r, const char* what)
{
    return dtnmos_fail(r->error, DTNMOS_E_PARSE, "JSON at offset %zu: %s.", r->position,
                       what);
}

static void skip_space(reader* r)
{
    while (r->position < r->length)
    {
        const char c = r->text[r->position];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
            return;
        }
        ++r->position;
    }
}

static int next_is(reader* r, char c)
{
    return r->position < r->length && r->text[r->position] == c;
}

static int read_word(reader* r, const char* word)
{
    const size_t length = strlen(word);
    if (r->length - r->position < length ||
        memcmp(r->text + r->position, word, length) != 0)
    {
        return 0;
    }
    r->position += length;
    return 1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// Reads the four hexadecimal digits of \u.
static int read_hex4(reader* r, unsigned* code)
{
    if (r->length - r->position < 4)
    {
        return 0;
    }
    unsigned value = 0;
    for (int i = 0; i < 4; ++i)
    {
        const int digit = hex_value(r->text[r->position + (size_t)i]);
        if (digit < 0)
        {
            return 0;
        }
        value = value * 16 + (unsigned)digit;
    }
    r->position += 4;
    *code = value;
    return 1;
}

static void append_utf8(dtnmos_buffer* buffer, unsigned code)
{
    char bytes[4];
    size_t count = 0;
    if (code < 0x80)
    {
        bytes[count++] = (char)code;
    }
    else if (code < 0x800)
    {
        bytes[count++] = (char)(0xC0 | (code >> 6));
        bytes[count++] = (char)(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        bytes[count++] = (char)(0xE0 | (code >> 12));
        bytes[count++] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[count++] = (char)(0x80 | (code & 0x3F));
    }
    else
    {
        bytes[count++] = (char)(0xF0 | (code >> 18));
        bytes[count++] = (char)(0x80 | ((code >> 12) & 0x3F));
        bytes[count++] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[count++] = (char)(0x80 | (code & 0x3F));
    }
    dtnmos_buffer_append(buffer, bytes, count);
}

// Reads a string after its opening quote into a text of its own.
static dtnmos_result read_string(reader* r, char** text, size_t* length)
{
    dtnmos_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    DTNMOS_APPEND_LITERAL(&buffer, "");
    while (r->position < r->length)
    {
        const char c = r->text[r->position++];
        if (c == '"')
        {
            if (buffer.failed)
            {
                dtnmos_buffer_free(&buffer);
                return dtnmos_fail_memory(r->error);
            }
            *text = buffer.data;
            *length = buffer.length;
            return DTNMOS_OK;
        }
        if ((unsigned char)c < 0x20)
        {
            dtnmos_buffer_free(&buffer);
            return fail(r, "a string holds a control character");
        }
        if (c != '\\')
        {
            dtnmos_buffer_append(&buffer, &c, 1);
            continue;
        }
        if (r->position >= r->length)
        {
            break;
        }
        const char escape = r->text[r->position++];
        const char* simple = NULL;
        switch (escape)
        {
        case '"':
            simple = "\"";
            break;
        case '\\':
            simple = "\\";
            break;
        case '/':
            simple = "/";
            break;
        case 'b':
            simple = "\b";
            break;
        case 'f':
            simple = "\f";
            break;
        case 'n':
            simple = "\n";
            break;
        case 'r':
            simple = "\r";
            break;
        case 't':
            simple = "\t";
            break;
        case 'u':
        {
            unsigned code = 0;
            if (!read_hex4(r, &code))
            {
                dtnmos_buffer_free(&buffer);
                return fail(r, "\\u needs four hexadecimal digits");
            }
            // A high surrogate and the low one after it make one character beyond U+FFFF.
            if (code >= 0xD800 && code <= 0xDBFF)
            {
                unsigned low = 0;
                if (!read_word(r, "\\u") || !read_hex4(r, &low) || low < 0xDC00 ||
                    low > 0xDFFF)
                {
                    dtnmos_buffer_free(&buffer);
                    return fail(r, "a high surrogate lacks its low one");
                }
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            }
            append_utf8(&buffer, code);
            continue;
        }
        default:
            dtnmos_buffer_free(&buffer);
            return fail(r, "a string holds an unknown escape");
        }
        dtnmos_buffer_append(&buffer, simple, 1);
    }
    dtnmos_buffer_free(&buffer);
    return fail(r, "a string does not end");
}

static dtnmos_result read_number(reader* r, dtnmos_json* value)
{
    const size_t start = r->position;
    if (next_is(r, '-'))
    {
        ++r->position;
    }
    while (r->position < r->length)
    {
        const char c = r->text[r->position];
        if ((c < '0' || c > '9') && c != '.' && c != 'e' && c != 'E' && c != '+' &&
            c != '-')
        {
            break;
        }
        ++r->position;
    }
    const size_t length = r->position - start;
    char digits[64];
    if (length == 0 || length >= sizeof(digits))
    {
        return fail(r, "a number is malformed");
    }
    memcpy(digits, r->text + start, length);
    digits[length] = '\0';
    char* end = NULL;
    value->number = strtod(digits, &end);
    if (end != digits + length)
    {
        return fail(r, "a number is malformed");
    }
    value->type = DTNMOS_JSON_NUMBER;
    return DTNMOS_OK;
}

static dtnmos_result read_value(reader* r, dtnmos_json* value, int depth);

// Appends an item to a value, and for an object its key, which it takes over.
static dtnmos_json* add_item(dtnmos_json* value, char* key, size_t* capacity)
{
    if (value->count == *capacity)
    {
        const size_t grown = *capacity == 0 ? 8 : *capacity * 2;
        dtnmos_json* items = realloc(value->items, grown * sizeof(*items));
        if (items == NULL)
        {
            return NULL;
        }
        value->items = items;
        if (value->type == DTNMOS_JSON_OBJECT)
        {
            char** keys = realloc(value->keys, grown * sizeof(*keys));
            if (keys == NULL)
            {
                return NULL;
            }
            value->keys = keys;
        }
        *capacity = grown;
    }
    dtnmos_json* item = &value->items[value->count];
    memset(item, 0, sizeof(*item));
    if (value->type == DTNMOS_JSON_OBJECT)
    {
        value->keys[value->count] = key;
    }
    ++value->count;
    return item;
}

static dtnmos_result read_container(reader* r, dtnmos_json* value, int depth, char close)
{
    const int object = close == '}';
    value->type = object ? DTNMOS_JSON_OBJECT : DTNMOS_JSON_ARRAY;
    size_t capacity = 0;
    skip_space(r);
    if (next_is(r, close))
    {
        ++r->position;
        return DTNMOS_OK;
    }
    for (;;)
    {
        char* key = NULL;
        if (object)
        {
            skip_space(r);
            if (!next_is(r, '"'))
            {
                return fail(r, "an object needs a string as key");
            }
            ++r->position;
            size_t key_length = 0;
            dtnmos_result result = read_string(r, &key, &key_length);
            if (result != DTNMOS_OK)
            {
                return result;
            }
            skip_space(r);
            if (!next_is(r, ':'))
            {
                free(key);
                return fail(r, "a key needs a colon after it");
            }
            ++r->position;
        }
        dtnmos_json* item = add_item(value, key, &capacity);
        if (item == NULL)
        {
            free(key);
            return dtnmos_fail_memory(r->error);
        }
        dtnmos_result result = read_value(r, item, depth + 1);
        if (result != DTNMOS_OK)
        {
            return result;
        }
        skip_space(r);
        if (next_is(r, ','))
        {
            ++r->position;
            continue;
        }
        if (next_is(r, close))
        {
            ++r->position;
            return DTNMOS_OK;
        }
        return fail(r, object ? "an object needs , or }" : "an array needs , or ]");
    }
}

static dtnmos_result read_value(reader* r, dtnmos_json* value, int depth)
{
    if (depth > DTNMOS_JSON_MAX_DEPTH)
    {
        return fail(r, "the values nest too deep");
    }
    skip_space(r);
    if (r->position >= r->length)
    {
        return fail(r, "a value is missing");
    }
    const char c = r->text[r->position];
    if (c == '{' || c == '[')
    {
        ++r->position;
        return read_container(r, value, depth, c == '{' ? '}' : ']');
    }
    if (c == '"')
    {
        ++r->position;
        value->type = DTNMOS_JSON_STRING;
        return read_string(r, &value->string, &value->string_length);
    }
    if (read_word(r, "null"))
    {
        value->type = DTNMOS_JSON_NULL;
        return DTNMOS_OK;
    }
    if (read_word(r, "true"))
    {
        value->type = DTNMOS_JSON_TRUE;
        return DTNMOS_OK;
    }
    if (read_word(r, "false"))
    {
        value->type = DTNMOS_JSON_FALSE;
        return DTNMOS_OK;
    }
    return read_number(r, value);
}

static void clear_value(dtnmos_json* value)
{
    free(value->string);
    for (size_t i = 0; i < value->count; ++i)
    {
        clear_value(&value->items[i]);
        if (value->type == DTNMOS_JSON_OBJECT)
        {
            free(value->keys[i]);
        }
    }
    free(value->items);
    free(value->keys);
}

dtnmos_result dtnmos_json_parse(const char* text, size_t length, dtnmos_json** value,
                                dtnmos_error* error)
{
    if (value == NULL || (text == NULL && length > 0))
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_json_parse() needs text.");
    }
    *value = NULL;
    dtnmos_json* root = calloc(1, sizeof(*root));
    if (root == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    reader r = {text, length, 0, error};
    dtnmos_result result = read_value(&r, root, 0);
    if (result == DTNMOS_OK)
    {
        skip_space(&r);
        if (r.position != r.length)
        {
            result = fail(&r, "text follows the value");
        }
    }
    if (result != DTNMOS_OK)
    {
        dtnmos_json_free(root);
        return result;
    }
    *value = root;
    return DTNMOS_OK;
}

void dtnmos_json_free(dtnmos_json* value)
{
    if (value == NULL)
    {
        return;
    }
    clear_value(value);
    free(value);
}

const dtnmos_json* dtnmos_json_member(const dtnmos_json* value, const char* key)
{
    if (value == NULL || value->type != DTNMOS_JSON_OBJECT || key == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < value->count; ++i)
    {
        if (strcmp(value->keys[i], key) == 0)
        {
            return &value->items[i];
        }
    }
    return NULL;
}

const char* dtnmos_json_text(const dtnmos_json* value)
{
    return value != NULL && value->type == DTNMOS_JSON_STRING ? value->string : NULL;
}

const char* dtnmos_json_member_text(const dtnmos_json* value, const char* key)
{
    return dtnmos_json_text(dtnmos_json_member(value, key));
}

void dtnmos_json_write_string(dtnmos_buffer* buffer, const char* text)
{
    DTNMOS_APPEND_LITERAL(buffer, "\"");
    for (const char* c = text == NULL ? "" : text; *c != '\0'; ++c)
    {
        switch (*c)
        {
        case '"':
            DTNMOS_APPEND_LITERAL(buffer, "\\\"");
            break;
        case '\\':
            DTNMOS_APPEND_LITERAL(buffer, "\\\\");
            break;
        case '\n':
            DTNMOS_APPEND_LITERAL(buffer, "\\n");
            break;
        case '\r':
            DTNMOS_APPEND_LITERAL(buffer, "\\r");
            break;
        case '\t':
            DTNMOS_APPEND_LITERAL(buffer, "\\t");
            break;
        default:
            if ((unsigned char)*c < 0x20)
            {
                dtnmos_buffer_printf(buffer, "\\u%04x", (unsigned)(unsigned char)*c);
            }
            else
            {
                dtnmos_buffer_append(buffer, c, 1);
            }
            break;
        }
    }
    DTNMOS_APPEND_LITERAL(buffer, "\"");
}
