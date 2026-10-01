// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSdp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of reading and writing the SDP of ST 2110 flows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_sdp.h"

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "tests.h"

// The example of SMPTE ST 2110-20, with both paths of ST 2022-7.
static const char* const video_sdp =
    "v=0\r\n"
    "o=- 123456 11 IN IP4 192.168.100.2\r\n"
    "s=Example of a SMPTE ST2110-20 signal\r\n"
    "i=this example is for 720p video at 59.94\r\n"
    "t=0 0\r\n"
    "a=recvonly\r\n"
    "a=group:DUP primary secondary\r\n"
    "m=video 50000 RTP/AVP 112\r\n"
    "c=IN IP4 239.100.9.10/32\r\n"
    "a=source-filter: incl IN IP4 239.100.9.10 192.168.100.2\r\n"
    "a=rtpmap:112 raw/90000\r\n"
    "a=fmtp:112 sampling=YCbCr-4:2:2; width=1280; height=720; exactframerate=60000/1001; "
    "depth=10; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN; "
    "\r\n"
    "a=ts-refclk:ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:37\r\n"
    "a=mediaclk:direct=0\r\n"
    "a=mid:primary\r\n"
    "m=video 50020 RTP/AVP 112\r\n"
    "c=IN IP4 239.101.9.10/32\r\n"
    "a=source-filter: incl IN IP4 239.101.9.10 192.168.101.2\r\n"
    "a=rtpmap:112 raw/90000\r\n"
    "a=fmtp:112 sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=25; "
    "depth=10; "
    "TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN; "
    "interlace\r\n"
    "a=ts-refclk:ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:37\r\n"
    "a=mediaclk:direct=0\r\n"
    "a=mid:secondary\r\n";

static const char* const audio_sdp = "v=0\n"
                                     "o=- 1 1 IN IP4 10.0.0.5\n"
                                     "s=8 channels at 125 us\n"
                                     "t=0 0\n"
                                     "m=audio 5004 RTP/AVP 97\n"
                                     "c=IN IP4 239.69.0.1/32\n"
                                     "a=rtpmap:97 L24/48000/8\n"
                                     "a=fmtp:97 channel-order=SMPTE2110.(SGRP,SGRP)\n"
                                     "a=ptime:0.125\n"
                                     "a=ts-refclk:ptp=IEEE1588-2008:traceable\n"
                                     "a=mediaclk:direct=1234\n";

// JPEG XS as RFC 9134 and ST 2110-22 describe it.
static const char* const compressed_sdp =
    "v=0\r\n"
    "o=- 7 7 IN IP4 10.0.0.6\r\n"
    "s=JPEG XS\r\n"
    "t=0 0\r\n"
    "m=video 30000 RTP/AVP 112\r\n"
    "c=IN IP4 239.0.0.1/64\r\n"
    "b=AS:116000\r\n"
    "a=rtpmap:112 jxsv/90000\r\n"
    "a=fmtp:112 packetmode=0;profile=High444.12;level=2k-1;sublevel=Sublev3bpp;"
    "sampling=YCbCr-4:2:2;depth=10;width=1920;height=1080;exactframerate=60000/1001;"
    "colorimetry=BT709;TCS=SDR;RANGE=FULL;SSN=ST2110-22:2019;TP=2110TPNL\r\n";

