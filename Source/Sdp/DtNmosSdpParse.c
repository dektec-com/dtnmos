// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosSdpParse.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Reading an SDP (RFC 8866) as SMPTE ST 2110 writes it
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "dtnmos_sdp.h"

struct DtNmosSdp
{
    DtNmosSession session;
    DtNmosFlow* flows;
    size_t count;
};

// The attributes that the session level and a media section both may carry.
typedef struct shared_lines
{
    dtnmos_span connection; // the address of c=, without TTL
    size_t connection_line;
    dtnmos_span filter_source; // the first source of a=source-filter: incl
    dtnmos_span ts_refclk;
    dtnmos_span mediaclk; // the value of a=mediaclk
} shared_lines;

// A media section as its lines give it.
typedef struct section
{
    size_t line; // of its m=
    uint16_t port;
    uint8_t payload_type;
    shared_lines shared;
    dtnmos_span encoding; // of the a=rtpmap of payload_type
    uint32_t clock_rate;
    uint32_t channels; // the encoding parameters of a=rtpmap; 0 when absent
    dtnmos_span fmtp;
    size_t fmtp_line;
    dtnmos_span ptime;
    size_t ptime_line;
    dtnmos_span mid;
    uint64_t bandwidth_kbps;
} section;

typedef struct parser
{
    const char* text;
    size_t length;
    size_t position;
    size_t line; // number of the current line, from 1
    DtNmosSession* session;
    shared_lines session_lines;
    dtnmos_span group_second[16]; // the second mid of each a=group:DUP
    size_t group_count;
    section* sections;
    size_t count;
    size_t capacity;
} parser;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fail_line -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult fail_line(parser* p, const char* what, dtnmos_span text)
{
    return dtnmos_fail(DTNMOS_E_PARSE, "SDP line %zu: %s: '%.*s'", p->line, what,
                       (int)(text.length > 120 ? 120 : text.length), text.data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fail_at -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult fail_at(size_t line, const char* what, dtnmos_span text)
{
    return dtnmos_fail(DTNMOS_E_PARSE, "SDP line %zu: %s: '%.*s'", line, what,
                       (int)(text.length > 120 ? 120 : text.length), text.data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_line -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the next line without its end of line, or 0 at the end of the text.
//
static int next_line(parser* p, dtnmos_span* line)
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
static dtnmos_span next_word(dtnmos_span span, dtnmos_span* word)
{
    span = dtnmos_span_trim(span);
    const dtnmos_span rest = dtnmos_span_split(span, ' ', word);
    return dtnmos_span_trim(rest);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_connection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult read_connection(parser* p, dtnmos_span value, shared_lines* lines)
{
    dtnmos_span network;
    dtnmos_span type;
    dtnmos_span address;
    dtnmos_span rest = next_word(value, &network);
    rest = next_word(rest, &type);
    next_word(rest, &address);
    if (!dtnmos_span_equals(network, "IN", 0) ||
        (!dtnmos_span_equals(type, "IP4", 0) && !dtnmos_span_equals(type, "IP6", 0)) ||
        address.length == 0)
    {
        return fail_line(p, "c= needs IN IP4 or IN IP6 and an address", value);
    }
    // A multicast address of IPv4 carries its TTL, and either may carry a count, after
    // '/'.
    dtnmos_span host;
    dtnmos_span_split(address, '/', &host);
    lines->connection = host;
    lines->connection_line = p->line;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_origin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult read_origin(parser* p, dtnmos_span value)
{
    dtnmos_span user;
    dtnmos_span id;
    dtnmos_span version;
    dtnmos_span network;
    dtnmos_span type;
    dtnmos_span address;
    dtnmos_span rest = next_word(value, &user);
    rest = next_word(rest, &id);
    rest = next_word(rest, &version);
    rest = next_word(rest, &network);
    rest = next_word(rest, &type);
    next_word(rest, &address);
    if (!dtnmos_parse_u64(id, UINT64_MAX, &p->session->SessionId) ||
        !dtnmos_parse_u64(version, UINT64_MAX, &p->session->SessionVersion) ||
        address.length == 0)
    {
        return fail_line(p, "o= needs a user, a session ID, a version and an address",
                         value);
    }
    if (dtnmos_string_set_span(&p->session->OriginIp, address) != DTNMOS_OK)
    {
        return dtnmos_fail_memory();
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_media -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_media(parser* p, dtnmos_span value)
{
    if (p->count == p->capacity)
    {
        const size_t capacity = p->capacity == 0 ? 4 : p->capacity * 2;
        section* sections = realloc(p->sections, capacity * sizeof(*sections));
        if (sections == NULL)
        {
            return dtnmos_fail_memory();
        }
        p->sections = sections;
        p->capacity = capacity;
    }
    section* s = &p->sections[p->count];
    memset(s, 0, sizeof(*s));
    s->line = p->line;
    dtnmos_span media;
    dtnmos_span port;
    dtnmos_span protocol;
    dtnmos_span format;
    dtnmos_span rest = next_word(value, &media);
    rest = next_word(rest, &port);
    rest = next_word(rest, &protocol);
    next_word(rest, &format);
    dtnmos_span port_number;
    dtnmos_span_split(port, '/', &port_number);
    uint32_t number = 0;
    uint32_t payload_type = 0;
    if (media.length == 0 || !dtnmos_parse_u32(port_number, 65535, &number) ||
        protocol.length == 0 || !dtnmos_parse_u32(format, 127, &payload_type))
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
static int read_payload_type(dtnmos_span value, uint32_t* payload_type, dtnmos_span* rest)
{
    dtnmos_span number;
    *rest = next_word(value, &number);
    return dtnmos_parse_u32(number, 127, payload_type);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_rtpmap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult read_rtpmap(parser* p, section* s, dtnmos_span value)
{
    uint32_t payload_type = 0;
    dtnmos_span map;
    if (!read_payload_type(value, &payload_type, &map))
    {
        return fail_line(p, "a=rtpmap needs a payload type", value);
    }
    if (payload_type != s->payload_type)
    {
        return DTNMOS_OK;
    }
    dtnmos_span encoding;
    dtnmos_span clock;
    dtnmos_span rest = dtnmos_span_split(map, '/', &encoding);
    const dtnmos_span parameters = dtnmos_span_split(rest, '/', &clock);
    s->encoding = dtnmos_span_trim(encoding);
    if (s->encoding.length == 0 || !dtnmos_parse_u32(clock, UINT32_MAX, &s->clock_rate) ||
        (parameters.data != NULL && !dtnmos_parse_u32(parameters, 65535, &s->channels)))
    {
        return fail_line(
            p, "a=rtpmap needs an encoding, a clock rate and optional channels", value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_source_filter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_source_filter(parser* p, dtnmos_span value, shared_lines* lines)
{
    // a=source-filter: incl IN IP4 <destination> <source> ...
    dtnmos_span mode;
    dtnmos_span network;
    dtnmos_span type;
    dtnmos_span destination;
    dtnmos_span source;
    dtnmos_span rest = next_word(value, &mode);
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
    if (dtnmos_span_equals(mode, "incl", 1) && lines->filter_source.length == 0)
    {
        lines->filter_source = source;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_attribute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads an attribute into the lines of the session or of the current section.
//
static DtNmosResult read_attribute(parser* p, dtnmos_span line)
{
    dtnmos_span name;
    const dtnmos_span value = dtnmos_span_split(line, ':', &name);
    section* s = p->count == 0 ? NULL : &p->sections[p->count - 1];
    shared_lines* lines = s == NULL ? &p->session_lines : &s->shared;
    if (dtnmos_span_equals(name, "source-filter", 0))
    {
        return read_source_filter(p, value, lines);
    }
    if (dtnmos_span_equals(name, "ts-refclk", 0))
    {
        if (lines->ts_refclk.length == 0)
        {
            lines->ts_refclk = dtnmos_span_trim(value);
        }
        return DTNMOS_OK;
    }
    if (dtnmos_span_equals(name, "mediaclk", 0))
    {
        lines->mediaclk = dtnmos_span_trim(value);
        return DTNMOS_OK;
    }
    if (dtnmos_span_equals(name, "group", 0))
    {
        dtnmos_span semantics;
        dtnmos_span rest = next_word(value, &semantics);
        dtnmos_span first;
        dtnmos_span second;
        rest = next_word(rest, &first);
        next_word(rest, &second);
        if (dtnmos_span_equals(semantics, "DUP", 0) && second.length > 0 &&
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
    if (dtnmos_span_equals(name, "rtpmap", 0))
    {
        return read_rtpmap(p, s, value);
    }
    if (dtnmos_span_equals(name, "fmtp", 0))
    {
        uint32_t payload_type = 0;
        dtnmos_span parameters;
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
    if (dtnmos_span_equals(name, "ptime", 0))
    {
        s->ptime = dtnmos_span_trim(value);
        s->ptime_line = p->line;
        return DTNMOS_OK;
    }
    if (dtnmos_span_equals(name, "mid", 0))
    {
        s->mid = dtnmos_span_trim(value);
        return DTNMOS_OK;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_bandwidth -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_bandwidth(parser* p, dtnmos_span value)
{
    if (p->count == 0)
    {
        return DTNMOS_OK;
    }
    dtnmos_span type;
    const dtnmos_span amount = dtnmos_span_split(value, ':', &type);
    if (dtnmos_span_equals(type, "AS", 0) &&
        !dtnmos_parse_u64(dtnmos_span_trim(amount), UINT64_MAX,
                          &p->sections[p->count - 1].bandwidth_kbps))
    {
        return fail_line(p, "b=AS needs a bandwidth in kilobits per second", value);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult read_lines(parser* p)
{
    dtnmos_span line;
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
        const dtnmos_span value = {line.data + 2, line.length - 2};
        DtNmosResult result = DTNMOS_OK;
        switch (line.data[0])
        {
        case 'o':
            result = read_origin(p, value);
            break;
        case 's':
            if (dtnmos_string_set_span(&p->session->Name, value) != DTNMOS_OK)
            {
                result = dtnmos_fail_memory();
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
static DtNmosMedia media_of(dtnmos_span encoding)
{
    if (dtnmos_span_equals(encoding, "raw", 1))
    {
        return DTNMOS_MEDIA_VIDEO;
    }
    if (dtnmos_span_equals(encoding, "L24", 1) ||
        dtnmos_span_equals(encoding, "L16", 1) ||
        dtnmos_span_equals(encoding, "AM824", 1))
    {
        return DTNMOS_MEDIA_AUDIO;
    }
    if (dtnmos_span_equals(encoding, "jxsv", 1))
    {
        return DTNMOS_MEDIA_COMPRESSED_VIDEO;
    }
    if (dtnmos_span_equals(encoding, "smpte291", 1))
    {
        return DTNMOS_MEDIA_ANC;
    }
    return DTNMOS_MEDIA_OTHER;
}

// The parameters of an a=fmtp: pairs of a name and a value, separated by ';'.
typedef struct fmtp_reader
{
    dtnmos_span rest;
} fmtp_reader;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_parameter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the next parameter into name and value; value is empty for a flag such as
// interlace.
//
static int next_parameter(fmtp_reader* reader, dtnmos_span* name, dtnmos_span* value)
{
    while (reader->rest.data != NULL)
    {
        dtnmos_span parameter;
        reader->rest = dtnmos_span_split(reader->rest, ';', &parameter);
        parameter = dtnmos_span_trim(parameter);
        if (parameter.length == 0)
        {
            continue;
        }
        const dtnmos_span after = dtnmos_span_split(parameter, '=', name);
        *name = dtnmos_span_trim(*name);
        if (after.data == NULL)
        {
            value->data = parameter.data + parameter.length;
            value->length = 0;
        }
        else
        {
            *value = dtnmos_span_trim(after);
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
static int flag_value(dtnmos_span value)
{
    return value.length == 0 || !dtnmos_span_equals(value, "0", 0);
}

// The raster, rate and colour that ST 2110-20 and -22 share.
typedef struct raster
{
    uint32_t* width;
    uint32_t* height;
    uint32_t* rate_numerator;
    uint32_t* rate_denominator;
    int* interlaced;
    int* segmented;
    uint32_t* depth;
    DtNmosString* sampling;
    DtNmosString* colorimetry;
    DtNmosString* tcs;
    DtNmosString* range;
    DtNmosString* ssn;
    DtNmosString* transmitter_type;
} raster;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_raster -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the parameter name of the raster; sets handled when it is one of them.
//
static DtNmosResult read_raster(size_t line, const raster* r, dtnmos_span name,
                                dtnmos_span value, int* handled)
{
    *handled = 1;
    int valid = 1;
    DtNmosString* text = NULL;
    if (dtnmos_span_equals(name, "width", 1))
    {
        valid = dtnmos_parse_u32(value, 65535, r->width);
    }
    else if (dtnmos_span_equals(name, "height", 1))
    {
        valid = dtnmos_parse_u32(value, 65535, r->height);
    }
    else if (dtnmos_span_equals(name, "exactframerate", 1))
    {
        valid = dtnmos_parse_rate(value, r->rate_numerator, r->rate_denominator);
    }
    else if (dtnmos_span_equals(name, "depth", 1))
    {
        valid = dtnmos_parse_u32(value, 64, r->depth);
    }
    else if (dtnmos_span_equals(name, "interlace", 1))
    {
        *r->interlaced = flag_value(value);
    }
    else if (dtnmos_span_equals(name, "segmented", 1))
    {
        *r->segmented = flag_value(value);
    }
    else if (dtnmos_span_equals(name, "sampling", 1))
    {
        text = r->sampling;
    }
    else if (dtnmos_span_equals(name, "colorimetry", 1))
    {
        text = r->colorimetry;
    }
    else if (dtnmos_span_equals(name, "TCS", 1))
    {
        text = r->tcs;
    }
    else if (dtnmos_span_equals(name, "RANGE", 1))
    {
        text = r->range;
    }
    else if (dtnmos_span_equals(name, "SSN", 1))
    {
        text = r->ssn;
    }
    else if (dtnmos_span_equals(name, "TP", 1))
    {
        text = r->transmitter_type;
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
    if (text != NULL && dtnmos_string_set_span(text, value) != DTNMOS_OK)
    {
        return dtnmos_fail_memory();
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult build_video(const section* s, DtNmosVideoFormat* video)
{
    const raster r = {&video->Width,          &video->Height,
                      &video->RateNumerator,  &video->RateDenominator,
                      &video->Interlaced,     &video->Segmented,
                      &video->Depth,          &video->Sampling,
                      &video->Colorimetry,    &video->Tcs,
                      &video->Range,          &video->Ssn,
                      &video->TransmitterType};
    fmtp_reader reader = {s->fmtp};
    dtnmos_span name;
    dtnmos_span value;
    while (next_parameter(&reader, &name, &value))
    {
        int handled = 0;
        DtNmosResult result = read_raster(s->fmtp_line, &r, name, value, &handled);
        if (result != DTNMOS_OK)
        {
            return result;
        }
        if (!handled && dtnmos_span_equals(name, "PM", 1) &&
            dtnmos_string_set_span(&video->PackingMode, value) != DTNMOS_OK)
        {
            return dtnmos_fail_memory();
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_compressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult build_compressed(const section* s, DtNmosCompressedVideoFormat* video)
{
    if (dtnmos_string_set_span(&video->Encoding, s->encoding) != DTNMOS_OK)
    {
        return dtnmos_fail_memory();
    }
    video->TransmissionMode = 1;
    video->BandwidthKbps = s->bandwidth_kbps;
    const raster r = {&video->Width,          &video->Height,
                      &video->RateNumerator,  &video->RateDenominator,
                      &video->Interlaced,     &video->Segmented,
                      &video->Depth,          &video->Sampling,
                      &video->Colorimetry,    &video->Tcs,
                      &video->Range,          &video->Ssn,
                      &video->TransmitterType};
    fmtp_reader reader = {s->fmtp};
    dtnmos_span name;
    dtnmos_span value;
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
        DtNmosString* text = NULL;
        uint32_t* number = NULL;
        if (dtnmos_span_equals(name, "profile", 1))
        {
            text = &video->Profile;
        }
        else if (dtnmos_span_equals(name, "level", 1))
        {
            text = &video->Level;
        }
        else if (dtnmos_span_equals(name, "sublevel", 1))
        {
            text = &video->Sublevel;
        }
        else if (dtnmos_span_equals(name, "packetmode", 1))
        {
            number = &video->PacketMode;
        }
        else if (dtnmos_span_equals(name, "transmode", 1))
        {
            number = &video->TransmissionMode;
        }
        if (text != NULL && dtnmos_string_set_span(text, value) != DTNMOS_OK)
        {
            return dtnmos_fail_memory();
        }
        if (number != NULL && !dtnmos_parse_u32(value, 255, number))
        {
            return fail_at(s->fmtp_line,
                           "a=fmtp has a parameter whose value is no number", name);
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult build_audio(const section* s, DtNmosAudioFormat* audio)
{
    if (dtnmos_string_set_span(&audio->Encoding, s->encoding) != DTNMOS_OK)
    {
        return dtnmos_fail_memory();
    }
    audio->SampleRate = s->clock_rate;
    // RFC 4566 leaves one channel when a=rtpmap names none.
    audio->Channels = s->channels == 0 ? 1 : s->channels;
    if (s->ptime.length > 0 && !dtnmos_parse_milliseconds(s->ptime, &audio->PacketTimeNs))
    {
        return fail_at(s->ptime_line, "a=ptime needs milliseconds, such as 1 or 0.125",
                       s->ptime);
    }
    fmtp_reader reader = {s->fmtp};
    dtnmos_span name;
    dtnmos_span value;
    while (next_parameter(&reader, &name, &value))
    {
        if (dtnmos_span_equals(name, "channel-order", 1) &&
            dtnmos_string_set_span(&audio->ChannelOrder, value) != DTNMOS_OK)
        {
            return dtnmos_fail_memory();
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- read_did_sdid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads DID_SDID={0x61,0x02} into pair.
//
static int read_did_sdid(dtnmos_span value, DtNmosDidSdid* pair)
{
    if (value.length < 2 || value.data[0] != '{' || value.data[value.length - 1] != '}')
    {
        return 0;
    }
    const dtnmos_span inside = {value.data + 1, value.length - 2};
    dtnmos_span did;
    const dtnmos_span sdid = dtnmos_span_split(inside, ',', &did);
    return sdid.data != NULL && dtnmos_parse_byte(dtnmos_span_trim(did), &pair->Did) &&
           dtnmos_parse_byte(dtnmos_span_trim(sdid), &pair->Sdid);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_anc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult build_anc(const section* s, DtNmosFlow* flow)
{
    DtNmosAncFormat* anc = &flow->Format.Anc;
    DtNmosDidSdid pairs[64];
    size_t count = 0;
    fmtp_reader reader = {s->fmtp};
    dtnmos_span name;
    dtnmos_span value;
    while (next_parameter(&reader, &name, &value))
    {
        int valid = 1;
        DtNmosString* text = NULL;
        if (dtnmos_span_equals(name, "DID_SDID", 1))
        {
            if (count == sizeof(pairs) / sizeof(pairs[0]))
            {
                return fail_at(s->fmtp_line, "a=fmtp has more DID_SDID than 64", name);
            }
            valid = read_did_sdid(value, &pairs[count++]);
        }
        else if (dtnmos_span_equals(name, "VPID_Code", 1))
        {
            valid = dtnmos_parse_u32(value, 255, &anc->VpidCode);
        }
        else if (dtnmos_span_equals(name, "exactframerate", 1))
        {
            valid = dtnmos_parse_rate(value, &anc->RateNumerator, &anc->RateDenominator);
        }
        else if (dtnmos_span_equals(name, "TM", 1))
        {
            text = &anc->TransmissionModel;
        }
        else if (dtnmos_span_equals(name, "SSN", 1))
        {
            text = &anc->Ssn;
        }
        if (!valid)
        {
            return fail_at(s->fmtp_line,
                           "a=fmtp has a parameter whose value is not valid", value);
        }
        if (text != NULL && dtnmos_string_set_span(text, value) != DTNMOS_OK)
        {
            return dtnmos_fail_memory();
        }
    }
    if (DtNmosFlow_SetDidSdid(flow, pairs, count) != DTNMOS_OK)
    {
        return dtnmos_fail_memory();
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_second_leg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the mid of s is the second of a group of DUP.
//
static int is_second_leg(const parser* p, const section* s)
{
    if (s->mid.length == 0)
    {
        return 0;
    }
    for (size_t i = 0; i < p->group_count; ++i)
    {
        const dtnmos_span second = p->group_second[i];
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
static DtNmosResult read_media_clock(const section* s, dtnmos_span mediaclk,
                                     DtNmosFlow* flow)
{
    if (!dtnmos_span_starts_with(mediaclk, "direct="))
    {
        return DTNMOS_OK;
    }
    dtnmos_span offset = {mediaclk.data + 7, mediaclk.length - 7};
    dtnmos_span number;
    next_word(offset, &number);
    if (!dtnmos_parse_u32(number, UINT32_MAX, &flow->MediaClockOffset))
    {
        return fail_at(s->line, "a=mediaclk:direct= needs an offset", mediaclk);
    }
    flow->MediaClockDirect = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- build_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult build_flow(parser* p, const section* s, DtNmosFlow* flow)
{
    flow->Size = sizeof(*flow);
    flow->Media = media_of(s->encoding);
    flow->DestinationPort = s->port;
    flow->PayloadType = s->payload_type;
    flow->ClockRate = s->clock_rate;
    flow->Leg = is_second_leg(p, s) ? 1u : 0u;
    const shared_lines* own = &s->shared;
    const shared_lines* session = &p->session_lines;
    const dtnmos_span connection =
        own->connection.length > 0 ? own->connection : session->connection;
    if (connection.length == 0)
    {
        return fail_at(s->line, "the media section has no c= and neither has the session",
                       (dtnmos_span){"m=", 2});
    }
    const dtnmos_span source =
        own->filter_source.length > 0 ? own->filter_source : session->filter_source;
    const dtnmos_span refclk =
        own->ts_refclk.length > 0 ? own->ts_refclk : session->ts_refclk;
    const dtnmos_span mediaclk =
        own->mediaclk.length > 0 ? own->mediaclk : session->mediaclk;
    if (dtnmos_string_set_span(&flow->DestinationIp, connection) != DTNMOS_OK ||
        dtnmos_string_set_span(&flow->SourceIp, source) != DTNMOS_OK ||
        dtnmos_string_set_span(&flow->TsRefclk, refclk) != DTNMOS_OK)
    {
        return dtnmos_fail_memory();
    }
    DtNmosResult result = read_media_clock(s, mediaclk, flow);
    if (result != DTNMOS_OK)
    {
        return result;
    }
    switch (flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        return build_video(s, &flow->Format.Video);
    case DTNMOS_MEDIA_AUDIO:
        return build_audio(s, &flow->Format.Audio);
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return build_compressed(s, &flow->Format.CompressedVideo);
    case DTNMOS_MEDIA_ANC:
        return build_anc(s, flow);
    case DTNMOS_MEDIA_OTHER:
        if (dtnmos_string_set_span(&flow->Format.Other.Encoding, s->encoding) !=
                DTNMOS_OK ||
            dtnmos_string_set_span(&flow->Format.Other.Fmtp, dtnmos_span_trim(s->fmtp)) !=
                DTNMOS_OK)
        {
            return dtnmos_fail_memory();
        }
        return DTNMOS_OK;
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSdp_Parse(const char* text, size_t length, DtNmosSdp** sdp)
{
    if (sdp == NULL || (text == NULL && length > 0))
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosSdp_Parse() needs a text and a place for the SDP.");
    }
    *sdp = NULL;
    DtNmosSdp* result = calloc(1, sizeof(*result));
    if (result == NULL)
    {
        return dtnmos_fail_memory();
    }
    result->session.Size = sizeof(result->session);
    parser p;
    memset(&p, 0, sizeof(p));
    p.text = text;
    p.length = length;
    p.session = &result->session;
    DtNmosResult status = read_lines(&p);
    if (status == DTNMOS_OK && p.count == 0)
    {
        status = dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT, "The SDP has no media section.");
    }
    if (status == DTNMOS_OK)
    {
        result->flows = calloc(p.count, sizeof(*result->flows));
        if (result->flows == NULL)
        {
            status = dtnmos_fail_memory();
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
    for (size_t i = 0; i < sdp->count; ++i)
    {
        DtNmosFlow_Clear(&sdp->flows[i]);
    }
    free(sdp->flows);
    DtNmosSession_Clear(&sdp->session);
    free(sdp);
}
