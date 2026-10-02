// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosJson.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A parser of JSON (RFC 8259) of limited depth, and the escaping of strings
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "NmosJson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// How deep arrays and objects may nest, which bounds the recursion.
#define DTNMOS_JSON_MAX_DEPTH 64

typedef struct NmosReader
{
    const char* Text;
    size_t Length;
    size_t Position;
} NmosReader;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult Fail(NmosReader* r, const char* What)
{
    return NmosError_Fail(DTNMOS_E_PARSE, "JSON at offset %zu: %s.", r->Position, What);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SkipSpace -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void SkipSpace(NmosReader* r)
{
    while (r->Position < r->Length)
    {
        const char c = r->Text[r->Position];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
            return;
        }
        ++r->Position;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextIs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool NextIs(NmosReader* r, char c)
{
    return r->Position < r->Length && r->Text[r->Position] == c;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool ReadWord(NmosReader* r, const char* Word)
{
    const size_t Length = strlen(Word);
    if (r->Length - r->Position < Length ||
        memcmp(r->Text + r->Position, Word, Length) != 0)
    {
        return false;
    }
    r->Position += Length;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HexValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int HexValue(char c)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadHex4 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the four hexadecimal digits of \u.
//
static bool ReadHex4(NmosReader* r, unsigned* Code)
{
    if (r->Length - r->Position < 4)
    {
        return false;
    }
    unsigned Value = 0;
    for (int i = 0; i < 4; ++i)
    {
        const int Digit = HexValue(r->Text[r->Position + (size_t)i]);
        if (Digit < 0)
        {
            return false;
        }
        Value = Value * 16 + (unsigned)Digit;
    }
    r->Position += 4;
    *Code = Value;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AppendUtf8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void AppendUtf8(NmosBuffer* Buffer, unsigned Code)
{
    char Bytes[4];
    size_t Count = 0;
    if (Code < 0x80)
    {
        Bytes[Count++] = (char)Code;
    }
    else if (Code < 0x800)
    {
        Bytes[Count++] = (char)(0xC0 | (Code >> 6));
        Bytes[Count++] = (char)(0x80 | (Code & 0x3F));
    }
    else if (Code < 0x10000)
    {
        Bytes[Count++] = (char)(0xE0 | (Code >> 12));
        Bytes[Count++] = (char)(0x80 | ((Code >> 6) & 0x3F));
        Bytes[Count++] = (char)(0x80 | (Code & 0x3F));
    }
    else
    {
        Bytes[Count++] = (char)(0xF0 | (Code >> 18));
        Bytes[Count++] = (char)(0x80 | ((Code >> 12) & 0x3F));
        Bytes[Count++] = (char)(0x80 | ((Code >> 6) & 0x3F));
        Bytes[Count++] = (char)(0x80 | (Code & 0x3F));
    }
    NmosBuffer_Append(Buffer, Bytes, Count);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a string after its opening quote into a text of its own.
//
static DtNmosResult ReadString(NmosReader* r, char** Text, size_t* Length)
{
    NmosBuffer Buffer;
    memset(&Buffer, 0, sizeof(Buffer));
    DTNMOS_APPEND_LITERAL(&Buffer, "");
    while (r->Position < r->Length)
    {
        const char c = r->Text[r->Position++];
        if (c == '"')
        {
            if (Buffer.Failed)
            {
                NmosBuffer_Free(&Buffer);
                return NmosError_FailMemory();
            }
            *Text = Buffer.Data;
            *Length = Buffer.Length;
            return DTNMOS_OK;
        }
        if ((unsigned char)c < 0x20)
        {
            NmosBuffer_Free(&Buffer);
            return Fail(r, "a string holds a control character");
        }
        if (c != '\\')
        {
            NmosBuffer_Append(&Buffer, &c, 1);
            continue;
        }
        if (r->Position >= r->Length)
        {
            break;
        }
        const char Escaped = r->Text[r->Position++];
        const char* Simple = NULL;
        switch (Escaped)
        {
        case '"':
            Simple = "\"";
            break;
        case '\\':
            Simple = "\\";
            break;
        case '/':
            Simple = "/";
            break;
        case 'b':
            Simple = "\b";
            break;
        case 'f':
            Simple = "\f";
            break;
        case 'n':
            Simple = "\n";
            break;
        case 'r':
            Simple = "\r";
            break;
        case 't':
            Simple = "\t";
            break;
        case 'u':
        {
            unsigned Code = 0;
            if (!ReadHex4(r, &Code))
            {
                NmosBuffer_Free(&Buffer);
                return Fail(r, "\\u needs four hexadecimal digits");
            }
            // A high surrogate and the low one after it make one character beyond U+FFFF.
            if (Code >= 0xD800 && Code <= 0xDBFF)
            {
                unsigned Low = 0;
                if (!ReadWord(r, "\\u") || !ReadHex4(r, &Low) || Low < 0xDC00 ||
                    Low > 0xDFFF)
                {
                    NmosBuffer_Free(&Buffer);
                    return Fail(r, "a high surrogate lacks its low one");
                }
                Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
            }
            AppendUtf8(&Buffer, Code);
            continue;
        }
        default:
            NmosBuffer_Free(&Buffer);
            return Fail(r, "a string holds an unknown escape");
        }
        NmosBuffer_Append(&Buffer, Simple, 1);
    }
    NmosBuffer_Free(&Buffer);
    return Fail(r, "a string does not end");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ReadNumber(NmosReader* r, NmosJson* Value)
{
    const size_t Start = r->Position;
    if (NextIs(r, '-'))
    {
        ++r->Position;
    }
    while (r->Position < r->Length)
    {
        const char c = r->Text[r->Position];
        if ((c < '0' || c > '9') && c != '.' && c != 'e' && c != 'E' && c != '+' &&
            c != '-')
        {
            break;
        }
        ++r->Position;
    }
    const size_t Length = r->Position - Start;
    char Digits[64];
    if (Length == 0 || Length >= sizeof(Digits))
    {
        return Fail(r, "a number is malformed");
    }
    memcpy(Digits, r->Text + Start, Length);
    Digits[Length] = '\0';
    char* End = NULL;
    Value->Number = strtod(Digits, &End);
    if (End != Digits + Length)
    {
        return Fail(r, "a number is malformed");
    }
    Value->Type = DTNMOS_JSON_NUMBER;
    return DTNMOS_OK;
}

static DtNmosResult ReadValue(NmosReader* r, NmosJson* Value, int Depth);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddItem -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Appends an item to a value, and for an object its key, which it takes over.
//
static NmosJson* AddItem(NmosJson* Value, char* Key, size_t* Capacity)
{
    if (Value->Count == *Capacity)
    {
        const size_t Grown = *Capacity == 0 ? 8 : *Capacity * 2;
        NmosJson* Items = realloc(Value->Items, Grown * sizeof(*Items));
        if (Items == NULL)
        {
            return NULL;
        }
        Value->Items = Items;
        if (Value->Type == DTNMOS_JSON_OBJECT)
        {
            char** Keys = realloc(Value->Keys, Grown * sizeof(*Keys));
            if (Keys == NULL)
            {
                return NULL;
            }
            Value->Keys = Keys;
        }
        *Capacity = Grown;
    }
    NmosJson* Item = &Value->Items[Value->Count];
    memset(Item, 0, sizeof(*Item));
    if (Value->Type == DTNMOS_JSON_OBJECT)
    {
        Value->Keys[Value->Count] = Key;
    }
    ++Value->Count;
    return Item;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadContainer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult ReadContainer(NmosReader* r, NmosJson* Value, int Depth, char Close)
{
    const bool Object = Close == '}';
    Value->Type = Object ? DTNMOS_JSON_OBJECT : DTNMOS_JSON_ARRAY;
    size_t Capacity = 0;
    SkipSpace(r);
    if (NextIs(r, Close))
    {
        ++r->Position;
        return DTNMOS_OK;
    }
    for (;;)
    {
        char* Key = NULL;
        if (Object)
        {
            SkipSpace(r);
            if (!NextIs(r, '"'))
            {
                return Fail(r, "an object needs a string as key");
            }
            ++r->Position;
            size_t KeyLength = 0;
            DtNmosResult Result = ReadString(r, &Key, &KeyLength);
            if (Result != DTNMOS_OK)
            {
                return Result;
            }
            SkipSpace(r);
            if (!NextIs(r, ':'))
            {
                free(Key);
                return Fail(r, "a key needs a colon after it");
            }
            ++r->Position;
        }
        NmosJson* Item = AddItem(Value, Key, &Capacity);
        if (Item == NULL)
        {
            free(Key);
            return NmosError_FailMemory();
        }
        DtNmosResult Result = ReadValue(r, Item, Depth + 1);
        if (Result != DTNMOS_OK)
        {
            return Result;
        }
        SkipSpace(r);
        if (NextIs(r, ','))
        {
            ++r->Position;
            continue;
        }
        if (NextIs(r, Close))
        {
            ++r->Position;
            return DTNMOS_OK;
        }
        return Fail(r, Object ? "an object needs , or }" : "an array needs , or ]");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult ReadValue(NmosReader* r, NmosJson* Value, int Depth)
{
    if (Depth > DTNMOS_JSON_MAX_DEPTH)
    {
        return Fail(r, "the values nest too deep");
    }
    SkipSpace(r);
    if (r->Position >= r->Length)
    {
        return Fail(r, "a value is missing");
    }
    const char c = r->Text[r->Position];
    if (c == '{' || c == '[')
    {
        ++r->Position;
        return ReadContainer(r, Value, Depth, c == '{' ? '}' : ']');
    }
    if (c == '"')
    {
        ++r->Position;
        Value->Type = DTNMOS_JSON_STRING;
        return ReadString(r, &Value->String, &Value->StringLength);
    }
    if (ReadWord(r, "null"))
    {
        Value->Type = DTNMOS_JSON_NULL;
        return DTNMOS_OK;
    }
    if (ReadWord(r, "true"))
    {
        Value->Type = DTNMOS_JSON_TRUE;
        return DTNMOS_OK;
    }
    if (ReadWord(r, "false"))
    {
        Value->Type = DTNMOS_JSON_FALSE;
        return DTNMOS_OK;
    }
    return ReadNumber(r, Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ClearValue(NmosJson* Value)
{
    free(Value->String);
    for (size_t i = 0; i < Value->Count; ++i)
    {
        ClearValue(&Value->Items[i]);
        if (Value->Type == DTNMOS_JSON_OBJECT)
        {
            free(Value->Keys[i]);
        }
    }
    free(Value->Items);
    free(Value->Keys);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosJson_Parse(const char* Text, size_t Length, NmosJson** Value)
{
    if (Value == NULL || (Text == NULL && Length > 0))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "NmosJson_Parse() needs text.");
    }
    *Value = NULL;
    NmosJson* Root = calloc(1, sizeof(*Root));
    if (Root == NULL)
    {
        return NmosError_FailMemory();
    }
    NmosReader r = {Text, Length, 0};
    DtNmosResult Result = ReadValue(&r, Root, 0);
    if (Result == DTNMOS_OK)
    {
        SkipSpace(&r);
        if (r.Position != r.Length)
        {
            Result = Fail(&r, "text follows the value");
        }
    }
    if (Result != DTNMOS_OK)
    {
        NmosJson_Free(Root);
        return Result;
    }
    *Value = Root;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosJson_Free(NmosJson* Value)
{
    if (Value == NULL)
    {
        return;
    }
    ClearValue(Value);
    free(Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_Member -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const NmosJson* NmosJson_Member(const NmosJson* Value, const char* Key)
{
    if (Value == NULL || Value->Type != DTNMOS_JSON_OBJECT || Key == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < Value->Count; ++i)
    {
        if (strcmp(Value->Keys[i], Key) == 0)
        {
            return &Value->Items[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* NmosJson_Text(const NmosJson* Value)
{
    return Value != NULL && Value->Type == DTNMOS_JSON_STRING ? Value->String : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_MemberText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* NmosJson_MemberText(const NmosJson* Value, const char* Key)
{
    return NmosJson_Text(NmosJson_Member(Value, Key));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_WriteString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosJson_WriteString(NmosBuffer* Buffer, const char* Text)
{
    DTNMOS_APPEND_LITERAL(Buffer, "\"");
    for (const char* c = Text == NULL ? "" : Text; *c != '\0'; ++c)
    {
        switch (*c)
        {
        case '"':
            DTNMOS_APPEND_LITERAL(Buffer, "\\\"");
            break;
        case '\\':
            DTNMOS_APPEND_LITERAL(Buffer, "\\\\");
            break;
        case '\n':
            DTNMOS_APPEND_LITERAL(Buffer, "\\n");
            break;
        case '\r':
            DTNMOS_APPEND_LITERAL(Buffer, "\\r");
            break;
        case '\t':
            DTNMOS_APPEND_LITERAL(Buffer, "\\t");
            break;
        default:
            if ((unsigned char)*c < 0x20)
            {
                NmosBuffer_Printf(Buffer, "\\u%04x", (unsigned)(unsigned char)*c);
            }
            else
            {
                NmosBuffer_Append(Buffer, c, 1);
            }
            break;
        }
    }
    DTNMOS_APPEND_LITERAL(Buffer, "\"");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosJson_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosJson_Write(NmosBuffer* Buffer, const NmosJson* Value)
{
    switch (Value->Type)
    {
    case DTNMOS_JSON_NULL:
        DTNMOS_APPEND_LITERAL(Buffer, "null");
        break;
    case DTNMOS_JSON_FALSE:
        DTNMOS_APPEND_LITERAL(Buffer, "false");
        break;
    case DTNMOS_JSON_TRUE:
        DTNMOS_APPEND_LITERAL(Buffer, "true");
        break;
    case DTNMOS_JSON_NUMBER:
        // Seventeen digits give a double back exactly; a whole number has none after the
        // point.
        NmosBuffer_Printf(Buffer, "%.17g", Value->Number);
        break;
    case DTNMOS_JSON_STRING:
        NmosJson_WriteString(Buffer, Value->String);
        break;
    case DTNMOS_JSON_ARRAY:
        DTNMOS_APPEND_LITERAL(Buffer, "[");
        for (size_t i = 0; i < Value->Count; ++i)
        {
            if (i > 0)
            {
                DTNMOS_APPEND_LITERAL(Buffer, ",");
            }
            NmosJson_Write(Buffer, &Value->Items[i]);
        }
        DTNMOS_APPEND_LITERAL(Buffer, "]");
        break;
    case DTNMOS_JSON_OBJECT:
        DTNMOS_APPEND_LITERAL(Buffer, "{");
        for (size_t i = 0; i < Value->Count; ++i)
        {
            if (i > 0)
            {
                DTNMOS_APPEND_LITERAL(Buffer, ",");
            }
            NmosJson_WriteString(Buffer, Value->Keys[i]);
            DTNMOS_APPEND_LITERAL(Buffer, ":");
            NmosJson_Write(Buffer, &Value->Items[i]);
        }
        DTNMOS_APPEND_LITERAL(Buffer, "}");
        break;
    }
}
