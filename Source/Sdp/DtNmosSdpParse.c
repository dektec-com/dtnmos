// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosSdpParse.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Reading an SDP (RFC 8866) as SMPTE ST 2110 writes it
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosFlow.h"

struct DtNmosSdp
{
    DtNmosSession session;
    DtNmosFlow* flows;
    size_t count;
    NmosStore store; // the strings and arrays of the session and the flows
};

// The attributes that the session level and a media section both may carry.
typedef struct NmosSharedLines
{
    NmosSpan connection; // the address of c=, without TTL
    size_t connection_line;
    NmosSpan filter_source; // the first source of a=source-filter: incl
    NmosSpan ts_refclk;
    NmosSpan mediaclk; // the value of a=mediaclk
} NmosSharedLines;

// A media section as its lines give it.
typedef struct NmosSection
{
    size_t line; // of its m=
    uint16_t port;
    uint8_t payload_type;
    NmosSharedLines shared;
    NmosSpan encoding; // of the a=rtpmap of payload_type
    uint32_t clock_rate;
    uint32_t channels; // the encoding parameters of a=rtpmap; 0 when absent
    NmosSpan fmtp;
    size_t fmtp_line;
    NmosSpan ptime;
    size_t ptime_line;
    NmosSpan mid;
    uint64_t bandwidth_kbps;
} NmosSection;