// Ancillary data as RFC 8331 and ST 2110-40 describe it.
static const char* const anc_sdp =
    "v=0\r\n"
    "o=- 9 9 IN IP4 10.0.0.7\r\n"
    "s=Captions and timecode\r\n"
    "t=0 0\r\n"
    "m=video 50000 RTP/AVP 100\r\n"
    "c=IN IP4 239.100.9.11/32\r\n"
    "a=rtpmap:100 smpte291/90000\r\n"
    "a=fmtp:100 DID_SDID={0x61,0x02};DID_SDID={0x41,0x05};VPID_Code=133;"
    "exactframerate=30000/1001;TM=CTM;SSN=ST2110-40:2023\r\n";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosSdp* parse(const char* text)
{
    DtNmosSdp* sdp = NULL;
    if (DtNmosSdp_Parse(text, strlen(text), &sdp) != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    return sdp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_video_on_two_paths -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_video_on_two_paths(void)
{
    DtNmosSdp* sdp = parse(video_sdp);
    REQUIRE(sdp != NULL);
    const DtNmosSession* session = DtNmosSdp_Session(sdp);
    CHECK_STR(session->Name, "Example of a SMPTE ST2110-20 signal");
    CHECK_STR(session->OriginIp, "192.168.100.2");
    CHECK_EQ(session->SessionId, 123456);
    CHECK_EQ(session->SessionVersion, 11);
    REQUIRE(DtNmosSdp_FlowCount(sdp) == 2);
    CHECK(DtNmosSdp_Flow(sdp, 2) == NULL);

    const DtNmosFlow* primary = DtNmosSdp_Flow(sdp, 0);
    CHECK_EQ(primary->Media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(primary->DestinationIp, "239.100.9.10");
    CHECK_EQ(primary->DestinationPort, 50000);
    CHECK_STR(primary->SourceIp, "192.168.100.2");
    CHECK_EQ(primary->PayloadType, 112);
    CHECK_EQ(primary->ClockRate, 90000);
    // The domain as ST 2110-10 §8.2 writes it, after the grandmaster.
    CHECK_EQ(primary->RefClock.Kind, DTNMOS_REFCLOCK_PTP);
    CHECK_STR(primary->RefClock.PtpVersion, "IEEE1588-2008");
    CHECK_STR(primary->RefClock.Grandmaster, "39-A7-94-FF-FE-07-CB-D0");
    CHECK_EQ(primary->RefClock.Domain, 37);
    CHECK(!primary->RefClock.Traceable);
    CHECK(primary->MediaClockDirect);
    CHECK_EQ(primary->MediaClockOffset, 0);
    CHECK_EQ(primary->Leg, 0);
    const DtNmosVideoFormat* video = &primary->Format.Video;
    CHECK_EQ(video->Width, 1280);
    CHECK_EQ(video->Height, 720);
    CHECK_EQ(video->RateNumerator, 60000);
    CHECK_EQ(video->RateDenominator, 1001);
    CHECK_EQ(video->Depth, 10);
    CHECK(!video->Interlaced);
    CHECK_STR(video->Sampling, "YCbCr-4:2:2");
    CHECK_STR(video->Colorimetry, "BT709");
    CHECK_STR(video->Tcs, "SDR");
    CHECK_STR(video->PackingMode, "2110GPM");
    CHECK_STR(video->Ssn, "ST2110-20:2017");
    CHECK_STR(video->TransmitterType, "2110TPN");

    const DtNmosFlow* secondary = DtNmosSdp_Flow(sdp, 1);
    CHECK_EQ(secondary->Leg, 1);
    CHECK_STR(secondary->DestinationIp, "239.101.9.10");
    CHECK_EQ(secondary->Format.Video.Height, 1080);
    CHECK_EQ(secondary->Format.Video.RateNumerator, 25);
    CHECK_EQ(secondary->Format.Video.RateDenominator, 1);
    CHECK(secondary->Format.Video.Interlaced);
    DtNmosSdp_Free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_reads_audio(void)
{
    DtNmosSdp* sdp = parse(audio_sdp);
    REQUIRE(sdp != NULL);
    REQUIRE(DtNmosSdp_FlowCount(sdp) == 1);
    const DtNmosFlow* flow = DtNmosSdp_Flow(sdp, 0);
    CHECK_EQ(flow->Media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(flow->SourceIp, "");
    CHECK_EQ(flow->MediaClockOffset, 1234);
    const DtNmosAudioFormat* audio = &flow->Format.Audio;
    CHECK_STR(audio->Encoding, "L24");
    CHECK_EQ(audio->SampleRate, 48000);
    CHECK_EQ(audio->Channels, 8);
    CHECK_EQ(audio->PacketTimeNs, 125000);
    CHECK_STR(audio->ChannelOrder, "SMPTE2110.(SGRP,SGRP)");
    DtNmosSdp_Free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_compressed_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_compressed_video(void)
{
    DtNmosSdp* sdp = parse(compressed_sdp);
    REQUIRE(sdp != NULL);
    const DtNmosFlow* flow = DtNmosSdp_Flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_EQ(flow->Media, DTNMOS_MEDIA_COMPRESSED_VIDEO);
    const DtNmosCompressedVideoFormat* video = &flow->Format.CompressedVideo;
    CHECK_STR(video->Encoding, "jxsv");
    CHECK_STR(video->Profile, "High444.12");
    CHECK_STR(video->Level, "2k-1");
    CHECK_STR(video->Sublevel, "Sublev3bpp");
    CHECK_EQ(video->PacketMode, 0);
    CHECK_EQ(video->TransmissionMode, 1);
    CHECK_EQ(video->BandwidthKbps, 116000);
    CHECK_EQ(video->Width, 1920);
    CHECK_EQ(video->RateDenominator, 1001);
    CHECK_STR(video->Range, "FULL");
    CHECK_STR(video->TransmitterType, "2110TPNL");
    DtNmosSdp_Free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_ancillary_data -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_ancillary_data(void)
{
    DtNmosSdp* sdp = parse(anc_sdp);
    REQUIRE(sdp != NULL);
    const DtNmosFlow* flow = DtNmosSdp_Flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_EQ(flow->Media, DTNMOS_MEDIA_ANC);
    const DtNmosAncFormat* anc = &flow->Format.Anc;
    REQUIRE(anc->DidSdidCount == 2);
    CHECK_EQ(anc->DidSdid[0].Did, 0x61);
    CHECK_EQ(anc->DidSdid[0].Sdid, 0x02);
    CHECK_EQ(anc->DidSdid[1].Did, 0x41);
    CHECK_EQ(anc->DidSdid[1].Sdid, 0x05);
    CHECK_EQ(anc->VpidCode, 133);
    CHECK_EQ(anc->RateNumerator, 30000);
    CHECK_STR(anc->TransmissionModel, "CTM");
    CHECK_STR(anc->Ssn, "ST2110-40:2023");
    DtNmosSdp_Free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_other_media_as_they_are -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_reads_other_media_as_they_are(void)
{
    DtNmosSdp* sdp = parse("v=0\r\no=- 1 1 IN IP4 10.0.0.8\r\ns=H.264\r\nt=0 0\r\n"
                           "m=video 5000 RTP/AVP 96\r\nc=IN IP4 239.0.0.9/16\r\n"
                           "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1; "
                           "profile-level-id=42e01f\r\n");
    REQUIRE(sdp != NULL);
    const DtNmosFlow* flow = DtNmosSdp_Flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_EQ(flow->Media, DTNMOS_MEDIA_OTHER);
    CHECK_STR(flow->Format.Other.Encoding, "H264");
    CHECK_STR(flow->Format.Other.Fmtp, "packetization-mode=1; profile-level-id=42e01f");
    DtNmosSdp_Free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_takes_defaults_of_the_session -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_takes_defaults_of_the_session(void)
{
    // c=, a=source-filter, a=ts-refclk and a=mediaclk of the session, IPv6, and a line
    // without its carriage return at the end.
    DtNmosSdp* sdp =
        parse("v=0\no=- 2 3 IN IP6 fd00::5\ns=Defaults\nc=IN IP6 ff15::7\nt=0 0\n"
              "a=source-filter: incl IN IP6 ff15::7 "
              "fd00::5\na=ts-refclk:localmac=CA-FE-01-02-03-04\n"
              "a=mediaclk:direct=5\n"
              "m=audio 5006 RTP/AVP 98\na=rtpmap:98 L16/48000/2\na=ptime:1");
    REQUIRE(sdp != NULL);
    const DtNmosFlow* flow = DtNmosSdp_Flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_STR(flow->DestinationIp, "ff15::7");
    CHECK_STR(flow->SourceIp, "fd00::5");
    CHECK_EQ(flow->RefClock.Kind, DTNMOS_REFCLOCK_LOCALMAC);
    CHECK_STR(flow->RefClock.LocalMac, "CA-FE-01-02-03-04");
    CHECK_EQ(flow->MediaClockOffset, 5);
    CHECK_STR(flow->Format.Audio.Encoding, "L16");
    CHECK_EQ(flow->Format.Audio.PacketTimeNs, 1000000);
    CHECK_STR(DtNmosSdp_Session(sdp)->OriginIp, "fd00::5");
    DtNmosSdp_Free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- check_error -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Parses text, which must fail with code, and checks that the message holds fragment.
//
static void check_error(const char* text, DtNmosResult code, const char* fragment)
{
    DtNmosSdp* sdp = (DtNmosSdp*)&sdp;
    const DtNmosResult result = DtNmosSdp_Parse(text, strlen(text), &sdp);
    CHECK_EQ(result, code);
    CHECK(sdp == NULL);
    if (strstr(DtNmos_GetLastError(), fragment) == NULL)
    {
        printf("  the message \"%s\" lacks \"%s\"\n", DtNmos_GetLastError(), fragment);
        CHECK(strstr(DtNmos_GetLastError(), fragment) != NULL);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_names_the_line_of_an_error -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_names_the_line_of_an_error(void)
{
    const char* head = "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n";
    char text[512];
    snprintf(text, sizeof(text), "%sthis is no line\n", head);
    check_error(text, DTNMOS_E_PARSE, "SDP line 5: a line of an SDP is <type>=<value>");
    snprintf(text, sizeof(text), "%sm=video 5000 RTP/AVP\n", head);
    check_error(text, DTNMOS_E_PARSE, "SDP line 5: m= needs");
    snprintf(text, sizeof(text),
             "%sm=video 5000 RTP/AVP 96\nc=IN IP4 239.0.0.1\n"
             "a=rtpmap:96 raw/90000\na=fmtp:96 width=wide\n",
             head);
    check_error(text, DTNMOS_E_PARSE, "SDP line 8: a=fmtp has a parameter");
    snprintf(text, sizeof(text),
             "%sm=audio 5000 RTP/AVP 97\nc=IN IP4 239.0.0.1\n"
             "a=rtpmap:97 L24/48000/2\na=ptime:1.2345678\n",
             head);
    check_error(text, DTNMOS_E_PARSE, "a=ptime needs milliseconds");
    snprintf(text, sizeof(text), "%sm=audio 5000 RTP/AVP 97\na=rtpmap:97 L24/48000/2\n",
             head);
    check_error(text, DTNMOS_E_PARSE, "SDP line 5: the media section has no c=");
    snprintf(text, sizeof(text), "%sc=IN IP9 239.0.0.1\n", head);
    check_error(text, DTNMOS_E_PARSE, "c= needs IN IP4 or IN IP6");
    check_error(head, DTNMOS_E_INVALID_ARGUMENT, "no media section");
    DtNmosSdp* sdp = NULL;
    CHECK(DtNmosSdp_Parse(NULL, 1, &sdp) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- same -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks that the texts a and b are the same; a null one counts as empty.
//
static int same(const char* a, const char* b)
{
    return strcmp(a == NULL ? "" : a, b == NULL ? "" : b) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- same_clock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether a and b are the same reference clock.
//
static int same_clock(const DtNmosRefClock* a, const DtNmosRefClock* b)
{
    return a->Kind == b->Kind && same(a->PtpVersion, b->PtpVersion) &&
           same(a->Grandmaster, b->Grandmaster) && a->Traceable == b->Traceable &&
           a->Domain == b->Domain && same(a->LocalMac, b->LocalMac) &&
           same(a->Text, b->Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- flows_equal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether flows a and b describe the same flow.
//
static int flows_equal(const DtNmosFlow* a, const DtNmosFlow* b)
{
    if (a->Media != b->Media || !same(a->DestinationIp, b->DestinationIp) ||
        a->DestinationPort != b->DestinationPort || !same(a->SourceIp, b->SourceIp) ||
        a->PayloadType != b->PayloadType || a->ClockRate != b->ClockRate ||
        !same_clock(&a->RefClock, &b->RefClock) ||
        a->MediaClockDirect != b->MediaClockDirect ||
        a->MediaClockOffset != b->MediaClockOffset || a->Leg != b->Leg)
    {
        return 0;
    }
    switch (a->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
    {
        const DtNmosVideoFormat* x = &a->Format.Video;
        const DtNmosVideoFormat* y = &b->Format.Video;
        return x->Width == y->Width && x->Height == y->Height &&
               x->RateNumerator == y->RateNumerator &&
               x->RateDenominator == y->RateDenominator &&
               x->Interlaced == y->Interlaced && x->Segmented == y->Segmented &&
               x->Depth == y->Depth && same(x->Sampling, y->Sampling) &&
               same(x->Colorimetry, y->Colorimetry) && same(x->Tcs, y->Tcs) &&
               same(x->Range, y->Range) && same(x->PackingMode, y->PackingMode) &&
               same(x->Ssn, y->Ssn) && same(x->TransmitterType, y->TransmitterType);
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        const DtNmosAudioFormat* x = &a->Format.Audio;
        const DtNmosAudioFormat* y = &b->Format.Audio;
        return same(x->Encoding, y->Encoding) && x->SampleRate == y->SampleRate &&
               x->Channels == y->Channels && x->PacketTimeNs == y->PacketTimeNs &&
               same(x->ChannelOrder, y->ChannelOrder);
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        const DtNmosCompressedVideoFormat* x = &a->Format.CompressedVideo;
        const DtNmosCompressedVideoFormat* y = &b->Format.CompressedVideo;
        return same(x->Encoding, y->Encoding) && x->Width == y->Width &&
               x->Height == y->Height && x->RateNumerator == y->RateNumerator &&
               x->RateDenominator == y->RateDenominator && x->Depth == y->Depth &&
               same(x->Sampling, y->Sampling) && same(x->Profile, y->Profile) &&
               same(x->Level, y->Level) && same(x->Sublevel, y->Sublevel) &&
               same(x->Range, y->Range) && same(x->Ssn, y->Ssn) &&
               x->PacketMode == y->PacketMode &&
               x->TransmissionMode == y->TransmissionMode &&
               x->BandwidthKbps == y->BandwidthKbps;
    }
    case DTNMOS_MEDIA_ANC:
    {
        const DtNmosAncFormat* x = &a->Format.Anc;
        const DtNmosAncFormat* y = &b->Format.Anc;
        return x->DidSdidCount == y->DidSdidCount &&
               (x->DidSdidCount == 0 ||
                memcmp(x->DidSdid, y->DidSdid, x->DidSdidCount * sizeof(*x->DidSdid)) ==
                    0) &&
               x->VpidCode == y->VpidCode && x->RateNumerator == y->RateNumerator &&
               same(x->TransmissionModel, y->TransmissionModel) && same(x->Ssn, y->Ssn);
    }
    case DTNMOS_MEDIA_OTHER:
        return same(a->Format.Other.Encoding, b->Format.Other.Encoding) &&
               same(a->Format.Other.Fmtp, b->Format.Other.Fmtp);
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_writes_what_it_reads_back -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_writes_what_it_reads_back(void)
{
    const char* const texts[] = {video_sdp, audio_sdp, compressed_sdp, anc_sdp};
    for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t)
    {
        DtNmosSdp* original = parse(texts[t]);
        REQUIRE(original != NULL);
        const size_t count = DtNmosSdp_FlowCount(original);
        DtNmosFlow* flows = calloc(count, sizeof(*flows));
        REQUIRE(flows != NULL);
        for (size_t i = 0; i < count; ++i)
        {
            flows[i] = *DtNmosSdp_Flow(original, i);
        }
        char written[4096];
        size_t size = sizeof(written);
        const DtNmosResult result =
            DtNmosSdp_Write(DtNmosSdp_Session(original), flows, count, written, &size);
        if (result != DTNMOS_OK)
        {
            printf("  %s\n", DtNmos_GetLastError());
        }
        CHECK(result == DTNMOS_OK);
        DtNmosSdp* again = parse(written);
        CHECK(again != NULL);
        if (again != NULL)
        {
            CHECK_EQ(DtNmosSdp_FlowCount(again), count);
            for (size_t i = 0; i < count && i < DtNmosSdp_FlowCount(again); ++i)
            {
                if (!flows_equal(DtNmosSdp_Flow(again, i), &flows[i]))
                {
                    printf("  flow %zu of SDP %zu differs after writing:\n%s\n", i, t,
                           written);
                    CHECK(0);
                }
            }
            CHECK(
                same(DtNmosSdp_Session(again)->Name, DtNmosSdp_Session(original)->Name));
        }
        DtNmosSdp_Free(again);
        free(flows);
        DtNmosSdp_Free(original);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_writes_an_audio_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_writes_an_audio_sender(void)
{
    DtNmosSession session = {0};
    session.Size = sizeof(session);
    session.Name = "dt2110audiosink";
    snprintf(session.OriginIp, sizeof(session.OriginIp), "%s", "192.168.1.10");
    session.SessionId = 42;
    session.SessionVersion = 1;
    DtNmosFlow flow = {0};
    flow.Size = sizeof(flow);
    flow.Media = DTNMOS_MEDIA_AUDIO;
    snprintf(flow.DestinationIp, sizeof(flow.DestinationIp), "%s", "239.0.0.2");
    flow.DestinationPort = 5004;
    flow.PayloadType = 97;
    flow.ClockRate = 48000;
    flow.RefClock.Kind = DTNMOS_REFCLOCK_LOCALMAC;
    snprintf(flow.RefClock.LocalMac, sizeof(flow.RefClock.LocalMac), "%s",
             "00-14-F4-01-02-03");
    flow.MediaClockDirect = 1;
    snprintf(flow.Format.Audio.Encoding, sizeof(flow.Format.Audio.Encoding), "%s", "L24");
    flow.Format.Audio.SampleRate = 48000;
    flow.Format.Audio.Channels = 2;
    flow.Format.Audio.PacketTimeNs = 1000000;
    char text[1024];
    size_t size = sizeof(text);
    REQUIRE(DtNmosSdp_Write(&session, &flow, 1, text, &size) == DTNMOS_OK);
    CHECK_EQ(size, strlen(text));
    CHECK_STR(text, "v=0\r\n"
                    "o=- 42 1 IN IP4 192.168.1.10\r\n"
                    "s=dt2110audiosink\r\n"
                    "t=0 0\r\n"
                    "m=audio 5004 RTP/AVP 97\r\n"
                    "c=IN IP4 239.0.0.2/64\r\n"
                    "a=rtpmap:97 L24/48000/2\r\n"
                    "a=ptime:1\r\n"
                    "a=ts-refclk:localmac=00-14-F4-01-02-03\r\n"
                    "a=mediaclk:direct=0\r\n");
    // A buffer too small for it gets the size it needs.
    size_t small = 10;
    CHECK(DtNmosSdp_Write(&session, &flow, 1, text, &small) == DTNMOS_E_BUFFER_TOO_SMALL);
    CHECK_EQ(small, size + 1);
}

// .-.-.-.-.-.-.-.-.-.-.- sdp_refuses_to_write_an_incomplete_flow -.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_refuses_to_write_an_incomplete_flow(void)
{
    DtNmosSession session = {0};
    session.Size = sizeof(session);
    snprintf(session.OriginIp, sizeof(session.OriginIp), "%s", "10.0.0.1");
    DtNmosFlow flows[2] = {{0}, {0}};
    flows[0].Size = sizeof(flows[0]);
    flows[1].Size = sizeof(flows[1]);
    flows[0].Media = DTNMOS_MEDIA_OTHER;
    char text[2048];
    size_t size = sizeof(text);
    CHECK(DtNmosSdp_Write(&session, flows, 1, text, &size) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "destination") != NULL);
    snprintf(flows[0].DestinationIp, sizeof(flows[0].DestinationIp), "%s", "239.0.0.1");
    flows[0].DestinationPort = 5000;
    flows[1] = flows[0];
    snprintf(flows[1].DestinationIp, sizeof(flows[1].DestinationIp), "%s", "239.0.0.2");
    flows[1].Leg = 1;
    flows[0].Leg = 1;
    size = sizeof(text);
    CHECK(DtNmosSdp_Write(&session, flows, 2, text, &size) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "second path") != NULL);
    flows[0].Leg = 0;
    size = sizeof(text);
    REQUIRE(DtNmosSdp_Write(&session, flows, 2, text, &size) == DTNMOS_OK);
    CHECK(strstr(text, "a=group:DUP primary0 secondary1\r\n") != NULL);
    DtNmosSession empty = {0};
    empty.Size = sizeof(empty);
    size = sizeof(text);
    CHECK(DtNmosSdp_Write(&empty, flows, 1, text, &size) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- flow_is_copied_with_assignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A flow is copied with =. Its fixed arrays are its own and outlive the SDP it came
// from; its pointers, here DidSdid, point into that SDP.
void flow_is_copied_with_assignment(void)
{
    DtNmosSdp* sdp = parse(anc_sdp);
    REQUIRE(sdp != NULL);
    const DtNmosFlow copy = *DtNmosSdp_Flow(sdp, 0);
    CHECK(copy.Format.Anc.DidSdid == DtNmosSdp_Flow(sdp, 0)->Format.Anc.DidSdid);
    CHECK_EQ(copy.Format.Anc.DidSdidCount, 2);
    CHECK_EQ(copy.Format.Anc.DidSdid[1].Did, 0x41);
    DtNmosSdp_Free(sdp);
    CHECK_STR(copy.Format.Anc.Ssn, "ST2110-40:2023");
    CHECK_STR(copy.DestinationIp, "239.100.9.11");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_the_forms_of_ts_refclk -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The forms of a=ts-refclk: a PTP domain of RFC 7273 and of ST 2110-10, a traceable PTP
// clock, a local MAC, and another kind, which keeps its text.
void sdp_reads_the_forms_of_ts_refclk(void)
{
    static const char* const values[] = {
        "ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:domain-nmbr=127",
        "ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0",
        "ptp=IEEE802.1AS-2011:traceable",
        "localmac=CA-FE-01-02-03-04",
        "ntp=203.0.113.10",
    };
    char text[512];
    DtNmosRefClock clocks[5];
    for (size_t i = 0; i < 5; ++i)
    {
        snprintf(text, sizeof(text),
                 "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n"
                 "m=audio 5004 RTP/AVP 97\nc=IN IP4 239.0.0.1/64\n"
                 "a=rtpmap:97 L24/48000/2\na=ts-refclk:%s\n",
                 values[i]);
        DtNmosSdp* sdp = parse(text);
        REQUIRE(sdp != NULL);
        clocks[i] = DtNmosSdp_Flow(sdp, 0)->RefClock;
        if (i == 4)
        {
            CHECK_STR(clocks[i].Text, "ntp=203.0.113.10");
        }
        DtNmosSdp_Free(sdp);
    }
    CHECK_EQ(clocks[0].Kind, DTNMOS_REFCLOCK_PTP);
    CHECK_EQ(clocks[0].Domain, 127);
    CHECK_EQ(clocks[1].Kind, DTNMOS_REFCLOCK_PTP);
    CHECK_EQ(clocks[1].Domain, -1);
    CHECK_STR(clocks[1].Grandmaster, "39-A7-94-FF-FE-07-CB-D0");
    CHECK_EQ(clocks[2].Kind, DTNMOS_REFCLOCK_PTP);
    CHECK(clocks[2].Traceable);
    CHECK_STR(clocks[2].PtpVersion, "IEEE802.1AS-2011");
    CHECK_STR(clocks[2].Grandmaster, "");
    CHECK_EQ(clocks[3].Kind, DTNMOS_REFCLOCK_LOCALMAC);
    CHECK_EQ(clocks[4].Kind, DTNMOS_REFCLOCK_OTHER);
}

// .-.-.-.-.-.-.-.-.-.-.- sdp_refuses_a_value_longer_than_its_field -.-.-.-.-.-.-.-.-.-.-.
//
// A value of a=fmtp longer than its field, which no standard allows, fails the parse and
// names the line and the parameter.
void sdp_refuses_a_value_longer_than_its_field(void)
{
    check_error(
        "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n"
        "m=video 5000 RTP/AVP 96\nc=IN IP4 239.0.0.1/64\n"
        "a=rtpmap:96 raw/90000\n"
        "a=fmtp:96 sampling=YCbCr-4:2:2-and-far-more-than-it-may-be\n",
        DTNMOS_E_PARSE,
        "SDP line 8: a=fmtp has a value longer than its standard allows: 'sampling'");
}
