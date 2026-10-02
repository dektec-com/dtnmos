// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosSdpWrite.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Writing an SDP (RFC 8866) as SMPTE ST 2110 describes a sender
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <string.h>

#include "NmosFlow.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsIpv6 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether address is IPv6, which holds a colon where IPv4 has none.
//
static bool IsIpv6(const char* Address)
{
    return strchr(Address, ':') != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsIpv4Multicast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether address is an IPv4 multicast address, 224.0.0.0 to 239.255.255.255.
//
static bool IsIpv4Multicast(const char* Address)
{
    NmosSpan First;
    NmosSpan_Split(NmosSpan_Of(Address), '.', &First);
    uint32_t Octet = 0;
    return NmosText_ParseU32(First, 255, &Octet) && Octet >= 224 && Octet <= 239;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddressType -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const char* AddressType(const char* Address)
{
    return IsIpv6(Address) ? "IP6" : "IP4";
}

// Appends "; " between the parameters of an a=fmtp, and the payload type before the
// first.
typedef struct NmosFmtpWriter
{
    NmosBuffer* Buffer;
    uint8_t PayloadType;
    int Count;
} NmosFmtpWriter;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Separate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Separate(NmosFmtpWriter* Writer)
{
    if (Writer->Count++ == 0)
    {
        NmosBuffer_Printf(Writer->Buffer, "a=fmtp:%u ", (unsigned)Writer->PayloadType);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(Writer->Buffer, "; ");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes name=value; nothing when value is null or empty.
//
static void WriteText(NmosFmtpWriter* Writer, const char* Name, const char* Value)
{
    if (Value == NULL || Value[0] == '\0')
    {
        return;
    }
    Separate(Writer);
    NmosBuffer_Printf(Writer->Buffer, "%s=%s", Name, Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WriteNumber(NmosFmtpWriter* Writer, const char* Name, uint32_t Value)
{
    if (Value == 0)
    {
        return;
    }
    Separate(Writer);
    NmosBuffer_Printf(Writer->Buffer, "%s=%u", Name, (unsigned)Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WriteRate(NmosFmtpWriter* Writer, uint32_t Numerator, uint32_t Denominator)
{
    if (Numerator == 0)
    {
        return;
    }
    Separate(Writer);
    if (Denominator <= 1)
    {
        NmosBuffer_Printf(Writer->Buffer, "exactframerate=%u", (unsigned)Numerator);
    }
    else
    {
        NmosBuffer_Printf(Writer->Buffer, "exactframerate=%u/%u", (unsigned)Numerator,
                          (unsigned)Denominator);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFlag -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WriteFlag(NmosFmtpWriter* Writer, const char* Name, bool Set)
{
    if (!Set)
    {
        return;
    }
    Separate(Writer);
    NmosBuffer_Append(Writer->Buffer, Name, strlen(Name));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EndFmtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EndFmtp(NmosFmtpWriter* Writer)
{
    if (Writer->Count > 0)
    {
        DTNMOS_APPEND_LITERAL(Writer->Buffer, "\r\n");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteOthers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the parameters dtnmos does not know as they are, after those it knows.
//
static void WriteOthers(NmosFmtpWriter* Writer, const char* Others)
{
    if (Others == NULL || Others[0] == '\0')
    {
        return;
    }
    Separate(Writer);
    NmosBuffer_Append(Writer->Buffer, Others, strlen(Others));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WriteVideo(NmosBuffer* Buffer, const DtNmosFlow* Flow)
{
    const DtNmosVideoFormat* Video = &Flow->Format.Video;
    NmosBuffer_Printf(Buffer, "a=rtpmap:%u raw/%u\r\n", (unsigned)Flow->PayloadType,
                      (unsigned)Flow->ClockRate);
    NmosFmtpWriter Writer = {Buffer, Flow->PayloadType, 0};
    WriteText(&Writer, "sampling", DtNmosSampling_Text(Video->Sampling));
    WriteNumber(&Writer, "width", Video->Width);
    WriteNumber(&Writer, "height", Video->Height);
    WriteRate(&Writer, Video->RateNumerator, Video->RateDenominator);
    WriteNumber(&Writer, "depth", Video->Depth);
    WriteText(&Writer, "TCS", DtNmosTcs_Text(Video->Tcs));
    WriteText(&Writer, "colorimetry", DtNmosColorimetry_Text(Video->Colorimetry));
    WriteText(&Writer, "RANGE", DtNmosRange_Text(Video->Range));
    WriteText(&Writer, "PM", DtNmosPackingMode_Text(Video->PackingMode));
    WriteText(&Writer, "SSN", Video->Ssn);
    WriteText(&Writer, "TP", DtNmosTransmitterType_Text(Video->TransmitterType));
    WriteFlag(&Writer, "interlace", Video->Interlaced);
    WriteFlag(&Writer, "segmented", Video->Segmented);
    WriteOthers(&Writer, Video->OtherParameters);
    EndFmtp(&Writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteCompressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WriteCompressed(NmosBuffer* Buffer, const DtNmosFlow* Flow)
{
    const DtNmosCompressedVideoFormat* Video = &Flow->Format.CompressedVideo;
    NmosBuffer_Printf(Buffer, "a=rtpmap:%u %s/%u\r\n", (unsigned)Flow->PayloadType,
                      Video->Encoding, (unsigned)Flow->ClockRate);
    NmosFmtpWriter Writer = {Buffer, Flow->PayloadType, 0};
    Separate(&Writer);
    NmosBuffer_Printf(Buffer, "packetmode=%u", (unsigned)Video->PacketMode);
    WriteText(&Writer, "profile", Video->Profile);
    WriteText(&Writer, "level", Video->Level);
    WriteText(&Writer, "sublevel", Video->Sublevel);
    WriteText(&Writer, "sampling", DtNmosSampling_Text(Video->Sampling));
    WriteNumber(&Writer, "width", Video->Width);
    WriteNumber(&Writer, "height", Video->Height);
    WriteRate(&Writer, Video->RateNumerator, Video->RateDenominator);
    WriteNumber(&Writer, "depth", Video->Depth);
    WriteText(&Writer, "TCS", DtNmosTcs_Text(Video->Tcs));
    WriteText(&Writer, "colorimetry", DtNmosColorimetry_Text(Video->Colorimetry));
    WriteText(&Writer, "RANGE", DtNmosRange_Text(Video->Range));
    WriteText(&Writer, "SSN", Video->Ssn);
    WriteText(&Writer, "TP", DtNmosTransmitterType_Text(Video->TransmitterType));
    if (Video->TransmissionMode != 1)
    {
        Separate(&Writer);
        NmosBuffer_Printf(Buffer, "transmode=%u", (unsigned)Video->TransmissionMode);
    }
    WriteFlag(&Writer, "interlace", Video->Interlaced);
    WriteFlag(&Writer, "segmented", Video->Segmented);
    WriteOthers(&Writer, Video->OtherParameters);
    EndFmtp(&Writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteMilliseconds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a time in nanoseconds as milliseconds, "1" or "0.125".
//
static void WriteMilliseconds(NmosBuffer* Buffer, uint32_t Nanoseconds)
{
    const unsigned Whole = (unsigned)(Nanoseconds / 1000000u);
    unsigned Fraction = (unsigned)(Nanoseconds % 1000000u);
    if (Fraction == 0)
    {
        NmosBuffer_Printf(Buffer, "%u", Whole);
        return;
    }
    int Digits = 6;
    while (Fraction % 10 == 0)
    {
        Fraction /= 10;
        --Digits;
    }
    NmosBuffer_Printf(Buffer, "%u.%0*u", Whole, Digits, Fraction);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WriteAudio(NmosBuffer* Buffer, const DtNmosFlow* Flow)
{
    const DtNmosAudioFormat* Audio = &Flow->Format.Audio;
    NmosBuffer_Printf(Buffer, "a=rtpmap:%u %s/%u/%u\r\n", (unsigned)Flow->PayloadType,
                      DtNmosAudioEncoding_Text(Audio->Encoding),
                      (unsigned)Audio->SampleRate, (unsigned)Audio->Channels);
    NmosFmtpWriter Writer = {Buffer, Flow->PayloadType, 0};
    WriteText(&Writer, "channel-order", Audio->ChannelOrder);
    WriteOthers(&Writer, Audio->OtherParameters);
    EndFmtp(&Writer);
    if (Audio->PacketTimeNs != 0)
    {
        DTNMOS_APPEND_LITERAL(Buffer, "a=ptime:");
        WriteMilliseconds(Buffer, Audio->PacketTimeNs);
        DTNMOS_APPEND_LITERAL(Buffer, "\r\n");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteAnc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WriteAnc(NmosBuffer* Buffer, const DtNmosFlow* Flow)
{
    const DtNmosAncFormat* Anc = &Flow->Format.Anc;
    NmosBuffer_Printf(Buffer, "a=rtpmap:%u smpte291/%u\r\n", (unsigned)Flow->PayloadType,
                      (unsigned)Flow->ClockRate);
    NmosFmtpWriter Writer = {Buffer, Flow->PayloadType, 0};
    for (size_t i = 0; i < Anc->DidSdidCount; ++i)
    {
        Separate(&Writer);
        NmosBuffer_Printf(Buffer, "DID_SDID={0x%02X,0x%02X}",
                          (unsigned)Anc->DidSdid[i].Did, (unsigned)Anc->DidSdid[i].Sdid);
    }
    WriteNumber(&Writer, "VPID_Code", Anc->VpidCode);
    WriteRate(&Writer, Anc->RateNumerator, Anc->RateDenominator);
    WriteText(&Writer, "TM", Anc->TransmissionModel);
    WriteText(&Writer, "SSN", Anc->Ssn);
    EndFmtp(&Writer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteOther -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WriteOther(NmosBuffer* Buffer, const DtNmosFlow* Flow)
{
    const DtNmosOtherFormat* Other = &Flow->Format.Other;
    if (Other->Encoding[0] != '\0')
    {
        NmosBuffer_Printf(Buffer, "a=rtpmap:%u %s/%u\r\n", (unsigned)Flow->PayloadType,
                          Other->Encoding, (unsigned)Flow->ClockRate);
    }
    if (Other->Fmtp != NULL && Other->Fmtp[0] != '\0')
    {
        NmosBuffer_Printf(Buffer, "a=fmtp:%u %s\r\n", (unsigned)Flow->PayloadType,
                          Other->Fmtp);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteRefClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a=ts-refclk; a PTP domain as ST 2110-10 §8.2 writes it, after the grandmaster.
//
static void WriteRefClock(NmosBuffer* Buffer, const DtNmosRefClock* Clock)
{
    switch (Clock->Kind)
    {
    case DTNMOS_REFCLOCK_NONE:
        break;
    case DTNMOS_REFCLOCK_PTP:
        if (Clock->Traceable)
        {
            NmosBuffer_Printf(Buffer, "a=ts-refclk:ptp=%s:traceable\r\n",
                              Clock->PtpVersion);
        }
        else if (Clock->Domain >= 0)
        {
            NmosBuffer_Printf(Buffer, "a=ts-refclk:ptp=%s:%s:%d\r\n", Clock->PtpVersion,
                              Clock->Grandmaster, Clock->Domain);
        }
        else
        {
            NmosBuffer_Printf(Buffer, "a=ts-refclk:ptp=%s:%s\r\n", Clock->PtpVersion,
                              Clock->Grandmaster);
        }
        break;
    case DTNMOS_REFCLOCK_LOCALMAC:
        NmosBuffer_Printf(Buffer, "a=ts-refclk:localmac=%s\r\n", Clock->LocalMac);
        break;
    case DTNMOS_REFCLOCK_OTHER:
        if (Clock->Text != NULL && Clock->Text[0] != '\0')
        {
            NmosBuffer_Printf(Buffer, "a=ts-refclk:%s\r\n", Clock->Text);
        }
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PairName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the a=mid of a path of pair Pair, from 1: "primary" and "secondary" for the
// first pair, the names an SDP of ST 2022-7 commonly has, and with the number of the
// pair after them for the next, "primary2", so that each is unique.
//
static void PairName(char* Name, size_t Size, const char* Path, size_t Pair)
{
    if (Pair == 1)
    {
        snprintf(Name, Size, "%s", Path);
    }
    else
    {
        snprintf(Name, Size, "%s%zu", Path, Pair);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Pair is the number of the pair of ST 2022-7 the flow is a path of, from 1, or 0 for a
// flow of one path, which has no a=mid.
//
static void WriteFlow(NmosBuffer* Buffer, const DtNmosFlow* Flow, size_t Pair)
{
    const char* Destination = Flow->DestinationIp;
    NmosBuffer_Printf(Buffer, "m=%s %u RTP/AVP %u\r\n",
                      Flow->Media == DTNMOS_MEDIA_AUDIO ? "audio" : "video",
                      (unsigned)Flow->DestinationPort, (unsigned)Flow->PayloadType);
    NmosBuffer_Printf(Buffer, "c=IN %s %s%s\r\n", AddressType(Destination), Destination,
                      IsIpv4Multicast(Destination) ? "/64" : "");
    if (Flow->Media == DTNMOS_MEDIA_COMPRESSED_VIDEO &&
        Flow->Format.CompressedVideo.BandwidthKbps != 0)
    {
        NmosBuffer_Printf(Buffer, "b=AS:%llu\r\n",
                          (unsigned long long)Flow->Format.CompressedVideo.BandwidthKbps);
    }
    if (Flow->SourceIp[0] != '\0')
    {
        NmosBuffer_Printf(Buffer, "a=source-filter: incl IN %s %s %s\r\n",
                          AddressType(Destination), Destination, Flow->SourceIp);
    }
    switch (Flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        WriteVideo(Buffer, Flow);
        break;
    case DTNMOS_MEDIA_AUDIO:
        WriteAudio(Buffer, Flow);
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        WriteCompressed(Buffer, Flow);
        break;
    case DTNMOS_MEDIA_ANC:
        WriteAnc(Buffer, Flow);
        break;
    case DTNMOS_MEDIA_OTHER:
        WriteOther(Buffer, Flow);
        break;
    }
    WriteRefClock(Buffer, &Flow->RefClock);
    if (Flow->MediaClockDirect)
    {
        NmosBuffer_Printf(Buffer, "a=mediaclk:direct=%u\r\n",
                          (unsigned)Flow->MediaClockOffset);
    }
    if (Pair != 0)
    {
        char Name[32];
        PairName(Name, sizeof(Name), Flow->Leg == 1 ? "secondary" : "primary", Pair);
        NmosBuffer_Printf(Buffer, "a=mid:%s\r\n", Name);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosSdp_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosSdp_Write(const DtNmosSession* Session, const DtNmosFlow* Flows,
                           size_t Count, NmosBuffer* Text)
{
    if (Session == NULL || (Flows == NULL && Count > 0) || Text == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosSdp_Write() needs a session, its flows and a buffer.");
    }
    if (Count == 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "An SDP needs at least one flow.");
    }
    if (Session->OriginIp[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "An SDP needs the address of its sender in OriginIp.");
    }
    for (size_t i = 0; i < Count; ++i)
    {
        if (Flows[i].DestinationIp[0] == '\0' || Flows[i].DestinationPort == 0)
        {
            return NmosError_Fail(
                DTNMOS_E_INVALID_ARGUMENT,
                "Flow %zu of the SDP needs a destination address and port.", i);
        }
        if (Flows[i].Media == DTNMOS_MEDIA_AUDIO &&
            DtNmosAudioEncoding_Text(Flows[i].Format.Audio.Encoding)[0] == '\0')
        {
            return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                                  "Flow %zu is audio and needs L16, L24 or AM824.", i);
        }
        if (Flows[i].Leg == 1)
        {
            if (i == 0 || Flows[i - 1].Leg != 0)
            {
                return NmosError_Fail(
                    DTNMOS_E_INVALID_ARGUMENT,
                    "Flow %zu is a second path and needs the first right before it.", i);
            }
        }
    }

    NmosBuffer Buffer;
    memset(&Buffer, 0, sizeof(Buffer));
    const char* Origin = Session->OriginIp;
    DTNMOS_APPEND_LITERAL(&Buffer, "v=0\r\n");
    NmosBuffer_Printf(
        &Buffer, "o=- %llu %llu IN %s %s\r\n", (unsigned long long)Session->SessionId,
        (unsigned long long)Session->SessionVersion, AddressType(Origin), Origin);
    // RFC 8866 asks for a single space when a session has no name.
    NmosBuffer_Printf(&Buffer, "s=%s\r\n",
                      Session->Name != NULL && Session->Name[0] != '\0' ? Session->Name
                                                                        : " ");
    DTNMOS_APPEND_LITERAL(&Buffer, "t=0 0\r\n");
    // A flow of leg 1 and the flow before it are a pair, numbered from 1 in their order.
    size_t Pairs = 0;
    for (size_t i = 0; i < Count; ++i)
    {
        if (Flows[i].Leg == 1)
        {
            ++Pairs;
            char Primary[32];
            char Secondary[32];
            PairName(Primary, sizeof(Primary), "primary", Pairs);
            PairName(Secondary, sizeof(Secondary), "secondary", Pairs);
            NmosBuffer_Printf(&Buffer, "a=group:DUP %s %s\r\n", Primary, Secondary);
        }
    }
    size_t Pair = 0;
    for (size_t i = 0; i < Count; ++i)
    {
        const bool Paired = i + 1 < Count && Flows[i + 1].Leg == 1;
        if (Paired)
        {
            ++Pair;
        }
        WriteFlow(&Buffer, &Flows[i], Paired || Flows[i].Leg == 1 ? Pair : 0);
    }
    if (Buffer.Failed)
    {
        NmosBuffer_Free(&Buffer);
        return NmosError_FailMemory();
    }
    NmosBuffer_Free(Text);
    *Text = Buffer;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSdp_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosSdp_Write(const DtNmosSession* Session, const DtNmosFlow* Flows,
                             size_t Count, char* Buffer, size_t* Size)
{
    if (Size == NULL || Session == NULL || (Flows == NULL && Count > 0))
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosSdp_Write() needs a session, its flows and the size of "
            "the buffer.");
    }
    const DtNmosResult Sized =
        DTNMOS_CHECK_SIZE(Session, DtNmosSession, sizeof(DtNmosSession));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    for (size_t i = 0; i < Count; ++i)
    {
        const DtNmosResult FlowSized =
            DTNMOS_CHECK_SIZE(&Flows[i], DtNmosFlow, sizeof(DtNmosFlow));
        if (FlowSized != DTNMOS_OK)
        {
            return FlowSized;
        }
    }
    NmosBuffer Text;
    memset(&Text, 0, sizeof(Text));
    DtNmosResult Result = NmosSdp_Write(Session, Flows, Count, &Text);
    if (Result == DTNMOS_OK)
    {
        Result = NmosText_CopyText(Buffer, Size, Text.Data, Text.Length);
    }
    NmosBuffer_Free(&Text);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosVideoFormat_SetDefaults -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosVideoFormat_SetDefaults(DtNmosVideoFormat* Format)
{
    if (Format == NULL)
    {
        return;
    }
    if (Format->Colorimetry == DTNMOS_COLORIMETRY_NONE && Format->Height != 0)
    {
        Format->Colorimetry = Format->Height <= 576    ? DTNMOS_COLORIMETRY_BT601
                              : Format->Height <= 1080 ? DTNMOS_COLORIMETRY_BT709
                                                       : DTNMOS_COLORIMETRY_BT2020;
    }
    if (Format->Tcs == DTNMOS_TCS_NONE)
    {
        Format->Tcs = DTNMOS_TCS_SDR;
    }
}