typedef struct NmosParser
{
    const char* text;
    size_t length;
    size_t position;
    size_t line; // number of the current line, from 1
    DtNmosSession* session;
    NmosStore* store; // of the DtNmosSdp being read
    NmosSharedLines session_lines;
    NmosSpan group_second[16]; // the second mid of each a=group:DUP
    size_t group_count;
    NmosSection* sections;
    size_t count;
    size_t capacity;
} NmosParser;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fail_line -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult fail_line(NmosParser* p, const char* what, NmosSpan text)
{
    return NmosError_Fail(DTNMOS_E_PARSE, "SDP line %zu: %s: '%.*s'", p->line, what,
                          (int)(text.length > 120 ? 120 : text.length), text.data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fail_at -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult fail_at(size_t line, const char* what, NmosSpan text)
{
    return NmosError_Fail(DTNMOS_E_PARSE, "SDP line %zu: %s: '%.*s'", line, what,
                          (int)(text.length > 120 ? 120 : text.length), text.data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_line -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the next line without its end of line, or 0 at the end of the text.
//
static int next_line(NmosParser* p, NmosSpan* line)
{
    if (p->position >= p->length)
    {
        return 0;
    }
    const char* start = p->text + p->position;
    const char* end = memchr(start, '\n', p->length - p->position);
    size_t length = end == NULL ? p->length - p->position : (size_t)(end - start);
    p->position += length + (end == NULL ? 0 : 1);
    if (length > 0 && start[length - 1] == '\r')
    {
        --length;
    }
    line->data = start;
    line->length = length;
    ++p->line;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_word -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Splits span at its first space: word is what lies before it, and the rest follows,
// trimmed.
//
static NmosSpan next_word(NmosSpan span, NmosSpan* word)
{
    span = NmosSpan_Trim(span);
    const NmosSpan rest = NmosSpan_Split(span, ' ', word);
    return NmosSpan_Trim(rest);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_connection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult read_connection(NmosParser* p, NmosSpan value, NmosSharedLines* lines)
{
    NmosSpan network;
    NmosSpan type;
    NmosSpan address;
    NmosSpan rest = next_word(value, &network);
    rest = next_word(rest, &type);
    next_word(rest, &address);
    if (!NmosSpan_Equals(network, "IN", 0) ||
        (!NmosSpan_Equals(type, "IP4", 0) && !NmosSpan_Equals(type, "IP6", 0)) ||
        address.length == 0)
    {
        return fail_line(p, "c= needs IN IP4 or IN IP6 and an address", value);
    }
    // A multicast address of IPv4 carries its TTL, and either may carry a count, after
    // '/'.
    NmosSpan host;
    NmosSpan_Split(address, '/', &host);
    lines->connection = host;
    lines->connection_line = p->line;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_origin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult read_origin(NmosParser* p, NmosSpan value)
{
    NmosSpan user;
    NmosSpan id;
    NmosSpan version;
    NmosSpan network;
    NmosSpan type;
    NmosSpan address;
    NmosSpan rest = next_word(value, &user);
    rest = next_word(rest, &id);
    rest = next_word(rest, &version);
    rest = next_word(rest, &network);
    rest = next_word(rest, &type);
    next_word(rest, &address);
    if (!NmosText_ParseU64(id, UINT64_MAX, &p->session->SessionId) ||
        !NmosText_ParseU64(version, UINT64_MAX, &p->session->SessionVersion) ||
        address.length == 0)
    {
        return fail_line(p, "o= needs a user, a session ID, a version and an address",
                         value);
    }
    if (!NmosText_CopySpan(p->session->OriginIp, sizeof(p->session->OriginIp), address))
    {
        return fail_line(p, "o= has an address longer than a domain name may be", value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_media -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_media(NmosParser* p, NmosSpan value)
{
    if (p->count == p->capacity)
    {
        const size_t capacity = p->capacity == 0 ? 4 : p->capacity * 2;
        NmosSection* sections = realloc(p->sections, capacity * sizeof(*sections));
        if (sections == NULL)
        {
            return NmosError_FailMemory();
        }
        p->sections = sections;
        p->capacity = capacity;
    }
    NmosSection* s = &p->sections[p->count];
    memset(s, 0, sizeof(*s));
    s->line = p->line;
    NmosSpan media;
    NmosSpan port;
    NmosSpan protocol;
    NmosSpan format;
    NmosSpan rest = next_word(value, &media);
    rest = next_word(rest, &port);
    rest = next_word(rest, &protocol);
    next_word(rest, &format);
    NmosSpan port_number;
    NmosSpan_Split(port, '/', &port_number);
    uint32_t number = 0;
    uint32_t payload_type = 0;
    if (media.length == 0 || !NmosText_ParseU32(port_number, 65535, &number) ||
        protocol.length == 0 || !NmosText_ParseU32(format, 127, &payload_type))
    {
        return fail_line(p, "m= needs a media, a port, a protocol and a payload type",
                         value);
    }
    s->port = (uint16_t)number;
    s->payload_type = (uint8_t)payload_type;
    ++p->count;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_payload_type -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the payload type that starts value, as a=rtpmap and a=fmtp begin, into
// payload_type, and returns what follows it.
//
static int read_payload_type(NmosSpan value, uint32_t* payload_type, NmosSpan* rest)
{
    NmosSpan number;
    *rest = next_word(value, &number);
    return NmosText_ParseU32(number, 127, payload_type);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_rtpmap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult read_rtpmap(NmosParser* p, NmosSection* s, NmosSpan value)
{
    uint32_t payload_type = 0;
    NmosSpan map;
    if (!read_payload_type(value, &payload_type, &map))
    {
        return fail_line(p, "a=rtpmap needs a payload type", value);
    }
    if (payload_type != s->payload_type)
    {
        return DTNMOS_OK;
    }
    NmosSpan encoding;
    NmosSpan clock;
    NmosSpan rest = NmosSpan_Split(map, '/', &encoding);
    const NmosSpan parameters = NmosSpan_Split(rest, '/', &clock);
    s->encoding = NmosSpan_Trim(encoding);
    if (s->encoding.length == 0 ||
        !NmosText_ParseU32(clock, UINT32_MAX, &s->clock_rate) ||
        (parameters.data != NULL && !NmosText_ParseU32(parameters, 65535, &s->channels)))
    {
        return fail_line(
            p, "a=rtpmap needs an encoding, a clock rate and optional channels", value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_source_filter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_source_filter(NmosParser* p, NmosSpan value,
                                       NmosSharedLines* lines)
{
    // a=source-filter: incl IN IP4 <destination> <source> ...
    NmosSpan mode;
    NmosSpan network;
    NmosSpan type;
    NmosSpan destination;
    NmosSpan source;
    NmosSpan rest = next_word(value, &mode);
    rest = next_word(rest, &network);
    rest = next_word(rest, &type);
    rest = next_word(rest, &destination);
    next_word(rest, &source);
    if (mode.length == 0 || network.length == 0 || type.length == 0 ||
        destination.length == 0 || source.length == 0)
    {
        return fail_line(
            p,
            "a=source-filter needs a mode, IN, an address type, a destination "
            "and a source",
            value);
    }
    if (NmosSpan_Equals(mode, "incl", 1) && lines->filter_source.length == 0)
    {
        lines->filter_source = source;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_attribute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads an attribute into the lines of the session or of the current section.
//
static DtNmosResult read_attribute(NmosParser* p, NmosSpan line)
{
    NmosSpan name;
    const NmosSpan value = NmosSpan_Split(line, ':', &name);
    NmosSection* s = p->count == 0 ? NULL : &p->sections[p->count - 1];
    NmosSharedLines* lines = s == NULL ? &p->session_lines : &s->shared;
    if (NmosSpan_Equals(name, "source-filter", 0))
    {
        return read_source_filter(p, value, lines);
    }
    if (NmosSpan_Equals(name, "ts-refclk", 0))
    {
        if (lines->ts_refclk.length == 0)
        {
            lines->ts_refclk = NmosSpan_Trim(value);
        }
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(name, "mediaclk", 0))
    {
        lines->mediaclk = NmosSpan_Trim(value);
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(name, "group", 0))
    {
        NmosSpan semantics;
        NmosSpan rest = next_word(value, &semantics);
        NmosSpan first;
        NmosSpan second;
        rest = next_word(rest, &first);
        next_word(rest, &second);
        if (NmosSpan_Equals(semantics, "DUP", 0) && second.length > 0 &&
            p->group_count < sizeof(p->group_second) / sizeof(p->group_second[0]))
        {
            p->group_second[p->group_count++] = second;
        }
        return DTNMOS_OK;
    }
    if (s == NULL)
    {
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(name, "rtpmap", 0))
    {
        return read_rtpmap(p, s, value);
    }
    if (NmosSpan_Equals(name, "fmtp", 0))
    {
        uint32_t payload_type = 0;
        NmosSpan parameters;
        if (!read_payload_type(value, &payload_type, &parameters))
        {
            return fail_line(p, "a=fmtp needs a payload type", value);
        }
        if (payload_type == s->payload_type)
        {
            s->fmtp = parameters;
            s->fmtp_line = p->line;
        }
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(name, "ptime", 0))
    {
        s->ptime = NmosSpan_Trim(value);
        s->ptime_line = p->line;
        return DTNMOS_OK;
    }
    if (NmosSpan_Equals(name, "mid", 0))
    {
        s->mid = NmosSpan_Trim(value);
        return DTNMOS_OK;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_bandwidth -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_bandwidth(NmosParser* p, NmosSpan value)
{
    if (p->count == 0)
    {
        return DTNMOS_OK;
    }
    NmosSpan type;
    const NmosSpan amount = NmosSpan_Split(value, ':', &type);
    if (NmosSpan_Equals(type, "AS", 0) &&
        !NmosText_ParseU64(NmosSpan_Trim(amount), UINT64_MAX,
                           &p->sections[p->count - 1].bandwidth_kbps))
    {
        return fail_line(p, "b=AS needs a bandwidth in kilobits per second", value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_lines(NmosParser* p)
{
    NmosSpan line;
    while (next_line(p, &line))
    {
        if (line.length == 0)
        {
            continue;
        }
        if (line.length < 2 || line.data[1] != '=')
        {
            return fail_line(p, "a line of an SDP is <type>=<value>", line);
        }
        const NmosSpan value = {line.data + 2, line.length - 2};
        DtNmosResult result = DTNMOS_OK;
        switch (line.data[0])
        {
        case 'o':
            result = read_origin(p, value);
            break;
        case 's':
            p->session->Name = NmosStore_Text(p->store, value.data, value.length);
            if (p->session->Name == NULL)
            {
                result = NmosError_FailMemory();
            }
            break;
        case 'c':
            result = read_connection(p, value,
                                     p->count == 0 ? &p->session_lines
                                                   : &p->sections[p->count - 1].shared);
            break;
        case 'm':
            result = read_media(p, value);
            break;
        case 'a':
            result = read_attribute(p, value);
            break;
        case 'b':
            result = read_bandwidth(p, value);
            break;
        default:
            break;
        }
        if (result != DTNMOS_OK)
        {
            return result;
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- media_of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the media of an encoding of a=rtpmap.
//
static DtNmosMedia media_of(NmosSpan encoding)
{
    if (NmosSpan_Equals(encoding, "raw", 1))
    {
        return DTNMOS_MEDIA_VIDEO;
    }
    if (NmosSpan_Equals(encoding, "L24", 1) || NmosSpan_Equals(encoding, "L16", 1) ||
        NmosSpan_Equals(encoding, "AM824", 1))
    {
        return DTNMOS_MEDIA_AUDIO;
    }
    if (NmosSpan_Equals(encoding, "jxsv", 1))
    {
        return DTNMOS_MEDIA_COMPRESSED_VIDEO;
    }
    if (NmosSpan_Equals(encoding, "smpte291", 1))
    {
        return DTNMOS_MEDIA_ANC;
    }
    return DTNMOS_MEDIA_OTHER;
}

// The parameters of an a=fmtp: pairs of a name and a value, separated by ';'.
typedef struct NmosFmtpReader
{
    NmosSpan rest;
} NmosFmtpReader;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_parameter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the next parameter into name and value; value is empty for a flag such as
// interlace.
//
static int next_parameter(NmosFmtpReader* reader, NmosSpan* name, NmosSpan* value)
{
    while (reader->rest.data != NULL)
    {
        NmosSpan parameter;
        reader->rest = NmosSpan_Split(reader->rest, ';', &parameter);
        parameter = NmosSpan_Trim(parameter);
        if (parameter.length == 0)
        {
            continue;
        }
        const NmosSpan after = NmosSpan_Split(parameter, '=', name);
        *name = NmosSpan_Trim(*name);
        if (after.data == NULL)
        {
            value->data = parameter.data + parameter.length;
            value->length = 0;
        }
        else
        {
            *value = NmosSpan_Trim(after);
        }
        return 1;
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- flag_value -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether a flag such as interlace is set: present without a value, or with one other
// than 0.
//
static int flag_value(NmosSpan value)
{
    return value.length == 0 || !NmosSpan_Equals(value, "0", 0);
}

// The raster, rate and colour that ST 2110-20 and -22 share.
typedef struct NmosRaster
{
    uint32_t* width;
    uint32_t* height;
    uint32_t* rate_numerator;
    uint32_t* rate_denominator;
    int* interlaced;
    int* segmented;
    uint32_t* depth;
    char* sampling;         // of DTNMOS_MAX_VALUE_SIZE
    char* colorimetry;      // of DTNMOS_MAX_VALUE_SIZE
    char* tcs;              // of DTNMOS_MAX_VALUE_SIZE
    char* range;            // of DTNMOS_MAX_VALUE_SIZE
    char* ssn;              // of DTNMOS_MAX_VALUE_SIZE
    char* transmitter_type; // of DTNMOS_MAX_SHORT_SIZE
} NmosRaster;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_value -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies the value of the parameter name of a=fmtp into the array target of size bytes;
// fails when it does not fit.
//
static DtNmosResult copy_value(size_t line, NmosSpan name, NmosSpan value, char* target,
                               size_t size)
{
    if (!NmosText_CopySpan(target, size, value))
    {
        return fail_at(line, "a=fmtp has a value longer than its standard allows", name);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_raster -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the parameter name of the raster; sets handled when it is one of them.
//
static DtNmosResult read_raster(size_t line, const NmosRaster* r, NmosSpan name,
                                NmosSpan value, int* handled)
{
    *handled = 1;
    int valid = 1;
    char* text = NULL;
    size_t size = DTNMOS_MAX_VALUE_SIZE;
    if (NmosSpan_Equals(name, "width", 1))
    {
        valid = NmosText_ParseU32(value, 65535, r->width);
    }
    else if (NmosSpan_Equals(name, "height", 1))
    {
        valid = NmosText_ParseU32(value, 65535, r->height);
    }
    else if (NmosSpan_Equals(name, "exactframerate", 1))
    {
        valid = NmosText_ParseRate(value, r->rate_numerator, r->rate_denominator);
    }
    else if (NmosSpan_Equals(name, "depth", 1))
    {
        valid = NmosText_ParseU32(value, 64, r->depth);
    }
    else if (NmosSpan_Equals(name, "interlace", 1))
    {
        *r->interlaced = flag_value(value);
    }
    else if (NmosSpan_Equals(name, "segmented", 1))
    {
        *r->segmented = flag_value(value);
    }
    else if (NmosSpan_Equals(name, "sampling", 1))
    {
        text = r->sampling;
    }
    else if (NmosSpan_Equals(name, "colorimetry", 1))
    {
        text = r->colorimetry;
    }
    else if (NmosSpan_Equals(name, "TCS", 1))
    {
        text = r->tcs;
    }
    else if (NmosSpan_Equals(name, "RANGE", 1))
    {
        text = r->range;
    }
    else if (NmosSpan_Equals(name, "SSN", 1))
    {
        text = r->ssn;
    }
    else if (NmosSpan_Equals(name, "TP", 1))
    {
        text = r->transmitter_type;
        size = DTNMOS_MAX_SHORT_SIZE;
    }
    else
    {
        *handled = 0;
    }
    if (!valid)
    {
        return fail_at(line, "a=fmtp has a parameter whose value is no number or rate",
                       name);
    }
    return text == NULL ? DTNMOS_OK : copy_value(line, name, value, text, size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult build_video(const NmosSection* s, DtNmosVideoFormat* video)
{
    const NmosRaster r = {&video->Width,         &video->Height,
                          &video->RateNumerator, &video->RateDenominator,
                          &video->Interlaced,    &video->Segmented,
                          &video->Depth,         video->Sampling,
                          video->Colorimetry,    video->Tcs,
                          video->Range,          video->Ssn,
                          video->TransmitterType};
    NmosFmtpReader reader = {s->fmtp};
    NmosSpan name;
    NmosSpan value;
    while (next_parameter(&reader, &name, &value))
    {
        int handled = 0;
        DtNmosResult result = read_raster(s->fmtp_line, &r, name, value, &handled);
        if (result == DTNMOS_OK && !handled && NmosSpan_Equals(name, "PM", 1))
        {
            result = copy_value(s->fmtp_line, name, value, video->PackingMode,
                                sizeof(video->PackingMode));
        }
        if (result != DTNMOS_OK)
        {
            return result;
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_compressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult build_compressed(const NmosSection* s,
                                     DtNmosCompressedVideoFormat* video)
{
    if (!NmosText_CopySpan(video->Encoding, sizeof(video->Encoding), s->encoding))
    {
        return fail_at(s->line, "a=rtpmap has an encoding longer than RFC 6838 allows",
                       s->encoding);
    }
    video->TransmissionMode = 1;
    video->BandwidthKbps = s->bandwidth_kbps;
    const NmosRaster r = {&video->Width,         &video->Height,
                          &video->RateNumerator, &video->RateDenominator,
                          &video->Interlaced,    &video->Segmented,
                          &video->Depth,         video->Sampling,
                          video->Colorimetry,    video->Tcs,
                          video->Range,          video->Ssn,
                          video->TransmitterType};
    NmosFmtpReader reader = {s->fmtp};
    NmosSpan name;
    NmosSpan value;
    while (next_parameter(&reader, &name, &value))
    {
        int handled = 0;
        DtNmosResult result = read_raster(s->fmtp_line, &r, name, value, &handled);
        if (result != DTNMOS_OK)
        {
            return result;
        }
        if (handled)
        {
            continue;
        }
        char* text = NULL;
        uint32_t* number = NULL;
        if (NmosSpan_Equals(name, "profile", 1))
        {
            text = video->Profile;
        }
        else if (NmosSpan_Equals(name, "level", 1))
        {
            text = video->Level;
        }
        else if (NmosSpan_Equals(name, "sublevel", 1))
        {
            text = video->Sublevel;
        }
        else if (NmosSpan_Equals(name, "packetmode", 1))
        {
            number = &video->PacketMode;
        }
        else if (NmosSpan_Equals(name, "transmode", 1))
        {
            number = &video->TransmissionMode;
        }
        if (text != NULL)
        {
            result = copy_value(s->fmtp_line, name, value, text, DTNMOS_MAX_VALUE_SIZE);
            if (result != DTNMOS_OK)
            {
                return result;
            }
        }
        if (number != NULL && !NmosText_ParseU32(value, 255, number))
        {
            return fail_at(s->fmtp_line,
                           "a=fmtp has a parameter whose value is no number", name);
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult build_audio(const NmosSection* s, NmosStore* store,
                                DtNmosAudioFormat* audio)
{
    if (!NmosText_CopySpan(audio->Encoding, sizeof(audio->Encoding), s->encoding))
    {
        return fail_at(s->line, "a=rtpmap has an audio encoding longer than L24",
                       s->encoding);
    }
    audio->SampleRate = s->clock_rate;
    // RFC 4566 leaves one channel when a=rtpmap names none.
    audio->Channels = s->channels == 0 ? 1 : s->channels;
    if (s->ptime.length > 0 &&
        !NmosText_ParseMilliseconds(s->ptime, &audio->PacketTimeNs))
    {
        return fail_at(s->ptime_line, "a=ptime needs milliseconds, such as 1 or 0.125",
                       s->ptime);
    }
    NmosFmtpReader reader = {s->fmtp};
    NmosSpan name;
    NmosSpan value;
    while (next_parameter(&reader, &name, &value))
    {
        if (NmosSpan_Equals(name, "channel-order", 1))
        {
            audio->ChannelOrder = NmosStore_Text(store, value.data, value.length);
            if (audio->ChannelOrder == NULL)
            {
                return NmosError_FailMemory();
            }
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_did_sdid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads DID_SDID={0x61,0x02} into pair.
//
static int read_did_sdid(NmosSpan value, DtNmosDidSdid* pair)
{
    if (value.length < 2 || value.data[0] != '{' || value.data[value.length - 1] != '}')
    {
        return 0;
    }
    const NmosSpan inside = {value.data + 1, value.length - 2};
    NmosSpan did;
    const NmosSpan sdid = NmosSpan_Split(inside, ',', &did);
    return sdid.data != NULL && NmosText_ParseByte(NmosSpan_Trim(did), &pair->Did) &&
           NmosText_ParseByte(NmosSpan_Trim(sdid), &pair->Sdid);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_anc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult build_anc(const NmosSection* s, NmosStore* store, DtNmosFlow* flow)
{
    DtNmosAncFormat* anc = &flow->Format.Anc;
    DtNmosDidSdid pairs[64];
    size_t count = 0;
    NmosFmtpReader reader = {s->fmtp};
    NmosSpan name;
    NmosSpan value;
    while (next_parameter(&reader, &name, &value))
    {
        int valid = 1;
        char* text = NULL;
        size_t size = 0;
        if (NmosSpan_Equals(name, "DID_SDID", 1))
        {
            if (count == sizeof(pairs) / sizeof(pairs[0]))
            {
                return fail_at(s->fmtp_line, "a=fmtp has more DID_SDID than 64", name);
            }
            valid = read_did_sdid(value, &pairs[count++]);
        }
        else if (NmosSpan_Equals(name, "VPID_Code", 1))
        {
            valid = NmosText_ParseU32(value, 255, &anc->VpidCode);
        }
        else if (NmosSpan_Equals(name, "exactframerate", 1))
        {
            valid = NmosText_ParseRate(value, &anc->RateNumerator, &anc->RateDenominator);
        }
        else if (NmosSpan_Equals(name, "TM", 1))
        {
            text = anc->TransmissionModel;
            size = sizeof(anc->TransmissionModel);
        }
        else if (NmosSpan_Equals(name, "SSN", 1))
        {
            text = anc->Ssn;
            size = sizeof(anc->Ssn);
        }
        if (!valid)
        {
            return fail_at(s->fmtp_line,
                           "a=fmtp has a parameter whose value is not valid", value);
        }
        if (text != NULL)
        {
            const DtNmosResult result = copy_value(s->fmtp_line, name, value, text, size);
            if (result != DTNMOS_OK)
            {
                return result;
            }
        }
    }
    if (count > 0)
    {
        anc->DidSdid = NmosStore_Copy(store, pairs, count * sizeof(pairs[0]));
        if (anc->DidSdid == NULL)
        {
            return NmosError_FailMemory();
        }
        anc->DidSdidCount = count;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_second_leg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the mid of s is the second of a group of DUP.
//
static int is_second_leg(const NmosParser* p, const NmosSection* s)
{
    if (s->mid.length == 0)
    {
        return 0;
    }
    for (size_t i = 0; i < p->group_count; ++i)
    {
        const NmosSpan second = p->group_second[i];
        if (second.length == s->mid.length &&
            memcmp(second.data, s->mid.data, second.length) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_media_clock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a=mediaclk:direct=<offset> into the flow.
//
static DtNmosResult read_media_clock(const NmosSection* s, NmosSpan mediaclk,
                                     DtNmosFlow* flow)
{
    if (!NmosSpan_StartsWith(mediaclk, "direct="))
    {
        return DTNMOS_OK;
    }
    NmosSpan offset = {mediaclk.data + 7, mediaclk.length - 7};
    NmosSpan number;
    next_word(offset, &number);
    if (!NmosText_ParseU32(number, UINT32_MAX, &flow->MediaClockOffset))
    {
        return fail_at(s->line, "a=mediaclk:direct= needs an offset", mediaclk);
    }
    flow->MediaClockDirect = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_ptp_domain -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the domain after the grandmaster of ptp=, ":127" as ST 2110-10 §8.2 writes it or
// ":domain-nmbr=127" as RFC 7273 §4.8 does, into *domain; returns 0 for anything else,
// such as a domain-name, which the clock then keeps as text.
//
static int read_ptp_domain(NmosSpan text, int* domain)
{
    if (NmosSpan_StartsWith(text, "domain-nmbr="))
    {
        text.data += 12;
        text.length -= 12;
    }
    uint32_t number = 0;
    if (!NmosText_ParseU32(text, 127, &number))
    {
        return 0;
    }
    *domain = (int)number;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_ptp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the value of ptp=, <version>:<grandmaster>[:<domain>] or <version>:traceable,
// into clock; returns 0 when it is neither.
//
static int read_ptp(NmosSpan text, DtNmosRefClock* clock)
{
    NmosSpan version;
    const NmosSpan rest = NmosSpan_Split(text, ':', &version);
    NmosSpan grandmaster;
    const NmosSpan domain = NmosSpan_Split(rest, ':', &grandmaster);
    if (rest.data == NULL || version.length == 0 || grandmaster.length == 0 ||
        !NmosText_CopySpan(clock->PtpVersion, sizeof(clock->PtpVersion), version))
    {
        return 0;
    }
    if (NmosSpan_Equals(grandmaster, "traceable", 0))
    {
        clock->Traceable = 1;
        return domain.data == NULL;
    }
    return NmosText_CopySpan(clock->Grandmaster, sizeof(clock->Grandmaster),
                             grandmaster) &&
           (domain.data == NULL || read_ptp_domain(domain, &clock->Domain));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_ref_clock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the value of a=ts-refclk into clock: a PTP clock or the MAC of ST 2110-10 §8.2
// into its fields, any other kind as its text, owned by store.
//
static DtNmosResult read_ref_clock(NmosSpan text, NmosStore* store, DtNmosRefClock* clock)
{
    memset(clock, 0, sizeof(*clock));
    clock->Domain = -1;
    if (text.length == 0)
    {
        return DTNMOS_OK;
    }
    if (NmosSpan_StartsWith(text, "ptp="))
    {
        const NmosSpan value = {text.data + 4, text.length - 4};
        if (read_ptp(value, clock))
        {
            clock->Kind = DTNMOS_REFCLOCK_PTP;
            return DTNMOS_OK;
        }
    }
    else if (NmosSpan_StartsWith(text, "localmac="))
    {
        const NmosSpan mac = {text.data + 9, text.length - 9};
        if (mac.length > 0 &&
            NmosText_CopySpan(clock->LocalMac, sizeof(clock->LocalMac), mac))
        {
            clock->Kind = DTNMOS_REFCLOCK_LOCALMAC;
            return DTNMOS_OK;
        }
    }
    memset(clock, 0, sizeof(*clock));
    clock->Domain = -1;
    clock->Kind = DTNMOS_REFCLOCK_OTHER;
    clock->Text = NmosStore_Text(store, text.data, text.length);
    return clock->Text == NULL ? NmosError_FailMemory() : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult build_flow(NmosParser* p, const NmosSection* s, DtNmosFlow* flow)
{
    flow->Size = sizeof(*flow);
    flow->Media = media_of(s->encoding);
    flow->DestinationPort = s->port;
    flow->PayloadType = s->payload_type;
    flow->ClockRate = s->clock_rate;
    flow->Leg = is_second_leg(p, s) ? 1u : 0u;
    const NmosSharedLines* own = &s->shared;
    const NmosSharedLines* session = &p->session_lines;
    const NmosSpan connection =
        own->connection.length > 0 ? own->connection : session->connection;
    if (connection.length == 0)
    {
        return fail_at(s->line, "the media section has no c= and neither has the session",
                       (NmosSpan){"m=", 2});
    }
    const NmosSpan source =
        own->filter_source.length > 0 ? own->filter_source : session->filter_source;
    const NmosSpan refclk =
        own->ts_refclk.length > 0 ? own->ts_refclk : session->ts_refclk;
    const NmosSpan mediaclk =
        own->mediaclk.length > 0 ? own->mediaclk : session->mediaclk;
    if (!NmosText_CopySpan(flow->DestinationIp, sizeof(flow->DestinationIp),
                           connection) ||
        !NmosText_CopySpan(flow->SourceIp, sizeof(flow->SourceIp), source))
    {
        return fail_at(s->line, "an address is longer than a domain name may be",
                       connection);
    }
    DtNmosResult result = read_ref_clock(refclk, p->store, &flow->RefClock);
    if (result == DTNMOS_OK)
    {
        result = read_media_clock(s, mediaclk, flow);
    }
    if (result != DTNMOS_OK)
    {
        return result;
    }
    switch (flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        return build_video(s, &flow->Format.Video);
    case DTNMOS_MEDIA_AUDIO:
        return build_audio(s, p->store, &flow->Format.Audio);
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return build_compressed(s, &flow->Format.CompressedVideo);
    case DTNMOS_MEDIA_ANC:
        return build_anc(s, p->store, flow);
    case DTNMOS_MEDIA_OTHER:
    {
        DtNmosOtherFormat* other = &flow->Format.Other;
        const NmosSpan fmtp = NmosSpan_Trim(s->fmtp);
        if (!NmosText_CopySpan(other->Encoding, sizeof(other->Encoding), s->encoding))
        {
            return fail_at(s->line,
                           "a=rtpmap has an encoding longer than RFC 6838 allows",
                           s->encoding);
        }
        if (fmtp.length > 0)
        {
            other->Fmtp = NmosStore_Text(p->store, fmtp.data, fmtp.length);
            if (other->Fmtp == NULL)
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
DtNmosResult DtNmosSdp_Parse(const char* text, size_t length, DtNmosSdp** sdp)
{
    if (sdp == NULL || (text == NULL && length > 0))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosSdp_Parse() needs a text and a place for the SDP.");
    }
    *sdp = NULL;
    DtNmosSdp* result = calloc(1, sizeof(*result));
    if (result == NULL)
    {
        return NmosError_FailMemory();
    }
    result->session.Size = sizeof(result->session);
    NmosParser p;
    memset(&p, 0, sizeof(p));
    p.text = text;
    p.length = length;
    p.session = &result->session;
    p.store = &result->store;
    DtNmosResult status = read_lines(&p);
    if (status == DTNMOS_OK && p.count == 0)
    {
        status =
            NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The SDP has no media section.");
    }
    if (status == DTNMOS_OK)
    {
        result->flows = calloc(p.count, sizeof(*result->flows));
        if (result->flows == NULL)
        {
            status = NmosError_FailMemory();
        }
    }
    for (size_t i = 0; status == DTNMOS_OK && i < p.count; ++i)
    {
        status = build_flow(&p, &p.sections[i], &result->flows[i]);
        result->count = i + 1;
    }
    free(p.sections);
    if (status != DTNMOS_OK)
    {
        DtNmosSdp_Free(result);
        return status;
    }
    *sdp = result;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Session -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosSession* DtNmosSdp_Session(const DtNmosSdp* sdp)
{
    return sdp == NULL ? NULL : &sdp->session;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_FlowCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtNmosSdp_FlowCount(const DtNmosSdp* sdp)
{
    return sdp == NULL ? 0 : sdp->count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtNmosFlow* DtNmosSdp_Flow(const DtNmosSdp* sdp, size_t index)
{
    return sdp == NULL || index >= sdp->count ? NULL : &sdp->flows[index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosSdp_Free(DtNmosSdp* sdp)
{
    if (sdp == NULL)
    {
        return;
    }
    free(sdp->flows);
    NmosStore_Free(&sdp->store);
    free(sdp);
}
