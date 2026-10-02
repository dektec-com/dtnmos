// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosText.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Reading text
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosBuffer_Append -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosBuffer_Append(NmosBuffer* Buffer, const char* Text, size_t Length)
{
    if (Buffer->Failed)
    {
        return;
    }
    if (Buffer->Length + Length + 1 > Buffer->Capacity)
    {
        size_t Capacity = Buffer->Capacity < 256 ? 256 : Buffer->Capacity;
        while (Capacity < Buffer->Length + Length + 1)
        {
            Capacity *= 2;
        }
        char* Data = realloc(Buffer->Data, Capacity);
        if (Data == NULL)
        {
            Buffer->Failed = true;
            return;
        }
        Buffer->Data = Data;
        Buffer->Capacity = Capacity;
    }
    memcpy(Buffer->Data + Buffer->Length, Text, Length);
    Buffer->Length += Length;
    Buffer->Data[Buffer->Length] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosBuffer_Printf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosBuffer_Printf(NmosBuffer* Buffer, const char* Format, ...)
{
    char Local[256];
    va_list Arguments;
    va_start(Arguments, Format);
    const int Needed = vsnprintf(Local, sizeof(Local), Format, Arguments);
    va_end(Arguments);
    if (Needed < 0)
    {
        Buffer->Failed = true;
        return;
    }
    if ((size_t)Needed < sizeof(Local))
    {
        NmosBuffer_Append(Buffer, Local, (size_t)Needed);
        return;
    }
    char* Text = malloc((size_t)Needed + 1);
    if (Text == NULL)
    {
        Buffer->Failed = true;
        return;
    }
    va_start(Arguments, Format);
    vsnprintf(Text, (size_t)Needed + 1, Format, Arguments);
    va_end(Arguments);
    NmosBuffer_Append(Buffer, Text, (size_t)Needed);
    free(Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosBuffer_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosBuffer_Free(NmosBuffer* Buffer)
{
    free(Buffer->Data);
    memset(Buffer, 0, sizeof(*Buffer));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosSpan NmosSpan_Of(const char* Text)
{
    NmosSpan Span = {Text, Text == NULL ? 0 : strlen(Text)};
    return Span;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Trim -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosSpan NmosSpan_Trim(NmosSpan Span)
{
    while (Span.Length > 0 && (Span.Data[0] == ' ' || Span.Data[0] == '\t'))
    {
        ++Span.Data;
        --Span.Length;
    }
    while (Span.Length > 0 &&
           (Span.Data[Span.Length - 1] == ' ' || Span.Data[Span.Length - 1] == '\t'))
    {
        --Span.Length;
    }
    return Span;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Lower -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Equals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool NmosSpan_Equals(NmosSpan Span, const char* Text, bool Fold)
{
    const size_t Length = strlen(Text);
    if (Span.Length != Length)
    {
        return false;
    }
    for (size_t i = 0; i < Length; ++i)
    {
        const char a = Fold ? Lower(Span.Data[i]) : Span.Data[i];
        const char b = Fold ? Lower(Text[i]) : Text[i];
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_StartsWith -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool NmosSpan_StartsWith(NmosSpan Span, const char* Prefix)
{
    const size_t Length = strlen(Prefix);
    return Span.Length >= Length && memcmp(Span.Data, Prefix, Length) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSpan_Split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NmosSpan NmosSpan_Split(NmosSpan Span, char Separator, NmosSpan* Head)
{
    const char* Found =
        Span.Length == 0 ? NULL : memchr(Span.Data, Separator, Span.Length);
    if (Found == NULL)
    {
        *Head = Span;
        NmosSpan Rest = {NULL, 0};
        return Rest;
    }
    Head->Data = Span.Data;
    Head->Length = (size_t)(Found - Span.Data);
    NmosSpan Rest = {Found + 1, Span.Length - Head->Length - 1};
    return Rest;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseU64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool NmosText_ParseU64(NmosSpan Span, uint64_t Maximum, uint64_t* Value)
{
    if (Span.Length == 0)
    {
        return false;
    }
    uint64_t Result = 0;
    for (size_t i = 0; i < Span.Length; ++i)
    {
        const char c = Span.Data[i];
        if (c < '0' || c > '9')
        {
            return false;
        }
        const uint64_t Digit = (uint64_t)(c - '0');
        if (Result > (Maximum - Digit) / 10)
        {
            return false;
        }
        Result = Result * 10 + Digit;
    }
    *Value = Result;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseU32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool NmosText_ParseU32(NmosSpan Span, uint32_t Maximum, uint32_t* Value)
{
    uint64_t Result = 0;
    if (!NmosText_ParseU64(Span, Maximum, &Result))
    {
        return false;
    }
    *Value = (uint32_t)Result;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosText_ParseRate(NmosSpan Span, uint32_t* Numerator, uint32_t* Denominator)
{
    NmosSpan Head;
    const NmosSpan Rest = NmosSpan_Split(Span, '/', &Head);
    uint32_t n = 0;
    uint32_t d = 1;
    if (!NmosText_ParseU32(Head, UINT32_MAX, &n))
    {
        return false;
    }
    if (Rest.Data != NULL && (!NmosText_ParseU32(Rest, UINT32_MAX, &d) || d == 0))
    {
        return false;
    }
    *Numerator = n;
    *Denominator = d;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseMilliseconds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosText_ParseMilliseconds(NmosSpan Span, uint32_t* Nanoseconds)
{
    NmosSpan Whole;
    const NmosSpan Fraction = NmosSpan_Split(Span, '.', &Whole);
    uint32_t Milliseconds = 0;
    if (!NmosText_ParseU32(Whole, 4000, &Milliseconds))
    {
        return false;
    }
    uint32_t Result = Milliseconds * 1000000u;
    if (Fraction.Data != NULL)
    {
        if (Fraction.Length == 0 || Fraction.Length > 6)
        {
            return false;
        }
        uint32_t Digits = 0;
        if (!NmosText_ParseU32(Fraction, 999999, &Digits))
        {
            return false;
        }
        for (size_t i = Fraction.Length; i < 6; ++i)
        {
            Digits *= 10;
        }
        Result += Digits;
    }
    *Nanoseconds = Result;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_ParseByte -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool NmosText_ParseByte(NmosSpan Span, uint8_t* Value)
{
    if (Span.Length > 2 && Span.Data[0] == '0' &&
        (Span.Data[1] == 'x' || Span.Data[1] == 'X'))
    {
        unsigned Result = 0;
        for (size_t i = 2; i < Span.Length; ++i)
        {
            const char c = Lower(Span.Data[i]);
            unsigned Digit = 0;
            if (c >= '0' && c <= '9')
            {
                Digit = (unsigned)(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                Digit = (unsigned)(c - 'a' + 10);
            }
            else
            {
                return false;
            }
            Result = Result * 16 + Digit;
            if (Result > 255)
            {
                return false;
            }
        }
        *Value = (uint8_t)Result;
        return true;
    }
    uint32_t Result = 0;
    if (!NmosText_ParseU32(Span, 255, &Result))
    {
        return false;
    }
    *Value = (uint8_t)Result;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_CopySpan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool NmosText_CopySpan(char* Target, size_t Size, NmosSpan Span)
{
    if (Size == 0)
    {
        return false;
    }
    if (Span.Length >= Size)
    {
        Target[0] = '\0';
        return false;
    }
    if (Span.Length > 0)
    {
        memcpy(Target, Span.Data, Span.Length);
    }
    Target[Span.Length] = '\0';
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StorePiece -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Allocates size bytes that store owns; null when the memory ran out.
//
static void* StorePiece(NmosStore* Store, size_t Size)
{
    if (Store->Count == Store->Capacity)
    {
        const size_t Capacity = Store->Capacity == 0 ? 8 : Store->Capacity * 2;
        void** Pieces = realloc(Store->Pieces, Capacity * sizeof(*Pieces));
        if (Pieces == NULL)
        {
            return NULL;
        }
        Store->Pieces = Pieces;
        Store->Capacity = Capacity;
    }
    void* Piece = malloc(Size);
    if (Piece != NULL)
    {
        Store->Pieces[Store->Count++] = Piece;
    }
    return Piece;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosStore_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
char* NmosStore_Text(NmosStore* Store, const char* Data, size_t Length)
{
    char* Text = StorePiece(Store, Length + 1);
    if (Text != NULL)
    {
        if (Length > 0)
        {
            memcpy(Text, Data, Length);
        }
        Text[Length] = '\0';
    }
    return Text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosStore_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void* NmosStore_Copy(NmosStore* Store, const void* Data, size_t Size)
{
    if (Size == 0)
    {
        return NULL;
    }
    void* Copy = StorePiece(Store, Size);
    if (Copy != NULL)
    {
        memcpy(Copy, Data, Size);
    }
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosStore_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosStore_Free(NmosStore* Store)
{
    for (size_t i = 0; i < Store->Count; ++i)
    {
        free(Store->Pieces[i]);
    }
    free(Store->Pieces);
    memset(Store, 0, sizeof(*Store));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosText_CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosText_CopyText(char* Buffer, size_t* Size, const char* Data,
                               size_t Length)
{
    if (Buffer == NULL || *Size < Length + 1)
    {
        const size_t Had = *Size;
        *Size = Length + 1;
        return NmosError_Fail(DTNMOS_E_BUFFER_TOO_SMALL,
                              "The text needs %zu bytes, and the buffer has %zu.",
                              Length + 1, Buffer == NULL ? (size_t)0 : Had);
    }
    if (Length > 0)
    {
        memcpy(Buffer, Data, Length);
    }
    Buffer[Length] = '\0';
    *Size = Length;
    return DTNMOS_OK;
}
