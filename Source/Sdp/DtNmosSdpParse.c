// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosSdpParse.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Reading an SDP (RFC 8866) as SMPTE ST 2110 writes it
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosFlow.h"

struct DtNmosSdp
{
    DtNmosSession Session;
    DtNmosFlow* Flows;
    size_t Count;
    NmosStore Store; // the strings and arrays of the session and the flows
};

// The attributes that the session level and a media section both may carry.
typedef struct NmosSharedLines
{
    NmosSpan Connection; // the address of c=, without TTL
    size_t ConnectionLine;
    NmosSpan FilterSource; // the first source of a=source-filter: incl
    NmosSpan TsRefclk;
    NmosSpan Mediaclk; // the value of a=mediaclk
} NmosSharedLines;

// A media section as its lines give it.
typedef struct NmosSection
{
    size_t Line; // of its m=
    uint16_t Port;
    uint8_t PayloadType;
    NmosSharedLines Shared;
    NmosSpan Encoding; // of the a=rtpmap of payload_type
    uint32_t ClockRate;
    uint32_t Channels; // the encoding parameters of a=rtpmap; 0 when absent
    NmosSpan Fmtp;
    size_t FmtpLine;
    NmosSpan Ptime;
    size_t PtimeLine;
    NmosSpan Mid;
    uint64_t BandwidthKbps;
} NmosSection;

