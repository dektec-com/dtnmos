// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# sdp-write.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Writing an SDP (RFC 8866) as SMPTE ST 2110 describes a sender
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "dtnmos/sdp.h"
#include "internal.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_ipv6 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether address is IPv6, which holds a colon where IPv4 has none.
//
static int is_ipv6(const char* address)
{
    return strchr(address, ':') != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_ipv4_multicast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether address is an IPv4 multicast address, 224.0.0.0 to 239.255.255.255.
//
static int is_ipv4_multicast(const char* address)
{
    dtnmos_span first;
    dtnmos_span_split(dtnmos_span_of(address), '.', &first);
    uint32_t octet = 0;
    return dtnmos_parse_u32(first, 255, &octet) && octet >= 224 && octet <= 239;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- address_type -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static const char* address_type(const char* address)
{
    return is_ipv6(address) ? "IP6" : "IP4";
}

// Appends "; " between the parameters of an a=fmtp, and the payload type before the
// first.
typedef struct fmtp_writer
{
    dtnmos_buffer* buffer;
    uint8_t payload_type;
    int count;
} fmtp_writer;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- separate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void separate(fmtp_writer* writer)
{
    if (writer->count++ == 0)
    {
        dtnmos_buffer_printf(writer->buffer, "a=fmtp:%u ",
                             (unsigned)writer->payload_type);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(writer->buffer, "; ");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_text(fmtp_writer* writer, const char* name, const dtnmos_string* value)
{
    if (dtnmos_string_length(value) == 0)
    {
        return;
    }
    separate(writer);
    dtnmos_buffer_printf(writer->buffer, "%s=%s", name, dtnmos_string_get(value));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_number -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_number(fmtp_writer* writer, const char* name, uint32_t value)
{
    if (value == 0)
    {
        return;
    }
    separate(writer);
    dtnmos_buffer_printf(writer->buffer, "%s=%u", name, (unsigned)value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_rate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_rate(fmtp_writer* writer, uint32_t numerator, uint32_t denominator)
{
    if (numerator == 0)
    {
        return;
    }
    separate(writer);
    if (denominator <= 1)
    {
        dtnmos_buffer_printf(writer->buffer, "exactframerate=%u", (unsigned)numerator);
    }
    else
    {
        dtnmos_buffer_printf(writer->buffer, "exactframerate=%u/%u", (unsigned)numerator,
                             (unsigned)denominator);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_flag -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_flag(fmtp_writer* writer, const char* name, int set)
{
    if (!set)
    {
        return;
    }
    separate(writer);
    dtnmos_buffer_append(writer->buffer, name, strlen(name));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- end_fmtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void end_fmtp(fmtp_writer* writer)
{
    if (writer->count > 0)
    {
        DTNMOS_APPEND_LITERAL(writer->buffer, "\r\n");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void write_video(dtnmos_buffer* buffer, const dtnmos_flow* flow)
{
    const dtnmos_video_format* video = &flow->format.video;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u raw/%u\r\n", (unsigned)flow->payload_type,
                         (unsigned)flow->clock_rate);
    fmtp_writer writer = {buffer, flow->payload_type, 0};
    write_text(&writer, "sampling", &video->sampling);
    write_number(&writer, "width", video->width);
    write_number(&writer, "height", video->height);
    write_rate(&writer, video->rate_numerator, video->rate_denominator);
    write_number(&writer, "depth", video->depth);
    write_text(&writer, "TCS", &video->tcs);
    write_text(&writer, "colorimetry", &video->colorimetry);
    write_text(&writer, "RANGE", &video->range);
    write_text(&writer, "PM", &video->packing_mode);
    write_text(&writer, "SSN", &video->ssn);
    write_text(&writer, "TP", &video->transmitter_type);
    write_flag(&writer, "interlace", video->interlaced);
    write_flag(&writer, "segmented", video->segmented);
    end_fmtp(&writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_compressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_compressed(dtnmos_buffer* buffer, const dtnmos_flow* flow)
{
    const dtnmos_compressed_video_format* video = &flow->format.compressed_video;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u %s/%u\r\n", (unsigned)flow->payload_type,
                         dtnmos_string_get(&video->encoding), (unsigned)flow->clock_rate);
    fmtp_writer writer = {buffer, flow->payload_type, 0};
    separate(&writer);
    dtnmos_buffer_printf(buffer, "packetmode=%u", (unsigned)video->packet_mode);
    write_text(&writer, "profile", &video->profile);
    write_text(&writer, "level", &video->level);
    write_text(&writer, "sublevel", &video->sublevel);
    write_text(&writer, "sampling", &video->sampling);
    write_number(&writer, "width", video->width);
    write_number(&writer, "height", video->height);
    write_rate(&writer, video->rate_numerator, video->rate_denominator);
    write_number(&writer, "depth", video->depth);
    write_text(&writer, "TCS", &video->tcs);
    write_text(&writer, "colorimetry", &video->colorimetry);
    write_text(&writer, "RANGE", &video->range);
    write_text(&writer, "SSN", &video->ssn);
    write_text(&writer, "TP", &video->transmitter_type);
    if (video->transmission_mode != 1)
    {
        separate(&writer);
        dtnmos_buffer_printf(buffer, "transmode=%u", (unsigned)video->transmission_mode);
    }
    write_flag(&writer, "interlace", video->interlaced);
    write_flag(&writer, "segmented", video->segmented);
    end_fmtp(&writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_milliseconds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes a time in nanoseconds as milliseconds, "1" or "0.125".
//
static void write_milliseconds(dtnmos_buffer* buffer, uint32_t nanoseconds)
{
    const unsigned whole = (unsigned)(nanoseconds / 1000000u);
    unsigned fraction = (unsigned)(nanoseconds % 1000000u);
    if (fraction == 0)
    {
        dtnmos_buffer_printf(buffer, "%u", whole);
        return;
    }
    int digits = 6;
    while (fraction % 10 == 0)
    {
        fraction /= 10;
        --digits;
    }
    dtnmos_buffer_printf(buffer, "%u.%0*u", whole, digits, fraction);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void write_audio(dtnmos_buffer* buffer, const dtnmos_flow* flow)
{
    const dtnmos_audio_format* audio = &flow->format.audio;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u %s/%u/%u\r\n", (unsigned)flow->payload_type,
                         dtnmos_string_get(&audio->encoding),
                         (unsigned)audio->sample_rate, (unsigned)audio->channels);
    fmtp_writer writer = {buffer, flow->payload_type, 0};
    write_text(&writer, "channel-order", &audio->channel_order);
    end_fmtp(&writer);
    if (audio->packet_time_ns != 0)
    {
        DTNMOS_APPEND_LITERAL(buffer, "a=ptime:");
        write_milliseconds(buffer, audio->packet_time_ns);
        DTNMOS_APPEND_LITERAL(buffer, "\r\n");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_anc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void write_anc(dtnmos_buffer* buffer, const dtnmos_flow* flow)
{
    const dtnmos_anc_format* anc = &flow->format.anc;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u smpte291/%u\r\n",
                         (unsigned)flow->payload_type, (unsigned)flow->clock_rate);
    fmtp_writer writer = {buffer, flow->payload_type, 0};
    for (size_t i = 0; i < anc->did_sdid_count; ++i)
    {
        separate(&writer);
        dtnmos_buffer_printf(buffer, "DID_SDID={0x%02X,0x%02X}",
                             (unsigned)anc->did_sdid[i].did,
                             (unsigned)anc->did_sdid[i].sdid);
    }
    write_number(&writer, "VPID_Code", anc->vpid_code);
    write_rate(&writer, anc->rate_numerator, anc->rate_denominator);
    write_text(&writer, "TM", &anc->transmission_model);
    write_text(&writer, "SSN", &anc->ssn);
    end_fmtp(&writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_other -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void write_other(dtnmos_buffer* buffer, const dtnmos_flow* flow)
{
    const dtnmos_other_format* other = &flow->format.other;
    if (dtnmos_string_length(&other->encoding) > 0)
    {
        dtnmos_buffer_printf(
            buffer, "a=rtpmap:%u %s/%u\r\n", (unsigned)flow->payload_type,
            dtnmos_string_get(&other->encoding), (unsigned)flow->clock_rate);
    }
    if (dtnmos_string_length(&other->fmtp) > 0)
    {
        dtnmos_buffer_printf(buffer, "a=fmtp:%u %s\r\n", (unsigned)flow->payload_type,
                             dtnmos_string_get(&other->fmtp));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_flow(dtnmos_buffer* buffer, const dtnmos_flow* flow, size_t index,
                       int with_mid)
{
    const char* destination = dtnmos_string_get(&flow->destination_ip);
    dtnmos_buffer_printf(buffer, "m=%s %u RTP/AVP %u\r\n",
                         flow->media == DTNMOS_MEDIA_AUDIO ? "audio" : "video",
                         (unsigned)flow->destination_port, (unsigned)flow->payload_type);
    dtnmos_buffer_printf(buffer, "c=IN %s %s%s\r\n", address_type(destination),
                         destination, is_ipv4_multicast(destination) ? "/64" : "");
    if (flow->media == DTNMOS_MEDIA_COMPRESSED_VIDEO &&
        flow->format.compressed_video.bandwidth_kbps != 0)
    {
        dtnmos_buffer_printf(
            buffer, "b=AS:%llu\r\n",
            (unsigned long long)flow->format.compressed_video.bandwidth_kbps);
    }
    if (dtnmos_string_length(&flow->source_ip) > 0)
    {
        dtnmos_buffer_printf(buffer, "a=source-filter: incl IN %s %s %s\r\n",
                             address_type(destination), destination,
                             dtnmos_string_get(&flow->source_ip));
    }
    switch (flow->media)
    {
    case DTNMOS_MEDIA_VIDEO:
        write_video(buffer, flow);
        break;
    case DTNMOS_MEDIA_AUDIO:
        write_audio(buffer, flow);
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        write_compressed(buffer, flow);
        break;
    case DTNMOS_MEDIA_ANC:
        write_anc(buffer, flow);
        break;
    case DTNMOS_MEDIA_OTHER:
        write_other(buffer, flow);
        break;
    }
    if (dtnmos_string_length(&flow->ts_refclk) > 0)
    {
        dtnmos_buffer_printf(buffer, "a=ts-refclk:%s\r\n",
                             dtnmos_string_get(&flow->ts_refclk));
    }
    if (flow->media_clock_direct)
    {
        dtnmos_buffer_printf(buffer, "a=mediaclk:direct=%u\r\n",
                             (unsigned)flow->media_clock_offset);
    }
    if (with_mid)
    {
        dtnmos_buffer_printf(buffer, "a=mid:%s%zu\r\n",
                             flow->leg == 1 ? "secondary" : "primary", index);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_sdp_write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_sdp_write(const dtnmos_session* session, const dtnmos_flow* flows,
                               size_t count, dtnmos_string* text, dtnmos_error* error)
{
    if (session == NULL || (flows == NULL && count > 0) || text == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "dtnmos_sdp_write() needs a session, its flows and a text.");
    }
    if (count == 0)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "An SDP needs at least one flow.");
    }
    if (dtnmos_string_length(&session->origin_ip) == 0)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "An SDP needs the address of its sender in origin_ip.");
    }
    int with_mid = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (dtnmos_string_length(&flows[i].destination_ip) == 0 ||
            flows[i].destination_port == 0)
        {
            return dtnmos_fail(
                error, DTNMOS_E_INVALID_ARGUMENT,
                "Flow %zu of the SDP needs a destination address and port.", i);
        }
        if (flows[i].leg == 1)
        {
            if (i == 0 || flows[i - 1].leg != 0)
            {
                return dtnmos_fail(
                    error, DTNMOS_E_INVALID_ARGUMENT,
                    "Flow %zu is a second path and needs the first right before it.", i);
            }
            with_mid = 1;
        }
    }

    dtnmos_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    const char* origin = dtnmos_string_get(&session->origin_ip);
    DTNMOS_APPEND_LITERAL(&buffer, "v=0\r\n");
    dtnmos_buffer_printf(
        &buffer, "o=- %llu %llu IN %s %s\r\n", (unsigned long long)session->session_id,
        (unsigned long long)session->session_version, address_type(origin), origin);
    // RFC 8866 asks for a single space when a session has no name.
    dtnmos_buffer_printf(&buffer, "s=%s\r\n",
                         dtnmos_string_length(&session->name) > 0
                             ? dtnmos_string_get(&session->name)
                             : " ");
    DTNMOS_APPEND_LITERAL(&buffer, "t=0 0\r\n");
    for (size_t i = 0; with_mid && i < count; ++i)
    {
        if (flows[i].leg == 1)
        {
            // Flow i is named primary<i> or secondary<i>; the first path is the flow
            // before.
            dtnmos_buffer_printf(&buffer, "a=group:DUP primary%zu secondary%zu\r\n",
                                 i - 1, i);
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        write_flow(&buffer, &flows[i], i, with_mid);
    }
    if (buffer.failed)
    {
        dtnmos_buffer_free(&buffer);
        return dtnmos_fail_memory(error);
    }
    const dtnmos_result result = dtnmos_string_set(text, buffer.data, buffer.length);
    dtnmos_buffer_free(&buffer);
    if (result != DTNMOS_OK)
    {
        return dtnmos_fail_memory(error);
    }
    return DTNMOS_OK;
}
