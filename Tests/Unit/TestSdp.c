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
static const char* const VideoSdp =
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

static const char* const AudioSdp = "v=0\n"
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
static const char* const CompressedSdp =
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
static const char* const AncSdp =
    "v=0\r\n"
    "o=- 9 9 IN IP4 10.0.0.7\r\n"
    "s=Captions and timecode\r\n"
    "t=0 0\r\n"
    "m=video 50000 RTP/AVP 100\r\n"
    "c=IN IP4 239.100.9.11/32\r\n"
    "a=rtpmap:100 smpte291/90000\r\n"
    "a=fmtp:100 DID_SDID={0x61,0x02};DID_SDID={0x41,0x05};VPID_Code=133;"
    "exactframerate=30000/1001;TM=CTM;SSN=ST2110-40:2023\r\n";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosSdp* Parse(const char* Text)
{
    DtNmosSdp* Sdp = NULL;
    if (DtNmosSdp_Parse(Text, strlen(Text), &Sdp) != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    return Sdp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_video_on_two_paths -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_video_on_two_paths(void)
{
    DtNmosSdp* Sdp = Parse(VideoSdp);
    REQUIRE(Sdp != NULL);
    const DtNmosSession* Session = DtNmosSdp_Session(Sdp);
    CHECK_STR(Session->Name, "Example of a SMPTE ST2110-20 signal");
    CHECK_STR(Session->OriginIp, "192.168.100.2");
    CHECK_EQ(Session->SessionId, 123456);
    CHECK_EQ(Session->SessionVersion, 11);
    REQUIRE(DtNmosSdp_FlowCount(Sdp) == 2);
    CHECK(DtNmosSdp_Flow(Sdp, 2) == NULL);

    const DtNmosFlow* Primary = DtNmosSdp_Flow(Sdp, 0);
    CHECK_EQ(Primary->Media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(Primary->DestinationIp, "239.100.9.10");
    CHECK_EQ(Primary->DestinationPort, 50000);
    CHECK_STR(Primary->SourceIp, "192.168.100.2");
    CHECK_EQ(Primary->PayloadType, 112);
    CHECK_EQ(Primary->ClockRate, 90000);
    // The domain as ST 2110-10 §8.2 writes it, after the grandmaster.
    CHECK_EQ(Primary->RefClock.Kind, DTNMOS_REFCLOCK_PTP);
    CHECK_STR(Primary->RefClock.PtpVersion, "IEEE1588-2008");
    CHECK_STR(Primary->RefClock.Grandmaster, "39-A7-94-FF-FE-07-CB-D0");
    CHECK_EQ(Primary->RefClock.Domain, 37);
    CHECK(!Primary->RefClock.Traceable);
    CHECK(Primary->MediaClockDirect);
    CHECK_EQ(Primary->MediaClockOffset, 0);
    CHECK_EQ(Primary->Leg, 0);
    const DtNmosVideoFormat* Video = &Primary->Format.Video;
    CHECK_EQ(Video->Width, 1280);
    CHECK_EQ(Video->Height, 720);
    CHECK_EQ(Video->RateNumerator, 60000);
    CHECK_EQ(Video->RateDenominator, 1001);
    CHECK_EQ(Video->Depth, 10);
    CHECK(!Video->Interlaced);
    CHECK_STR(Video->Sampling, "YCbCr-4:2:2");
    CHECK_STR(Video->Colorimetry, "BT709");
    CHECK_STR(Video->Tcs, "SDR");
    CHECK_STR(Video->PackingMode, "2110GPM");
    CHECK_STR(Video->Ssn, "ST2110-20:2017");
    CHECK_STR(Video->TransmitterType, "2110TPN");

    const DtNmosFlow* Secondary = DtNmosSdp_Flow(Sdp, 1);
    CHECK_EQ(Secondary->Leg, 1);
    CHECK_STR(Secondary->DestinationIp, "239.101.9.10");
    CHECK_EQ(Secondary->Format.Video.Height, 1080);
    CHECK_EQ(Secondary->Format.Video.RateNumerator, 25);
    CHECK_EQ(Secondary->Format.Video.RateDenominator, 1);
    CHECK(Secondary->Format.Video.Interlaced);
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_reads_audio(void)
{
    DtNmosSdp* Sdp = Parse(AudioSdp);
    REQUIRE(Sdp != NULL);
    REQUIRE(DtNmosSdp_FlowCount(Sdp) == 1);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    CHECK_EQ(Flow->Media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(Flow->SourceIp, "");
    CHECK_EQ(Flow->MediaClockOffset, 1234);
    const DtNmosAudioFormat* Audio = &Flow->Format.Audio;
    CHECK_STR(Audio->Encoding, "L24");
    CHECK_EQ(Audio->SampleRate, 48000);
    CHECK_EQ(Audio->Channels, 8);
    CHECK_EQ(Audio->PacketTimeNs, 125000);
    CHECK_STR(Audio->ChannelOrder, "SMPTE2110.(SGRP,SGRP)");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_compressed_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_compressed_video(void)
{
    DtNmosSdp* Sdp = Parse(CompressedSdp);
    REQUIRE(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    REQUIRE(Flow != NULL);
    CHECK_EQ(Flow->Media, DTNMOS_MEDIA_COMPRESSED_VIDEO);
    const DtNmosCompressedVideoFormat* Video = &Flow->Format.CompressedVideo;
    CHECK_STR(Video->Encoding, "jxsv");
    CHECK_STR(Video->Profile, "High444.12");
    CHECK_STR(Video->Level, "2k-1");
    CHECK_STR(Video->Sublevel, "Sublev3bpp");
    CHECK_EQ(Video->PacketMode, 0);
    CHECK_EQ(Video->TransmissionMode, 1);
    CHECK_EQ(Video->BandwidthKbps, 116000);
    CHECK_EQ(Video->Width, 1920);
    CHECK_EQ(Video->RateDenominator, 1001);
    CHECK_STR(Video->Range, "FULL");
    CHECK_STR(Video->TransmitterType, "2110TPNL");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_ancillary_data -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_ancillary_data(void)
{
    DtNmosSdp* Sdp = Parse(AncSdp);
    REQUIRE(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    REQUIRE(Flow != NULL);
    CHECK_EQ(Flow->Media, DTNMOS_MEDIA_ANC);
    const DtNmosAncFormat* Anc = &Flow->Format.Anc;
    REQUIRE(Anc->DidSdidCount == 2);
    CHECK_EQ(Anc->DidSdid[0].Did, 0x61);
    CHECK_EQ(Anc->DidSdid[0].Sdid, 0x02);
    CHECK_EQ(Anc->DidSdid[1].Did, 0x41);
    CHECK_EQ(Anc->DidSdid[1].Sdid, 0x05);
    CHECK_EQ(Anc->VpidCode, 133);
    CHECK_EQ(Anc->RateNumerator, 30000);
    CHECK_STR(Anc->TransmissionModel, "CTM");
    CHECK_STR(Anc->Ssn, "ST2110-40:2023");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_other_media_as_they_are -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_reads_other_media_as_they_are(void)
{
    DtNmosSdp* Sdp = Parse("v=0\r\no=- 1 1 IN IP4 10.0.0.8\r\ns=H.264\r\nt=0 0\r\n"
                           "m=video 5000 RTP/AVP 96\r\nc=IN IP4 239.0.0.9/16\r\n"
                           "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1; "
                           "profile-level-id=42e01f\r\n");
    REQUIRE(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    REQUIRE(Flow != NULL);
    CHECK_EQ(Flow->Media, DTNMOS_MEDIA_OTHER);
    CHECK_STR(Flow->Format.Other.Encoding, "H264");
    CHECK_STR(Flow->Format.Other.Fmtp, "packetization-mode=1; profile-level-id=42e01f");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_takes_defaults_of_the_session -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_takes_defaults_of_the_session(void)
{
    // c=, a=source-filter, a=ts-refclk and a=mediaclk of the session, IPv6, and a line
    // without its carriage return at the end.
    DtNmosSdp* Sdp =
        Parse("v=0\no=- 2 3 IN IP6 fd00::5\ns=Defaults\nc=IN IP6 ff15::7\nt=0 0\n"
              "a=source-filter: incl IN IP6 ff15::7 "
              "fd00::5\na=ts-refclk:localmac=CA-FE-01-02-03-04\n"
              "a=mediaclk:direct=5\n"
              "m=audio 5006 RTP/AVP 98\na=rtpmap:98 L16/48000/2\na=ptime:1");
    REQUIRE(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    REQUIRE(Flow != NULL);
    CHECK_STR(Flow->DestinationIp, "ff15::7");
    CHECK_STR(Flow->SourceIp, "fd00::5");
    CHECK_EQ(Flow->RefClock.Kind, DTNMOS_REFCLOCK_LOCALMAC);
    CHECK_STR(Flow->RefClock.LocalMac, "CA-FE-01-02-03-04");
    CHECK_EQ(Flow->MediaClockOffset, 5);
    CHECK_STR(Flow->Format.Audio.Encoding, "L16");
    CHECK_EQ(Flow->Format.Audio.PacketTimeNs, 1000000);
    CHECK_STR(DtNmosSdp_Session(Sdp)->OriginIp, "fd00::5");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Parses text, which must fail with code, and checks that the message holds fragment.
//
static void CheckError(const char* Text, DtNmosResult Code, const char* Fragment)
{
    DtNmosSdp* Sdp = (DtNmosSdp*)&Sdp;
    const DtNmosResult Result = DtNmosSdp_Parse(Text, strlen(Text), &Sdp);
    CHECK_EQ(Result, Code);
    CHECK(Sdp == NULL);
    if (strstr(DtNmos_GetLastError(), Fragment) == NULL)
    {
        printf("  the message \"%s\" lacks \"%s\"\n", DtNmos_GetLastError(), Fragment);
        CHECK(strstr(DtNmos_GetLastError(), Fragment) != NULL);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_names_the_line_of_an_error -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_names_the_line_of_an_error(void)
{
    const char* Head = "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n";
    char Text[512];
    snprintf(Text, sizeof(Text), "%sthis is no line\n", Head);
    CheckError(Text, DTNMOS_E_PARSE, "SDP line 5: a line of an SDP is <type>=<value>");
    snprintf(Text, sizeof(Text), "%sm=video 5000 RTP/AVP\n", Head);
    CheckError(Text, DTNMOS_E_PARSE, "SDP line 5: m= needs");
    snprintf(Text, sizeof(Text),
             "%sm=video 5000 RTP/AVP 96\nc=IN IP4 239.0.0.1\n"
             "a=rtpmap:96 raw/90000\na=fmtp:96 width=wide\n",
             Head);
    CheckError(Text, DTNMOS_E_PARSE, "SDP line 8: a=fmtp has a parameter");
    snprintf(Text, sizeof(Text),
             "%sm=audio 5000 RTP/AVP 97\nc=IN IP4 239.0.0.1\n"
             "a=rtpmap:97 L24/48000/2\na=ptime:1.2345678\n",
             Head);
    CheckError(Text, DTNMOS_E_PARSE, "a=ptime needs milliseconds");
    snprintf(Text, sizeof(Text), "%sm=audio 5000 RTP/AVP 97\na=rtpmap:97 L24/48000/2\n",
             Head);
    CheckError(Text, DTNMOS_E_PARSE, "SDP line 5: the media section has no c=");
    snprintf(Text, sizeof(Text), "%sc=IN IP9 239.0.0.1\n", Head);
    CheckError(Text, DTNMOS_E_PARSE, "c= needs IN IP4 or IN IP6");
    CheckError(Head, DTNMOS_E_INVALID_ARGUMENT, "no media section");
    DtNmosSdp* Sdp = NULL;
    CHECK(DtNmosSdp_Parse(NULL, 1, &Sdp) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Same -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks that the texts a and b are the same; a null one counts as empty.
//
static int Same(const char* a, const char* b)
{
    return strcmp(a == NULL ? "" : a, b == NULL ? "" : b) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SameClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a and b are the same reference clock.
//
static int SameClock(const DtNmosRefClock* a, const DtNmosRefClock* b)
{
    return a->Kind == b->Kind && Same(a->PtpVersion, b->PtpVersion) &&
           Same(a->Grandmaster, b->Grandmaster) && a->Traceable == b->Traceable &&
           a->Domain == b->Domain && Same(a->LocalMac, b->LocalMac) &&
           Same(a->Text, b->Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FlowsEqual -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether flows a and b describe the same flow.
//
static int FlowsEqual(const DtNmosFlow* a, const DtNmosFlow* b)
{
    if (a->Media != b->Media || !Same(a->DestinationIp, b->DestinationIp) ||
        a->DestinationPort != b->DestinationPort || !Same(a->SourceIp, b->SourceIp) ||
        a->PayloadType != b->PayloadType || a->ClockRate != b->ClockRate ||
        !SameClock(&a->RefClock, &b->RefClock) ||
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
               x->Depth == y->Depth && Same(x->Sampling, y->Sampling) &&
               Same(x->Colorimetry, y->Colorimetry) && Same(x->Tcs, y->Tcs) &&
               Same(x->Range, y->Range) && Same(x->PackingMode, y->PackingMode) &&
               Same(x->Ssn, y->Ssn) && Same(x->TransmitterType, y->TransmitterType);
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        const DtNmosAudioFormat* x = &a->Format.Audio;
        const DtNmosAudioFormat* y = &b->Format.Audio;
        return Same(x->Encoding, y->Encoding) && x->SampleRate == y->SampleRate &&
               x->Channels == y->Channels && x->PacketTimeNs == y->PacketTimeNs &&
               Same(x->ChannelOrder, y->ChannelOrder);
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        const DtNmosCompressedVideoFormat* x = &a->Format.CompressedVideo;
        const DtNmosCompressedVideoFormat* y = &b->Format.CompressedVideo;
        return Same(x->Encoding, y->Encoding) && x->Width == y->Width &&
               x->Height == y->Height && x->RateNumerator == y->RateNumerator &&
               x->RateDenominator == y->RateDenominator && x->Depth == y->Depth &&
               Same(x->Sampling, y->Sampling) && Same(x->Profile, y->Profile) &&
               Same(x->Level, y->Level) && Same(x->Sublevel, y->Sublevel) &&
               Same(x->Range, y->Range) && Same(x->Ssn, y->Ssn) &&
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
               Same(x->TransmissionModel, y->TransmissionModel) && Same(x->Ssn, y->Ssn);
    }
    case DTNMOS_MEDIA_OTHER:
        return Same(a->Format.Other.Encoding, b->Format.Other.Encoding) &&
               Same(a->Format.Other.Fmtp, b->Format.Other.Fmtp);
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_writes_what_it_reads_back -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_writes_what_it_reads_back(void)
{
    const char* const Texts[] = {VideoSdp, AudioSdp, CompressedSdp, AncSdp};
    for (size_t t = 0; t < sizeof(Texts) / sizeof(Texts[0]); ++t)
    {
        DtNmosSdp* Original = Parse(Texts[t]);
        REQUIRE(Original != NULL);
        const size_t Count = DtNmosSdp_FlowCount(Original);
        DtNmosFlow* Flows = calloc(Count, sizeof(*Flows));
        REQUIRE(Flows != NULL);
        for (size_t i = 0; i < Count; ++i)
        {
            Flows[i] = *DtNmosSdp_Flow(Original, i);
        }
        char Written[4096];
        size_t Size = sizeof(Written);
        const DtNmosResult Result =
            DtNmosSdp_Write(DtNmosSdp_Session(Original), Flows, Count, Written, &Size);
        if (Result != DTNMOS_OK)
        {
            printf("  %s\n", DtNmos_GetLastError());
        }
        CHECK(Result == DTNMOS_OK);
        DtNmosSdp* Again = Parse(Written);
        CHECK(Again != NULL);
        if (Again != NULL)
        {
            CHECK_EQ(DtNmosSdp_FlowCount(Again), Count);
            for (size_t i = 0; i < Count && i < DtNmosSdp_FlowCount(Again); ++i)
            {
                if (!FlowsEqual(DtNmosSdp_Flow(Again, i), &Flows[i]))
                {
                    printf("  flow %zu of SDP %zu differs after writing:\n%s\n", i, t,
                           Written);
                    CHECK(0);
                }
            }
            CHECK(
                Same(DtNmosSdp_Session(Again)->Name, DtNmosSdp_Session(Original)->Name));
        }
        DtNmosSdp_Free(Again);
        free(Flows);
        DtNmosSdp_Free(Original);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_writes_an_audio_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_writes_an_audio_sender(void)
{
    DtNmosSession Session = {0};
    Session.Size = sizeof(Session);
    Session.Name = "dt2110audiosink";
    snprintf(Session.OriginIp, sizeof(Session.OriginIp), "%s", "192.168.1.10");
    Session.SessionId = 42;
    Session.SessionVersion = 1;
    DtNmosFlow Flow = {0};
    Flow.Size = sizeof(Flow);
    Flow.Media = DTNMOS_MEDIA_AUDIO;
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "%s", "239.0.0.2");
    Flow.DestinationPort = 5004;
    Flow.PayloadType = 97;
    Flow.ClockRate = 48000;
    Flow.RefClock.Kind = DTNMOS_REFCLOCK_LOCALMAC;
    snprintf(Flow.RefClock.LocalMac, sizeof(Flow.RefClock.LocalMac), "%s",
             "00-14-F4-01-02-03");
    Flow.MediaClockDirect = 1;
    snprintf(Flow.Format.Audio.Encoding, sizeof(Flow.Format.Audio.Encoding), "%s", "L24");
    Flow.Format.Audio.SampleRate = 48000;
    Flow.Format.Audio.Channels = 2;
    Flow.Format.Audio.PacketTimeNs = 1000000;
    char Text[1024];
    size_t Size = sizeof(Text);
    REQUIRE(DtNmosSdp_Write(&Session, &Flow, 1, Text, &Size) == DTNMOS_OK);
    CHECK_EQ(Size, strlen(Text));
    CHECK_STR(Text, "v=0\r\n"
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
    size_t Small = 10;
    CHECK(DtNmosSdp_Write(&Session, &Flow, 1, Text, &Small) == DTNMOS_E_BUFFER_TOO_SMALL);
    CHECK_EQ(Small, Size + 1);
}

// .-.-.-.-.-.-.-.-.-.-.- sdp_refuses_to_write_an_incomplete_flow -.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_refuses_to_write_an_incomplete_flow(void)
{
    DtNmosSession Session = {0};
    Session.Size = sizeof(Session);
    snprintf(Session.OriginIp, sizeof(Session.OriginIp), "%s", "10.0.0.1");
    DtNmosFlow Flows[2] = {{0}, {0}};
    Flows[0].Size = sizeof(Flows[0]);
    Flows[1].Size = sizeof(Flows[1]);
    Flows[0].Media = DTNMOS_MEDIA_OTHER;
    char Text[2048];
    size_t Size = sizeof(Text);
    CHECK(DtNmosSdp_Write(&Session, Flows, 1, Text, &Size) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "destination") != NULL);
    snprintf(Flows[0].DestinationIp, sizeof(Flows[0].DestinationIp), "%s", "239.0.0.1");
    Flows[0].DestinationPort = 5000;
    Flows[1] = Flows[0];
    snprintf(Flows[1].DestinationIp, sizeof(Flows[1].DestinationIp), "%s", "239.0.0.2");
    Flows[1].Leg = 1;
    Flows[0].Leg = 1;
    Size = sizeof(Text);
    CHECK(DtNmosSdp_Write(&Session, Flows, 2, Text, &Size) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "second path") != NULL);
    Flows[0].Leg = 0;
    Size = sizeof(Text);
    REQUIRE(DtNmosSdp_Write(&Session, Flows, 2, Text, &Size) == DTNMOS_OK);
    CHECK(strstr(Text, "a=group:DUP primary0 secondary1\r\n") != NULL);
    DtNmosSession Empty = {0};
    Empty.Size = sizeof(Empty);
    Size = sizeof(Text);
    CHECK(DtNmosSdp_Write(&Empty, Flows, 1, Text, &Size) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- flow_is_copied_with_assignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A flow is copied with =. Its fixed arrays are its own and outlive the SDP it came
// from; its pointers, here DidSdid, point into that SDP.
void flow_is_copied_with_assignment(void)
{
    DtNmosSdp* Sdp = Parse(AncSdp);
    REQUIRE(Sdp != NULL);
    const DtNmosFlow Copy = *DtNmosSdp_Flow(Sdp, 0);
    CHECK(Copy.Format.Anc.DidSdid == DtNmosSdp_Flow(Sdp, 0)->Format.Anc.DidSdid);
    CHECK_EQ(Copy.Format.Anc.DidSdidCount, 2);
    CHECK_EQ(Copy.Format.Anc.DidSdid[1].Did, 0x41);
    DtNmosSdp_Free(Sdp);
    CHECK_STR(Copy.Format.Anc.Ssn, "ST2110-40:2023");
    CHECK_STR(Copy.DestinationIp, "239.100.9.11");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_the_forms_of_ts_refclk -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The forms of a=ts-refclk: a PTP domain of RFC 7273 and of ST 2110-10, a traceable PTP
// clock, a local MAC, and another kind, which keeps its text.
void sdp_reads_the_forms_of_ts_refclk(void)
{
    static const char* const Values[] = {
        "ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:domain-nmbr=127",
        "ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0",
        "ptp=IEEE802.1AS-2011:traceable",
        "localmac=CA-FE-01-02-03-04",
        "ntp=203.0.113.10",
    };
    char Text[512];
    DtNmosRefClock Clocks[5];
    for (size_t i = 0; i < 5; ++i)
    {
        snprintf(Text, sizeof(Text),
                 "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n"
                 "m=audio 5004 RTP/AVP 97\nc=IN IP4 239.0.0.1/64\n"
                 "a=rtpmap:97 L24/48000/2\na=ts-refclk:%s\n",
                 Values[i]);
        DtNmosSdp* Sdp = Parse(Text);
        REQUIRE(Sdp != NULL);
        Clocks[i] = DtNmosSdp_Flow(Sdp, 0)->RefClock;
        if (i == 4)
        {
            CHECK_STR(Clocks[i].Text, "ntp=203.0.113.10");
        }
        DtNmosSdp_Free(Sdp);
    }
    CHECK_EQ(Clocks[0].Kind, DTNMOS_REFCLOCK_PTP);
    CHECK_EQ(Clocks[0].Domain, 127);
    CHECK_EQ(Clocks[1].Kind, DTNMOS_REFCLOCK_PTP);
    CHECK_EQ(Clocks[1].Domain, -1);
    CHECK_STR(Clocks[1].Grandmaster, "39-A7-94-FF-FE-07-CB-D0");
    CHECK_EQ(Clocks[2].Kind, DTNMOS_REFCLOCK_PTP);
    CHECK(Clocks[2].Traceable);
    CHECK_STR(Clocks[2].PtpVersion, "IEEE802.1AS-2011");
    CHECK_STR(Clocks[2].Grandmaster, "");
    CHECK_EQ(Clocks[3].Kind, DTNMOS_REFCLOCK_LOCALMAC);
    CHECK_EQ(Clocks[4].Kind, DTNMOS_REFCLOCK_OTHER);
}

// .-.-.-.-.-.-.-.-.-.-.- sdp_refuses_a_value_longer_than_its_field -.-.-.-.-.-.-.-.-.-.-.
//
// A value of a=fmtp longer than its field, which no standard allows, fails the parse and
// names the line and the parameter.
void sdp_refuses_a_value_longer_than_its_field(void)
{
    CheckError(
        "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n"
        "m=video 5000 RTP/AVP 96\nc=IN IP4 239.0.0.1/64\n"
        "a=rtpmap:96 raw/90000\n"
        "a=fmtp:96 sampling=YCbCr-4:2:2-and-far-more-than-it-may-be\n",
        DTNMOS_E_PARSE,
        "SDP line 8: a=fmtp has a value longer than its standard allows: 'sampling'");
}