typedef struct NmosParser
{
    const char* Text;
    size_t Length;
    size_t Position;
    size_t Line; // number of the current line, from 1
    DtNmosSession* Session;
    NmosStore* Store; // of the DtNmosSdp being read
    NmosSharedLines SessionLines;
    NmosSpan GroupSecond[16]; // the second mid of each a=group:DUP
    size_t GroupCount;
    NmosSection* Sections;
    size_t Count;
    size_t Capacity;
} NmosParser;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FailLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult FailLine(NmosParser* p, const char* What, NmosSpan Text)
{
    return NmosError_Fail(DTNMOS_E_PARSE, "SDP line %zu: %s: '%.*s'", p->Line, What,
                          (int)(Text.Length > 120 ? 120 : Text.Length), Text.Data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FailAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult FailAt(size_t Line, const char* What, NmosSpan Text)
{
    return NmosError_Fail(DTNMOS_E_PARSE, "SDP line %zu: %s: '%.*s'", Line, What,
                          (int)(Text.Length > 120 ? 120 : Text.Length), Text.Data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the next line without its end of line, or 0 at the end of the text.
//
static bool NextLine(NmosParser* p, NmosSpan* Line)
{
    if (p->Position >= p->Length)
    {
        return false;
    }
    const char* Start = p->Text + p->Position;
    const char* End = memchr(Start, '\n', p->Length - p->Position);
    size_t Length = End == NULL ? p->Length - p->Position : (size_t)(End - Start);
    p->Position += Length + (End == NULL ? 0 : 1);
    if (Length > 0 && Start[Length - 1] == '\r')
    {
        --Length;
    }
    Line->Data = Start;
    Line->Length = Length;
    ++p->Line;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Splits span at its first space: word is what lies before it, and the rest follows,
// trimmed.
//
static NmosSpan NextWord(NmosSpan Span, NmosSpan* Word)
{
    Span = NmosSpan_Trim(Span);
    const NmosSpan Rest = NmosSpan_Split(Span, ' ', Word);
    return NmosSpan_Trim(Rest);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadConnection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ReadConnection(NmosParser* p, NmosSpan Value, NmosSharedLines* Lines)
{
    NmosSpan Network;
    NmosSpan Type;
    NmosSpan Address;
    NmosSpan Rest = NextWord(Value, &Network);
    Rest = NextWord(Rest, &Type);
    NextWord(Rest, &Address);
    if (!NmosSpan_Equals(Network, "IN", false) ||
        (!NmosSpan_Equals(Type, "IP4", false) && !NmosSpan_Equals(Type, "IP6", false)) ||
        Address.Length == 0)
    {
        return FailLine(p, "c= needs IN IP4 or IN IP6 and an address", Value);
    }
    // A multicast address of IPv4 carries its TTL, and either may carry a count, after
    // '/'.
    NmosSpan Host;
    NmosSpan_Split(Address, '/', &Host);
    Lines->Connection = Host;
    Lines->ConnectionLine = p->Line;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadOrigin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ReadOrigin(NmosParser* p, NmosSpan Value)
{
    NmosSpan User;
    NmosSpan Id;
    NmosSpan Version;
    NmosSpan Network;
    NmosSpan Type;
    NmosSpan Address;
    NmosSpan Rest = NextWord(Value, &User);
    Rest = NextWord(Rest, &Id);
    Rest = NextWord(Rest, &Version);
    Rest = NextWord(Rest, &Network);
    Rest = NextWord(Rest, &Type);
    NextWord(Rest, &Address);
    if (!NmosText_ParseU64(Id, UINT64_MAX, &p->Session->SessionId) ||
        !NmosText_ParseU64(Version, UINT64_MAX, &p->Session->SessionVersion) ||
        Address.Length == 0)
    {
        return FailLine(p, "o= needs a user, a session ID, a version and an address",
                        Value);
    }
    if (!NmosText_CopySpan(p->Session->OriginIp, sizeof(p->Session->OriginIp), Address))
    {
        return FailLine(p, "o= has an address longer than a domain name may be", Value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadMedia -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult ReadMedia(NmosParser* p, NmosSpan Value)
{
    if (p->Count == p->Capacity)
    {
        const size_t Capacity = p->Capacity == 0 ? 4 : p->Capacity * 2;
        NmosSection* Sections = realloc(p->Sections, Capacity * sizeof(*Sections));
        if (Sections == NULL)
        {
            return NmosError_FailMemory();
        }
        p->Sections = Sections;
        p->Capacity = Capacity;
    }
    NmosSection* s = &p->Sections[p->Count];
    memset(s, 0, sizeof(*s));
    s->Line = p->Line;
    NmosSpan Media;
    NmosSpan Port;
    NmosSpan Protocol;
    NmosSpan Format;
    NmosSpan Rest = NextWord(Value, &Media);
    Rest = NextWord(Rest, &Port);
    Rest = NextWord(Rest, &Protocol);
    NextWord(Rest, &Format);
    NmosSpan PortNumber;
    NmosSpan_Split(Port, '/', &PortNumber);
    uint32_t Number = 0;
    uint32_t PayloadType = 0;
    if (Media.Length == 0 || !NmosText_ParseU32(PortNumber, 65535, &Number) ||
        Protocol.Length == 0 || !NmosText_ParseU32(Format, 127, &PayloadType))
    {
        return FailLine(p, "m= needs a media, a port, a protocol and a payload type",
                        Value);
    }
    s->Port = (uint16_t)Number;
    s->PayloadType = (uint8_t)PayloadType;
    ++p->Count;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPayloadType -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the payload type that starts value, as a=rtpmap and a=fmtp begin, into
// payload_type, and returns what follows it.
//
static bool ReadPayloadType(NmosSpan Value, uint32_t* PayloadType, NmosSpan* Rest)
{
    NmosSpan Number;
    *Rest = NextWord(Value, &Number);
    return NmosText_ParseU32(Number, 127, PayloadType);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadRtpmap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ReadRtpmap(NmosParser* p, NmosSection* s, NmosSpan Value)
{
    uint32_t PayloadType = 0;
    NmosSpan Map;
    if (!ReadPayloadType(Value, &PayloadType, &Map))
    {
        return FailLine(p, "a=rtpmap needs a payload type", Value);
    }
    if (PayloadType != s->PayloadType)
    {
        return DTNMOS_OK;
    }
    NmosSpan Encoding;
    NmosSpan Clock;
    NmosSpan Rest = NmosSpan_Split(Map, '/', &Encoding);
    const NmosSpan Parameters = NmosSpan_Split(Rest, '/', &Clock);
    s->Encoding = NmosSpan_Trim(Encoding);
    if (s->Encoding.Length == 0 || !NmosText_ParseU32(Clock, UINT32_MAX, &s->ClockRate) ||
        (Parameters.Data != NULL && !NmosText_ParseU32(Parameters, 65535, &s->Channels)))
    {
        return FailLine(
            p, "a=rtpmap needs an encoding, a clock rate and optional channels", Value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadSourceFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult ReadSourceFilter(NmosParser* p, NmosSpan Value,
                                     NmosSharedLines* Lines)
{
    // a=source-filter: incl IN IP4 <destination> <source> ...
    NmosSpan Mode;
    NmosSpan Network;
    NmosSpan Type;
    NmosSpan Destination;
    NmosSpan Source;
    NmosSpan Rest = NextWord(Value, &Mode);
    Rest = NextWord(Rest, &Network);
    Rest = NextWord(Rest, &Type);
    Rest = NextWord(Rest, &Destination);
    NextWord(Rest, &Source);
    if (Mode.Length == 0 || Network.Length == 0 || Type.Length == 0 ||
        Destination.Length == 0 || Source.Length == 0)
    {
        return FailLine(
            p,
            "a=source-filter needs a mode, IN, an address type, a destination "
            "and a source",
            Value);
    }
    if (NmosSpan_Equals(Mode, "incl", true) && Lines->FilterSource.Length == 0)
    {
        Lines->FilterSource = Source;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadAttribute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads an attribute into the lines of the session or of the current section.
//
static DtNmosResult ReadAttribute(NmosParser* p, NmosSpan Line)
{
    NmosSpan Name;
    const NmosSpan Value = NmosSpan_Split(Line, ':', &Name);
    NmosSection* s = p->Count == 0 ? NULL : &p->Sections[p->Count - 1];
    NmosSharedLines* Lines = s == NULL ? &p->SessionLines : &s->Shared;
    if (NmosSpan_Equals(Name, "source-filter", false))
    {
        return ReadSourceFilter(p, Value, Lines);
    }
    if (NmosSpan_Equals(Name, "ts-refclk", false))
    {
        if (Lines->TsRefclk.Length == 0)
        {
            Lines->TsRefclk = NmosSpan_Trim(Value);
        }
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(Name, "mediaclk", false))
    {
        Lines->Mediaclk = NmosSpan_Trim(Value);
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(Name, "group", false))
    {
        NmosSpan Semantics;
        NmosSpan Rest = NextWord(Value, &Semantics);
        NmosSpan First;
        NmosSpan Second;
        Rest = NextWord(Rest, &First);
        NextWord(Rest, &Second);
        if (NmosSpan_Equals(Semantics, "DUP", false) && Second.Length > 0 &&
            p->GroupCount < sizeof(p->GroupSecond) / sizeof(p->GroupSecond[0]))
        {
            p->GroupSecond[p->GroupCount++] = Second;
        }
        return DTNMOS_OK;
    }
    if (s == NULL)
    {
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(Name, "rtpmap", false))
    {
        return ReadRtpmap(p, s, Value);
    }
    if (NmosSpan_Equals(Name, "fmtp", false))
    {
        uint32_t PayloadType = 0;
        NmosSpan Parameters;
        if (!ReadPayloadType(Value, &PayloadType, &Parameters))
        {
            return FailLine(p, "a=fmtp needs a payload type", Value);
        }
        if (PayloadType == s->PayloadType)
        {
            s->Fmtp = Parameters;
            s->FmtpLine = p->Line;
        }
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(Name, "ptime", false))
    {
        s->Ptime = NmosSpan_Trim(Value);
        s->PtimeLine = p->Line;
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(Name, "mid", false))
    {
        s->Mid = NmosSpan_Trim(Value);
        return DTNMOS_OK;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadBandwidth -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult ReadBandwidth(NmosParser* p, NmosSpan Value)
{
    if (p->Count == 0)
    {
        return DTNMOS_OK;
    }
    NmosSpan Type;
    const NmosSpan Amount = NmosSpan_Split(Value, ':', &Type);
    if (NmosSpan_Equals(Type, "AS", false) &&
        !NmosText_ParseU64(NmosSpan_Trim(Amount), UINT64_MAX,
                           &p->Sections[p->Count - 1].BandwidthKbps))
    {
        return FailLine(p, "b=AS needs a bandwidth in kilobits per second", Value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult ReadLines(NmosParser* p)
{
    NmosSpan Line;
    while (NextLine(p, &Line))
    {
        if (Line.Length == 0)
        {
            continue;
        }
        if (Line.Length < 2 || Line.Data[1] != '=')
        {
            return FailLine(p, "a line of an SDP is <type>=<value>", Line);
        }
        const NmosSpan Value = {Line.Data + 2, Line.Length - 2};
        DtNmosResult Result = DTNMOS_OK;
        switch (Line.Data[0])
        {
        case 'o':
            Result = ReadOrigin(p, Value);
            break;
        case 's':
            p->Session->Name = NmosStore_Text(p->Store, Value.Data, Value.Length);
            if (p->Session->Name == NULL)
            {
                Result = NmosError_FailMemory();
            }
            break;
        case 'c':
            Result = ReadConnection(p, Value,
                                    p->Count == 0 ? &p->SessionLines
                                                  : &p->Sections[p->Count - 1].Shared);
            break;
        case 'm':
            Result = ReadMedia(p, Value);
            break;
        case 'a':
            Result = ReadAttribute(p, Value);
            break;
        case 'b':
            Result = ReadBandwidth(p, Value);
            break;
        default:
            break;
        }
        if (Result != DTNMOS_OK)
        {
            return Result;
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MediaOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the media of an encoding of a=rtpmap.
//
static DtNmosMedia MediaOf(NmosSpan Encoding)
{
    if (NmosSpan_Equals(Encoding, "raw", true))
    {
        return DTNMOS_MEDIA_VIDEO;
    }
    if (NmosSpan_Equals(Encoding, "L24", true) ||
        NmosSpan_Equals(Encoding, "L16", true) ||
        NmosSpan_Equals(Encoding, "AM824", true))
    {
        return DTNMOS_MEDIA_AUDIO;
    }
    if (NmosSpan_Equals(Encoding, "jxsv", true))
    {
        return DTNMOS_MEDIA_COMPRESSED_VIDEO;
    }
    if (NmosSpan_Equals(Encoding, "smpte291", true))
    {
        return DTNMOS_MEDIA_ANC;
    }
    return DTNMOS_MEDIA_OTHER;
}

// The parameters of an a=fmtp: pairs of a name and a value, separated by ';'.
typedef struct NmosFmtpReader
{
    NmosSpan Rest;
} NmosFmtpReader;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextParameter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the next parameter into name and value; value is empty for a flag such as
// interlace.
//
static bool NextParameter(NmosFmtpReader* Reader, NmosSpan* Name, NmosSpan* Value)
{
    while (Reader->Rest.Data != NULL)
    {
        NmosSpan Parameter;
        Reader->Rest = NmosSpan_Split(Reader->Rest, ';', &Parameter);
        Parameter = NmosSpan_Trim(Parameter);
        if (Parameter.Length == 0)
        {
            continue;
        }
        const NmosSpan After = NmosSpan_Split(Parameter, '=', Name);
        *Name = NmosSpan_Trim(*Name);
        if (After.Data == NULL)
        {
            Value->Data = Parameter.Data + Parameter.Length;
            Value->Length = 0;
        }
        else
        {
            *Value = NmosSpan_Trim(After);
        }
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FlagValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a flag such as interlace is set: present without a value, or with one other
// than 0.
//
static bool FlagValue(NmosSpan Value)
{
    return Value.Length == 0 || !NmosSpan_Equals(Value, "0", false);
}

// The raster, rate and colour that ST 2110-20 and -22 share, and the parameters of the
// format that dtnmos does not know.
typedef struct NmosRaster
{
    uint32_t* Width;
    uint32_t* Height;
    uint32_t* RateNumerator;
    uint32_t* RateDenominator;
    bool* Interlaced;
    bool* Segmented;
    uint32_t* Depth;
    DtNmosSampling* Sampling;
    DtNmosColorimetry* Colorimetry;
    DtNmosTcs* Tcs;
    DtNmosRange* Range;
    DtNmosTransmitterType* TransmitterType;
    char* Ssn; // of DTNMOS_MAX_VALUE_SIZE
    NmosBuffer* Others;
} NmosRaster;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies the value of the parameter name of a=fmtp into the array target of size bytes;
// fails when it does not fit.
//
static DtNmosResult CopyValue(size_t Line, NmosSpan Name, NmosSpan Value, char* Target,
                              size_t Size)
{
    if (!NmosText_CopySpan(Target, Size, Value))
    {
        return FailAt(Line, "a=fmtp has a value longer than its standard allows", Name);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- KeepParameter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Adds a parameter of a=fmtp to others as it was written: its name, and =value when it
// has a value, after "; " when others has one already.
//
static void KeepParameter(NmosBuffer* Others, NmosSpan Name, NmosSpan Value)
{
    if (Others->Length > 0)
    {
        DTNMOS_APPEND_LITERAL(Others, "; ");
    }
    NmosBuffer_Append(Others, Name.Data, Name.Length);
    if (Value.Length > 0)
    {
        DTNMOS_APPEND_LITERAL(Others, "=");
        NmosBuffer_Append(Others, Value.Data, Value.Length);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StoreOthers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets *Target to the parameters in others, owned by store, or leaves it null when there
// are none, and frees others; fails when the memory ran out.
//
static DtNmosResult StoreOthers(NmosBuffer* Others, NmosStore* Store, const char** Target)
{
    bool Stored = !Others->Failed;
    if (Stored && Others->Length > 0)
    {
        *Target = NmosStore_Text(Store, Others->Data, Others->Length);
        Stored = *Target != NULL;
    }
    NmosBuffer_Free(Others);
    return Stored ? DTNMOS_OK : NmosError_FailMemory();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValueText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies a value an enum reads into text of size bytes. A value longer than text is
// longer than any the enum knows, and becomes "?", which none spells, so that the enum
// reads it as _OTHER.
//
static void ValueText(NmosSpan Value, char* Text, size_t Size)
{
    if (!NmosText_CopySpan(Text, Size, Value))
    {
        snprintf(Text, Size, "?");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadRaster -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the parameter name of the raster; sets handled when it is one of them. A value
// an enum does not know makes it _OTHER and is kept in the others of the raster.
//
static DtNmosResult ReadRaster(size_t Line, const NmosRaster* r, NmosSpan Name,
                               NmosSpan Value, bool* Handled)
{
    *Handled = true;
    bool Valid = true;
    bool Other = false;
    char Text[DTNMOS_MAX_VALUE_SIZE];
    ValueText(Value, Text, sizeof(Text));
    if (NmosSpan_Equals(Name, "width", true))
    {
        Valid = NmosText_ParseU32(Value, 65535, r->Width);
    }
    else if (NmosSpan_Equals(Name, "height", true))
    {
        Valid = NmosText_ParseU32(Value, 65535, r->Height);
    }
    else if (NmosSpan_Equals(Name, "exactframerate", true))
    {
        Valid = NmosText_ParseRate(Value, r->RateNumerator, r->RateDenominator);
    }
    else if (NmosSpan_Equals(Name, "depth", true))
    {
        Valid = NmosText_ParseU32(Value, 64, r->Depth);
    }
    else if (NmosSpan_Equals(Name, "interlace", true))
    {
        *r->Interlaced = FlagValue(Value);
    }
    else if (NmosSpan_Equals(Name, "segmented", true))
    {
        *r->Segmented = FlagValue(Value);
    }
    else if (NmosSpan_Equals(Name, "sampling", true))
    {
        *r->Sampling = DtNmosSampling_FromText(Text);
        Other = *r->Sampling == DTNMOS_SAMPLING_OTHER;
    }
    else if (NmosSpan_Equals(Name, "colorimetry", true))
    {
        *r->Colorimetry = DtNmosColorimetry_FromText(Text);
        Other = *r->Colorimetry == DTNMOS_COLORIMETRY_OTHER;
    }
    else if (NmosSpan_Equals(Name, "TCS", true))
    {
        *r->Tcs = DtNmosTcs_FromText(Text);
        Other = *r->Tcs == DTNMOS_TCS_OTHER;
    }
    else if (NmosSpan_Equals(Name, "RANGE", true))
    {
        *r->Range = DtNmosRange_FromText(Text);
        Other = *r->Range == DTNMOS_RANGE_OTHER;
    }
    else if (NmosSpan_Equals(Name, "TP", true))
    {
        *r->TransmitterType = DtNmosTransmitterType_FromText(Text);
        Other = *r->TransmitterType == DTNMOS_TRANSMITTER_TYPE_OTHER;
    }
    else if (NmosSpan_Equals(Name, "SSN", true))
    {
        return CopyValue(Line, Name, Value, r->Ssn, DTNMOS_MAX_VALUE_SIZE);
    }
    else
    {
        *Handled = false;
    }
    if (!Valid)
    {
        return FailAt(Line, "a=fmtp has a parameter whose value is no number or rate",
                      Name);
    }
    if (Other)
    {
        KeepParameter(r->Others, Name, Value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult BuildVideo(const NmosSection* s, NmosStore* Store,
                               DtNmosVideoFormat* Video)
{
    NmosBuffer Others;
    memset(&Others, 0, sizeof(Others));
    const NmosRaster r = {&Video->Width,
                          &Video->Height,
                          &Video->RateNumerator,
                          &Video->RateDenominator,
                          &Video->Interlaced,
                          &Video->Segmented,
                          &Video->Depth,
                          &Video->Sampling,
                          &Video->Colorimetry,
                          &Video->Tcs,
                          &Video->Range,
                          &Video->TransmitterType,
                          Video->Ssn,
                          &Others};
    NmosFmtpReader Reader = {s->Fmtp};
    NmosSpan Name;
    NmosSpan Value;
    DtNmosResult Result = DTNMOS_OK;
    while (Result == DTNMOS_OK && NextParameter(&Reader, &Name, &Value))
    {
        bool Handled = false;
        Result = ReadRaster(s->FmtpLine, &r, Name, Value, &Handled);
        if (Result != DTNMOS_OK || Handled)
        {
            continue;
        }
        if (NmosSpan_Equals(Name, "PM", true))
        {
            char Text[DTNMOS_MAX_VALUE_SIZE];
            ValueText(Value, Text, sizeof(Text));
            Video->PackingMode = DtNmosPackingMode_FromText(Text);
            if (Video->PackingMode == DTNMOS_PACKING_MODE_OTHER)
            {
                KeepParameter(&Others, Name, Value);
            }
        }
        else
        {
            KeepParameter(&Others, Name, Value);
        }
    }
    if (Result != DTNMOS_OK)
    {
        NmosBuffer_Free(&Others);
        return Result;
    }
    return StoreOthers(&Others, Store, &Video->OtherParameters);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildCompressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult BuildCompressed(const NmosSection* s, NmosStore* Store,
                                    DtNmosCompressedVideoFormat* Video)
{
    if (!NmosText_CopySpan(Video->Encoding, sizeof(Video->Encoding), s->Encoding))
    {
        return FailAt(s->Line, "a=rtpmap has an encoding longer than RFC 6838 allows",
                      s->Encoding);
    }
    Video->TransmissionMode = 1;
    Video->BandwidthKbps = s->BandwidthKbps;
    NmosBuffer Others;
    memset(&Others, 0, sizeof(Others));
    const NmosRaster r = {&Video->Width,
                          &Video->Height,
                          &Video->RateNumerator,
                          &Video->RateDenominator,
                          &Video->Interlaced,
                          &Video->Segmented,
                          &Video->Depth,
                          &Video->Sampling,
                          &Video->Colorimetry,
                          &Video->Tcs,
                          &Video->Range,
                          &Video->TransmitterType,
                          Video->Ssn,
                          &Others};
    NmosFmtpReader Reader = {s->Fmtp};
    NmosSpan Name;
    NmosSpan Value;
    DtNmosResult Result = DTNMOS_OK;
    while (Result == DTNMOS_OK && NextParameter(&Reader, &Name, &Value))
    {
        bool Handled = false;
        Result = ReadRaster(s->FmtpLine, &r, Name, Value, &Handled);
        if (Result != DTNMOS_OK || Handled)
        {
            continue;
        }
        char* Text = NULL;
        uint32_t* Number = NULL;
        if (NmosSpan_Equals(Name, "profile", true))
        {
            Text = Video->Profile;
        }
        else if (NmosSpan_Equals(Name, "level", true))
        {
            Text = Video->Level;
        }
        else if (NmosSpan_Equals(Name, "sublevel", true))
        {
            Text = Video->Sublevel;
        }
        else if (NmosSpan_Equals(Name, "packetmode", true))
        {
            Number = &Video->PacketMode;
        }
        else if (NmosSpan_Equals(Name, "transmode", true))
        {
            Number = &Video->TransmissionMode;
        }
        else
        {
            KeepParameter(&Others, Name, Value);
        }
        if (Text != NULL)
        {
            Result = CopyValue(s->FmtpLine, Name, Value, Text, DTNMOS_MAX_VALUE_SIZE);
        }
        if (Number != NULL && !NmosText_ParseU32(Value, 255, Number))
        {
            Result = FailAt(s->FmtpLine,
                            "a=fmtp has a parameter whose value is no number", Name);
        }
    }
    if (Result != DTNMOS_OK)
    {
        NmosBuffer_Free(&Others);
        return Result;
    }
    return StoreOthers(&Others, Store, &Video->OtherParameters);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult BuildAudio(const NmosSection* s, NmosStore* Store,
                               DtNmosAudioFormat* Audio)
{
    // One of the three, as MediaOf() made the flow audio, and as there without case.
    Audio->Encoding =
        NmosSpan_Equals(s->Encoding, "L16", true)   ? DTNMOS_AUDIO_ENCODING_L16
        : NmosSpan_Equals(s->Encoding, "L24", true) ? DTNMOS_AUDIO_ENCODING_L24
                                                    : DTNMOS_AUDIO_ENCODING_AM824;
    Audio->SampleRate = s->ClockRate;
    // RFC 4566 leaves one channel when a=rtpmap names none.
    Audio->Channels = s->Channels == 0 ? 1 : s->Channels;
    if (s->Ptime.Length > 0 &&
        !NmosText_ParseMilliseconds(s->Ptime, &Audio->PacketTimeNs))
    {
        return FailAt(s->PtimeLine, "a=ptime needs milliseconds, such as 1 or 0.125",
                      s->Ptime);
    }
    NmosBuffer Others;
    memset(&Others, 0, sizeof(Others));
    NmosFmtpReader Reader = {s->Fmtp};
    NmosSpan Name;
    NmosSpan Value;
    while (NextParameter(&Reader, &Name, &Value))
    {
        if (!NmosSpan_Equals(Name, "channel-order", true))
        {
            KeepParameter(&Others, Name, Value);
            continue;
        }
        Audio->ChannelOrder = NmosStore_Text(Store, Value.Data, Value.Length);
        if (Audio->ChannelOrder == NULL)
        {
            NmosBuffer_Free(&Others);
            return NmosError_FailMemory();
        }
    }
    return StoreOthers(&Others, Store, &Audio->OtherParameters);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadDidSdid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads DID_SDID={0x61,0x02} into pair.
//
static bool ReadDidSdid(NmosSpan Value, DtNmosDidSdid* Pair)
{
    if (Value.Length < 2 || Value.Data[0] != '{' || Value.Data[Value.Length - 1] != '}')
    {
        return false;
    }
    const NmosSpan Inside = {Value.Data + 1, Value.Length - 2};
    NmosSpan Did;
    const NmosSpan Sdid = NmosSpan_Split(Inside, ',', &Did);
    return Sdid.Data != NULL && NmosText_ParseByte(NmosSpan_Trim(Did), &Pair->Did) &&
           NmosText_ParseByte(NmosSpan_Trim(Sdid), &Pair->Sdid);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildAnc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult BuildAnc(const NmosSection* s, NmosStore* Store, DtNmosFlow* Flow)
{
    DtNmosAncFormat* Anc = &Flow->Format.Anc;
    DtNmosDidSdid Pairs[64];
    size_t Count = 0;
    NmosFmtpReader Reader = {s->Fmtp};
    NmosSpan Name;
    NmosSpan Value;
    while (NextParameter(&Reader, &Name, &Value))
    {
        bool Valid = true;
        char* Text = NULL;
        size_t Size = 0;
        if (NmosSpan_Equals(Name, "DID_SDID", true))
        {
            if (Count == sizeof(Pairs) / sizeof(Pairs[0]))
            {
                return FailAt(s->FmtpLine, "a=fmtp has more DID_SDID than 64", Name);
            }
            Valid = ReadDidSdid(Value, &Pairs[Count++]);
        }
        else if (NmosSpan_Equals(Name, "VPID_Code", true))
        {
            Valid = NmosText_ParseU32(Value, 255, &Anc->VpidCode);
        }
        else if (NmosSpan_Equals(Name, "exactframerate", true))
        {
            Valid = NmosText_ParseRate(Value, &Anc->RateNumerator, &Anc->RateDenominator);
        }
        else if (NmosSpan_Equals(Name, "TM", true))
        {
            Text = Anc->TransmissionModel;
            Size = sizeof(Anc->TransmissionModel);
        }
        else if (NmosSpan_Equals(Name, "SSN", true))
        {
            Text = Anc->Ssn;
            Size = sizeof(Anc->Ssn);
        }
        if (!Valid)
        {
            return FailAt(s->FmtpLine, "a=fmtp has a parameter whose value is not valid",
                          Value);
        }
        if (Text != NULL)
        {
            const DtNmosResult Result = CopyValue(s->FmtpLine, Name, Value, Text, Size);
            if (Result != DTNMOS_OK)
            {
                return Result;
            }
        }
    }
    if (Count > 0)
    {
        Anc->DidSdid = NmosStore_Copy(Store, Pairs, Count * sizeof(Pairs[0]));
        if (Anc->DidSdid == NULL)
        {
            return NmosError_FailMemory();
        }
        Anc->DidSdidCount = Count;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSecondLeg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the mid of s is the second of a group of DUP.
//
static bool IsSecondLeg(const NmosParser* p, const NmosSection* s)
{
    if (s->Mid.Length == 0)
    {
        return false;
    }
    for (size_t i = 0; i < p->GroupCount; ++i)
    {
        const NmosSpan Second = p->GroupSecond[i];
        if (Second.Length == s->Mid.Length &&
            memcmp(Second.Data, s->Mid.Data, Second.Length) == 0)
        {
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadMediaClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a=mediaclk:direct=<offset> into the flow.
//
static DtNmosResult ReadMediaClock(const NmosSection* s, NmosSpan Mediaclk,
                                   DtNmosFlow* Flow)
{
    if (!NmosSpan_StartsWith(Mediaclk, "direct="))
    {
        return DTNMOS_OK;
    }
    NmosSpan Offset = {Mediaclk.Data + 7, Mediaclk.Length - 7};
    NmosSpan Number;
    NextWord(Offset, &Number);
    if (!NmosText_ParseU32(Number, UINT32_MAX, &Flow->MediaClockOffset))
    {
        return FailAt(s->Line, "a=mediaclk:direct= needs an offset", Mediaclk);
    }
    Flow->MediaClockDirect = true;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPtpDomain -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the domain after the grandmaster of ptp=, ":127" as ST 2110-10 §8.2 writes it or
// ":domain-nmbr=127" as RFC 7273 §4.8 does, into *Domain; returns false for anything
// else, such as a domain-name, which the clock then keeps as text.
//
static bool ReadPtpDomain(NmosSpan Text, int* Domain)
{
    if (NmosSpan_StartsWith(Text, "domain-nmbr="))
    {
        Text.Data += 12;
        Text.Length -= 12;
    }
    uint32_t Number = 0;
    if (!NmosText_ParseU32(Text, 127, &Number))
    {
        return false;
    }
    *Domain = (int)Number;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the value of ptp=, <version>:<grandmaster>[:<domain>] or <version>:traceable,
// into clock; returns false when it is neither.
//
static bool ReadPtp(NmosSpan Text, DtNmosRefClock* Clock)
{
    NmosSpan Version;
    const NmosSpan Rest = NmosSpan_Split(Text, ':', &Version);
    NmosSpan Grandmaster;
    const NmosSpan Domain = NmosSpan_Split(Rest, ':', &Grandmaster);
    if (Rest.Data == NULL || Version.Length == 0 || Grandmaster.Length == 0 ||
        !NmosText_CopySpan(Clock->PtpVersion, sizeof(Clock->PtpVersion), Version))
    {
        return false;
    }
    if (NmosSpan_Equals(Grandmaster, "traceable", false))
    {
        Clock->Traceable = true;
        return Domain.Data == NULL;
    }
    return NmosText_CopySpan(Clock->Grandmaster, sizeof(Clock->Grandmaster),
                             Grandmaster) &&
           (Domain.Data == NULL || ReadPtpDomain(Domain, &Clock->Domain));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadRefClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the value of a=ts-refclk into clock: a PTP clock or the MAC of ST 2110-10 §8.2
// into its fields, any other kind as its text, owned by store.
//
static DtNmosResult ReadRefClock(NmosSpan Text, NmosStore* Store, DtNmosRefClock* Clock)
{
    memset(Clock, 0, sizeof(*Clock));
    Clock->Domain = -1;
    if (Text.Length == 0)
    {
        return DTNMOS_OK;
    }
    if (NmosSpan_StartsWith(Text, "ptp="))
    {
        const NmosSpan Value = {Text.Data + 4, Text.Length - 4};
        if (ReadPtp(Value, Clock))
        {
            Clock->Kind = DTNMOS_REFCLOCK_PTP;
            return DTNMOS_OK;
        }
    }
    else if (NmosSpan_StartsWith(Text, "localmac="))
    {
        const NmosSpan Mac = {Text.Data + 9, Text.Length - 9};
        if (Mac.Length > 0 &&
            NmosText_CopySpan(Clock->LocalMac, sizeof(Clock->LocalMac), Mac))
        {
            Clock->Kind = DTNMOS_REFCLOCK_LOCALMAC;
            return DTNMOS_OK;
        }
    }
    memset(Clock, 0, sizeof(*Clock));
    Clock->Domain = -1;
    Clock->Kind = DTNMOS_REFCLOCK_OTHER;
    Clock->Text = NmosStore_Text(Store, Text.Data, Text.Length);
    return Clock->Text == NULL ? NmosError_FailMemory() : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult BuildFlow(NmosParser* p, const NmosSection* s, DtNmosFlow* Flow)
{
    Flow->Size = sizeof(*Flow);
    Flow->Media = MediaOf(s->Encoding);
    Flow->DestinationPort = s->Port;
    Flow->PayloadType = s->PayloadType;
    Flow->ClockRate = s->ClockRate;
    Flow->Leg = IsSecondLeg(p, s) ? 1u : 0u;
    const NmosSharedLines* Own = &s->Shared;
    const NmosSharedLines* Session = &p->SessionLines;
    const NmosSpan Connection =
        Own->Connection.Length > 0 ? Own->Connection : Session->Connection;
    if (Connection.Length == 0)
    {
        return FailAt(s->Line, "the media section has no c= and neither has the session",
                      (NmosSpan){"m=", 2});
    }
    const NmosSpan Source =
        Own->FilterSource.Length > 0 ? Own->FilterSource : Session->FilterSource;
    const NmosSpan Refclk = Own->TsRefclk.Length > 0 ? Own->TsRefclk : Session->TsRefclk;
    const NmosSpan Mediaclk =
        Own->Mediaclk.Length > 0 ? Own->Mediaclk : Session->Mediaclk;
    if (!NmosText_CopySpan(Flow->DestinationIp, sizeof(Flow->DestinationIp),
                           Connection) ||
        !NmosText_CopySpan(Flow->SourceIp, sizeof(Flow->SourceIp), Source))
    {
        return FailAt(s->Line, "an address is longer than a domain name may be",
                      Connection);
    }
    DtNmosResult Result = ReadRefClock(Refclk, p->Store, &Flow->RefClock);
    if (Result == DTNMOS_OK)
    {
        Result = ReadMediaClock(s, Mediaclk, Flow);
    }
    if (Result != DTNMOS_OK)
    {
        return Result;
    }
    switch (Flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        return BuildVideo(s, p->Store, &Flow->Format.Video);
    case DTNMOS_MEDIA_AUDIO:
        return BuildAudio(s, p->Store, &Flow->Format.Audio);
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return BuildCompressed(s, p->Store, &Flow->Format.CompressedVideo);
    case DTNMOS_MEDIA_ANC:
        return BuildAnc(s, p->Store, Flow);
    case DTNMOS_MEDIA_OTHER:
    {
        DtNmosOtherFormat* Other = &Flow->Format.Other;
        const NmosSpan Fmtp = NmosSpan_Trim(s->Fmtp);
        if (!NmosText_CopySpan(Other->Encoding, sizeof(Other->Encoding), s->Encoding))
        {
            return FailAt(s->Line, "a=rtpmap has an encoding longer than RFC 6838 allows",
                          s->Encoding);
        }
        if (Fmtp.Length > 0)
        {
            Other->Fmtp = NmosStore_Text(p->Store, Fmtp.Data, Fmtp.Length);
            if (Other->Fmtp == NULL)
            {
                return NmosError_FailMemory();
            }
        }
        return DTNMOS_OK;
    }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSdp_Parse(const char* Text, size_t Length, DtNmosSdp** Sdp)
{
    if (Sdp == NULL || (Text == NULL && Length > 0))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosSdp_Parse() needs a text and a place for the SDP.");
    }
    *Sdp = NULL;
    DtNmosSdp* Result = calloc(1, sizeof(*Result));
    if (Result == NULL)
    {
        return NmosError_FailMemory();
    }
    Result->Session.Size = sizeof(Result->Session);
    NmosParser p;
    memset(&p, 0, sizeof(p));
    p.Text = Text;
    p.Length = Length;
    p.Session = &Result->Session;
    p.Store = &Result->Store;
    DtNmosResult Status = ReadLines(&p);
    if (Status == DTNMOS_OK && p.Count == 0)
    {
        Status =
            NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The SDP has no media section.");
    }
    if (Status == DTNMOS_OK)
    {
        Result->Flows = calloc(p.Count, sizeof(*Result->Flows));
        if (Result->Flows == NULL)
        {
            Status = NmosError_FailMemory();
        }
    }
    for (size_t i = 0; Status == DTNMOS_OK && i < p.Count; ++i)
    {
        Status = BuildFlow(&p, &p.Sections[i], &Result->Flows[i]);
        Result->Count = i + 1;
    }
    free(p.Sections);
    if (Status != DTNMOS_OK)
    {
        DtNmosSdp_Free(Result);
        return Status;
    }
    *Sdp = Result;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Session -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosSession* DtNmosSdp_Session(const DtNmosSdp* Sdp)
{
    return Sdp == NULL ? NULL : &Sdp->Session;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_FlowCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtNmosSdp_FlowCount(const DtNmosSdp* Sdp)
{
    return Sdp == NULL ? 0 : Sdp->Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtNmosFlow* DtNmosSdp_Flow(const DtNmosSdp* Sdp, size_t Index)
{
    return Sdp == NULL || Index >= Sdp->Count ? NULL : &Sdp->Flows[Index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosSdp_Free(DtNmosSdp* Sdp)
{
    if (Sdp == NULL)
    {
        return;
    }
    free(Sdp->Flows);
    NmosStore_Free(&Sdp->Store);
    free(Sdp);
}
