// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosSdpWrite.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Writing an SDP (RFC 8866) as SMPTE ST 2110 describes a sender
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "NmosFlow.h"

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
// Writes name=value; nothing when value is null or empty.
//
static void write_text(fmtp_writer* writer, const char* name, const char* value)
{
    if (value == NULL || value[0] == '\0')
    {
        return;
    }
    separate(writer);
    dtnmos_buffer_printf(writer->buffer, "%s=%s", name, value);
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
static void write_video(dtnmos_buffer* buffer, const DtNmosFlow* flow)
{
    const DtNmosVideoFormat* video = &flow->Format.Video;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u raw/%u\r\n", (unsigned)flow->PayloadType,
                         (unsigned)flow->ClockRate);
    fmtp_writer writer = {buffer, flow->PayloadType, 0};
    write_text(&writer, "sampling", video->Sampling);
    write_number(&writer, "width", video->Width);
    write_number(&writer, "height", video->Height);
    write_rate(&writer, video->RateNumerator, video->RateDenominator);
    write_number(&writer, "depth", video->Depth);
    write_text(&writer, "TCS", video->Tcs);
    write_text(&writer, "colorimetry", video->Colorimetry);
    write_text(&writer, "RANGE", video->Range);
    write_text(&writer, "PM", video->PackingMode);
    write_text(&writer, "SSN", video->Ssn);
    write_text(&writer, "TP", video->TransmitterType);
    write_flag(&writer, "interlace", video->Interlaced);
    write_flag(&writer, "segmented", video->Segmented);
    end_fmtp(&writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_compressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_compressed(dtnmos_buffer* buffer, const DtNmosFlow* flow)
{
    const DtNmosCompressedVideoFormat* video = &flow->Format.CompressedVideo;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u %s/%u\r\n", (unsigned)flow->PayloadType,
                         video->Encoding, (unsigned)flow->ClockRate);
    fmtp_writer writer = {buffer, flow->PayloadType, 0};
    separate(&writer);
    dtnmos_buffer_printf(buffer, "packetmode=%u", (unsigned)video->PacketMode);
    write_text(&writer, "profile", video->Profile);
    write_text(&writer, "level", video->Level);
    write_text(&writer, "sublevel", video->Sublevel);
    write_text(&writer, "sampling", video->Sampling);
    write_number(&writer, "width", video->Width);
    write_number(&writer, "height", video->Height);
    write_rate(&writer, video->RateNumerator, video->RateDenominator);
    write_number(&writer, "depth", video->Depth);
    write_text(&writer, "TCS", video->Tcs);
    write_text(&writer, "colorimetry", video->Colorimetry);
    write_text(&writer, "RANGE", video->Range);
    write_text(&writer, "SSN", video->Ssn);
    write_text(&writer, "TP", video->TransmitterType);
    if (video->TransmissionMode != 1)
    {
        separate(&writer);
        dtnmos_buffer_printf(buffer, "transmode=%u", (unsigned)video->TransmissionMode);
    }
    write_flag(&writer, "interlace", video->Interlaced);
    write_flag(&writer, "segmented", video->Segmented);
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
static void write_audio(dtnmos_buffer* buffer, const DtNmosFlow* flow)
{
    const DtNmosAudioFormat* audio = &flow->Format.Audio;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u %s/%u/%u\r\n", (unsigned)flow->PayloadType,
                         audio->Encoding, (unsigned)audio->SampleRate,
                         (unsigned)audio->Channels);
    fmtp_writer writer = {buffer, flow->PayloadType, 0};
    write_text(&writer, "channel-order", audio->ChannelOrder);
    end_fmtp(&writer);
    if (audio->PacketTimeNs != 0)
    {
        DTNMOS_APPEND_LITERAL(buffer, "a=ptime:");
        write_milliseconds(buffer, audio->PacketTimeNs);
        DTNMOS_APPEND_LITERAL(buffer, "\r\n");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_anc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void write_anc(dtnmos_buffer* buffer, const DtNmosFlow* flow)
{
    const DtNmosAncFormat* anc = &flow->Format.Anc;
    dtnmos_buffer_printf(buffer, "a=rtpmap:%u smpte291/%u\r\n",
                         (unsigned)flow->PayloadType, (unsigned)flow->ClockRate);
    fmtp_writer writer = {buffer, flow->PayloadType, 0};
    for (size_t i = 0; i < anc->DidSdidCount; ++i)
    {
        separate(&writer);
        dtnmos_buffer_printf(buffer, "DID_SDID={0x%02X,0x%02X}",
                             (unsigned)anc->DidSdid[i].Did,
                             (unsigned)anc->DidSdid[i].Sdid);
    }
    write_number(&writer, "VPID_Code", anc->VpidCode);
    write_rate(&writer, anc->RateNumerator, anc->RateDenominator);
    write_text(&writer, "TM", anc->TransmissionModel);
    write_text(&writer, "SSN", anc->Ssn);
    end_fmtp(&writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_other -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void write_other(dtnmos_buffer* buffer, const DtNmosFlow* flow)
{
    const DtNmosOtherFormat* other = &flow->Format.Other;
    if (other->Encoding[0] != '\0')
    {
        dtnmos_buffer_printf(buffer, "a=rtpmap:%u %s/%u\r\n", (unsigned)flow->PayloadType,
                             other->Encoding, (unsigned)flow->ClockRate);
    }
    if (other->Fmtp != NULL && other->Fmtp[0] != '\0')
    {
        dtnmos_buffer_printf(buffer, "a=fmtp:%u %s\r\n", (unsigned)flow->PayloadType,
                             other->Fmtp);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_ref_clock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a=ts-refclk; a PTP domain as ST 2110-10 §8.2 writes it, after the grandmaster.
//
static void write_ref_clock(dtnmos_buffer* buffer, const DtNmosRefClock* clock)
{
    switch (clock->Kind)
    {
    case DTNMOS_REFCLOCK_NONE:
        break;
    case DTNMOS_REFCLOCK_PTP:
        if (clock->Traceable)
        {
            dtnmos_buffer_printf(buffer, "a=ts-refclk:ptp=%s:traceable\r\n",
                                 clock->PtpVersion);
        }
        else if (clock->Domain >= 0)
        {
            dtnmos_buffer_printf(buffer, "a=ts-refclk:ptp=%s:%s:%d\r\n",
                                 clock->PtpVersion, clock->Grandmaster, clock->Domain);
        }
        else
        {
            dtnmos_buffer_printf(buffer, "a=ts-refclk:ptp=%s:%s\r\n", clock->PtpVersion,
                                 clock->Grandmaster);
        }
        break;
    case DTNMOS_REFCLOCK_LOCALMAC:
        dtnmos_buffer_printf(buffer, "a=ts-refclk:localmac=%s\r\n", clock->LocalMac);
        break;
    case DTNMOS_REFCLOCK_OTHER:
        if (clock->Text != NULL && clock->Text[0] != '\0')
        {
            dtnmos_buffer_printf(buffer, "a=ts-refclk:%s\r\n", clock->Text);
        }
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void write_flow(dtnmos_buffer* buffer, const DtNmosFlow* flow, size_t index,
                       int with_mid)
{
    const char* destination = flow->DestinationIp;
    dtnmos_buffer_printf(buffer, "m=%s %u RTP/AVP %u\r\n",
                         flow->Media == DTNMOS_MEDIA_AUDIO ? "audio" : "video",
                         (unsigned)flow->DestinationPort, (unsigned)flow->PayloadType);
    dtnmos_buffer_printf(buffer, "c=IN %s %s%s\r\n", address_type(destination),
                         destination, is_ipv4_multicast(destination) ? "/64" : "");
    if (flow->Media == DTNMOS_MEDIA_COMPRESSED_VIDEO &&
        flow->Format.CompressedVideo.BandwidthKbps != 0)
    {
        dtnmos_buffer_printf(
            buffer, "b=AS:%llu\r\n",
            (unsigned long long)flow->Format.CompressedVideo.BandwidthKbps);
    }
    if (flow->SourceIp[0] != '\0')
    {
        dtnmos_buffer_printf(buffer, "a=source-filter: incl IN %s %s %s\r\n",
                             address_type(destination), destination, flow->SourceIp);
    }
    switch (flow->Media)
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
    write_ref_clock(buffer, &flow->RefClock);
    if (flow->MediaClockDirect)
    {
        dtnmos_buffer_printf(buffer, "a=mediaclk:direct=%u\r\n",
                             (unsigned)flow->MediaClockOffset);
    }
    if (with_mid)
    {
        dtnmos_buffer_printf(buffer, "a=mid:%s%zu\r\n",
                             flow->Leg == 1 ? "secondary" : "primary", index);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_sdp_write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult dtnmos_sdp_write(const DtNmosSession* session, const DtNmosFlow* flows,
                              size_t count, dtnmos_buffer* text)
{
    if (session == NULL || (flows == NULL && count > 0) || text == NULL)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosSdp_Write() needs a session, its flows and a buffer.");
    }
    if (count == 0)
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT, "An SDP needs at least one flow.");
    }
    if (session->OriginIp[0] == '\0')
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "An SDP needs the address of its sender in OriginIp.");
    }
    int with_mid = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (flows[i].DestinationIp[0] == '\0' || flows[i].DestinationPort == 0)
        {
            return dtnmos_fail(
                DTNMOS_E_INVALID_ARGUMENT,
                "Flow %zu of the SDP needs a destination address and port.", i);
        }
        if (flows[i].Leg == 1)
        {
            if (i == 0 || flows[i - 1].Leg != 0)
            {
                return dtnmos_fail(
                    DTNMOS_E_INVALID_ARGUMENT,
                    "Flow %zu is a second path and needs the first right before it.", i);
            }
            with_mid = 1;
        }
    }

    dtnmos_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    const char* origin = session->OriginIp;
    DTNMOS_APPEND_LITERAL(&buffer, "v=0\r\n");
    dtnmos_buffer_printf(
        &buffer, "o=- %llu %llu IN %s %s\r\n", (unsigned long long)session->SessionId,
        (unsigned long long)session->SessionVersion, address_type(origin), origin);
    // RFC 8866 asks for a single space when a session has no name.
    dtnmos_buffer_printf(&buffer, "s=%s\r\n",
                         session->Name != NULL && session->Name[0] != '\0' ? session->Name
                                                                           : " ");
    DTNMOS_APPEND_LITERAL(&buffer, "t=0 0\r\n");
    for (size_t i = 0; with_mid && i < count; ++i)
    {
        if (flows[i].Leg == 1)
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
        return dtnmos_fail_memory();
    }
    dtnmos_buffer_free(text);
    *text = buffer;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSdp_Write(const DtNmosSession* session, const DtNmosFlow* flows,
                             size_t count, char* buffer, size_t* size)
{
    if (size == NULL || session == NULL || (flows == NULL && count > 0))
    {
        return dtnmos_fail(DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosSdp_Write() needs a session, its flows and the size of "
                           "the buffer.");
    }
    const DtNmosResult sized =
        DTNMOS_CHECK_SIZE(session, DtNmosSession, sizeof(DtNmosSession));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    for (size_t i = 0; i < count; ++i)
    {
        const DtNmosResult flow_sized =
            DTNMOS_CHECK_SIZE(&flows[i], DtNmosFlow, sizeof(DtNmosFlow));
        if (flow_sized != DTNMOS_OK)
        {
            return flow_sized;
        }
    }
    dtnmos_buffer text;
    memset(&text, 0, sizeof(text));
    DtNmosResult result = dtnmos_sdp_write(session, flows, count, &text);
    if (result == DTNMOS_OK)
    {
        result = dtnmos_copy_text(buffer, size, text.data, text.length);
    }
    dtnmos_buffer_free(&text);
    return result;
}
