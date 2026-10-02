// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSdp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of reading and writing the SDP of ST 2110 flows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_sdp.h"

#include <stdlib.h>
#include <string.h>

#include "NmosTest.h"

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpReadsVideoOnTwoPaths -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(SdpReadsVideoOnTwoPaths)
{
    DtNmosSdp* Sdp = Parse(VideoSdp);
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosSession* Session = DtNmosSdp_Session(Sdp);
    NMOS_ASSERT_STR(Session->Name, "Example of a SMPTE ST2110-20 signal");
    NMOS_ASSERT_STR(Session->OriginIp, "192.168.100.2");
    NMOS_ASSERT_EQ(Session->SessionId, 123456);
    NMOS_ASSERT_EQ(Session->SessionVersion, 11);
    NMOS_ASSERT(DtNmosSdp_FlowCount(Sdp) == 2);
    NMOS_ASSERT(DtNmosSdp_Flow(Sdp, 2) == NULL);

    const DtNmosFlow* Primary = DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT_EQ(Primary->Media, DTNMOS_MEDIA_VIDEO);
    NMOS_ASSERT_STR(Primary->DestinationIp, "239.100.9.10");
    NMOS_ASSERT_EQ(Primary->DestinationPort, 50000);
    NMOS_ASSERT_STR(Primary->SourceIp, "192.168.100.2");
    NMOS_ASSERT_EQ(Primary->PayloadType, 112);
    NMOS_ASSERT_EQ(Primary->ClockRate, 90000);
    // The domain as ST 2110-10 §8.2 writes it, after the grandmaster.
    NMOS_ASSERT_EQ(Primary->RefClock.Kind, DTNMOS_REFCLOCK_PTP);
    NMOS_ASSERT_STR(Primary->RefClock.PtpVersion, "IEEE1588-2008");
    NMOS_ASSERT_STR(Primary->RefClock.Grandmaster, "39-A7-94-FF-FE-07-CB-D0");
    NMOS_ASSERT_EQ(Primary->RefClock.Domain, 37);
    NMOS_ASSERT(!Primary->RefClock.Traceable);
    NMOS_ASSERT(Primary->MediaClockDirect);
    NMOS_ASSERT_EQ(Primary->MediaClockOffset, 0);
    NMOS_ASSERT_EQ(Primary->Leg, 0);
    const DtNmosVideoFormat* Video = &Primary->Format.Video;
    NMOS_ASSERT_EQ(Video->Width, 1280);
    NMOS_ASSERT_EQ(Video->Height, 720);
    NMOS_ASSERT_EQ(Video->RateNumerator, 60000);
    NMOS_ASSERT_EQ(Video->RateDenominator, 1001);
    NMOS_ASSERT_EQ(Video->Depth, 10);
    NMOS_ASSERT(!Video->Interlaced);
    NMOS_ASSERT_STR(Video->Sampling, "YCbCr-4:2:2");
    NMOS_ASSERT_STR(Video->Colorimetry, "BT709");
    NMOS_ASSERT_STR(Video->Tcs, "SDR");
    NMOS_ASSERT_STR(Video->PackingMode, "2110GPM");
    NMOS_ASSERT_STR(Video->Ssn, "ST2110-20:2017");
    NMOS_ASSERT_STR(Video->TransmitterType, "2110TPN");

    const DtNmosFlow* Secondary = DtNmosSdp_Flow(Sdp, 1);
    NMOS_ASSERT_EQ(Secondary->Leg, 1);
    NMOS_ASSERT_STR(Secondary->DestinationIp, "239.101.9.10");
    NMOS_ASSERT_EQ(Secondary->Format.Video.Height, 1080);
    NMOS_ASSERT_EQ(Secondary->Format.Video.RateNumerator, 25);
    NMOS_ASSERT_EQ(Secondary->Format.Video.RateDenominator, 1);
    NMOS_ASSERT(Secondary->Format.Video.Interlaced);
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpReadsAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(SdpReadsAudio)
{
    DtNmosSdp* Sdp = Parse(AudioSdp);
    NMOS_ASSERT(Sdp != NULL);
    NMOS_ASSERT(DtNmosSdp_FlowCount(Sdp) == 1);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT_EQ(Flow->Media, DTNMOS_MEDIA_AUDIO);
    NMOS_ASSERT_STR(Flow->SourceIp, "");
    NMOS_ASSERT_EQ(Flow->MediaClockOffset, 1234);
    const DtNmosAudioFormat* Audio = &Flow->Format.Audio;
    NMOS_ASSERT_STR(Audio->Encoding, "L24");
    NMOS_ASSERT_EQ(Audio->SampleRate, 48000);
    NMOS_ASSERT_EQ(Audio->Channels, 8);
    NMOS_ASSERT_EQ(Audio->PacketTimeNs, 125000);
    NMOS_ASSERT_STR(Audio->ChannelOrder, "SMPTE2110.(SGRP,SGRP)");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpReadsCompressedVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(SdpReadsCompressedVideo)
{
    DtNmosSdp* Sdp = Parse(CompressedSdp);
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT(Flow != NULL);
    NMOS_ASSERT_EQ(Flow->Media, DTNMOS_MEDIA_COMPRESSED_VIDEO);
    const DtNmosCompressedVideoFormat* Video = &Flow->Format.CompressedVideo;
    NMOS_ASSERT_STR(Video->Encoding, "jxsv");
    NMOS_ASSERT_STR(Video->Profile, "High444.12");
    NMOS_ASSERT_STR(Video->Level, "2k-1");
    NMOS_ASSERT_STR(Video->Sublevel, "Sublev3bpp");
    NMOS_ASSERT_EQ(Video->PacketMode, 0);
    NMOS_ASSERT_EQ(Video->TransmissionMode, 1);
    NMOS_ASSERT_EQ(Video->BandwidthKbps, 116000);
    NMOS_ASSERT_EQ(Video->Width, 1920);
    NMOS_ASSERT_EQ(Video->RateDenominator, 1001);
    NMOS_ASSERT_STR(Video->Range, "FULL");
    NMOS_ASSERT_STR(Video->TransmitterType, "2110TPNL");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpReadsAncillaryData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(SdpReadsAncillaryData)
{
    DtNmosSdp* Sdp = Parse(AncSdp);
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT(Flow != NULL);
    NMOS_ASSERT_EQ(Flow->Media, DTNMOS_MEDIA_ANC);
    const DtNmosAncFormat* Anc = &Flow->Format.Anc;
    NMOS_ASSERT(Anc->DidSdidCount == 2);
    NMOS_ASSERT_EQ(Anc->DidSdid[0].Did, 0x61);
    NMOS_ASSERT_EQ(Anc->DidSdid[0].Sdid, 0x02);
    NMOS_ASSERT_EQ(Anc->DidSdid[1].Did, 0x41);
    NMOS_ASSERT_EQ(Anc->DidSdid[1].Sdid, 0x05);
    NMOS_ASSERT_EQ(Anc->VpidCode, 133);
    NMOS_ASSERT_EQ(Anc->RateNumerator, 30000);
    NMOS_ASSERT_STR(Anc->TransmissionModel, "CTM");
    NMOS_ASSERT_STR(Anc->Ssn, "ST2110-40:2023");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpReadsOtherMediaAsTheyAre -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(SdpReadsOtherMediaAsTheyAre)
{
    DtNmosSdp* Sdp = Parse("v=0\r\no=- 1 1 IN IP4 10.0.0.8\r\ns=H.264\r\nt=0 0\r\n"
                           "m=video 5000 RTP/AVP 96\r\nc=IN IP4 239.0.0.9/16\r\n"
                           "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1; "
                           "profile-level-id=42e01f\r\n");
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT(Flow != NULL);
    NMOS_ASSERT_EQ(Flow->Media, DTNMOS_MEDIA_OTHER);
    NMOS_ASSERT_STR(Flow->Format.Other.Encoding, "H264");
    NMOS_ASSERT_STR(Flow->Format.Other.Fmtp,
                    "packetization-mode=1; profile-level-id=42e01f");
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpTakesDefaultsOfTheSession -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(SdpTakesDefaultsOfTheSession)
{
    // c=, a=source-filter, a=ts-refclk and a=mediaclk of the session, IPv6, and a line
    // without its carriage return at the end.
    DtNmosSdp* Sdp =
        Parse("v=0\no=- 2 3 IN IP6 fd00::5\ns=Defaults\nc=IN IP6 ff15::7\nt=0 0\n"
              "a=source-filter: incl IN IP6 ff15::7 "
              "fd00::5\na=ts-refclk:localmac=CA-FE-01-02-03-04\n"
              "a=mediaclk:direct=5\n"
              "m=audio 5006 RTP/AVP 98\na=rtpmap:98 L16/48000/2\na=ptime:1");
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT(Flow != NULL);
    NMOS_ASSERT_STR(Flow->DestinationIp, "ff15::7");
    NMOS_ASSERT_STR(Flow->SourceIp, "fd00::5");
    NMOS_ASSERT_EQ(Flow->RefClock.Kind, DTNMOS_REFCLOCK_LOCALMAC);
    NMOS_ASSERT_STR(Flow->RefClock.LocalMac, "CA-FE-01-02-03-04");
    NMOS_ASSERT_EQ(Flow->MediaClockOffset, 5);
    NMOS_ASSERT_STR(Flow->Format.Audio.Encoding, "L16");
    NMOS_ASSERT_EQ(Flow->Format.Audio.PacketTimeNs, 1000000);
    NMOS_ASSERT_STR(DtNmosSdp_Session(Sdp)->OriginIp, "fd00::5");
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
    NMOS_ASSERT_EQ(Result, Code);
    NMOS_ASSERT(Sdp == NULL);
    if (strstr(DtNmos_GetLastError(), Fragment) == NULL)
    {
        printf("  the message \"%s\" lacks \"%s\"\n", DtNmos_GetLastError(), Fragment);
        NMOS_ASSERT(strstr(DtNmos_GetLastError(), Fragment) != NULL);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpNamesTheLineOfAnError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(SdpNamesTheLineOfAnError)
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
    NMOS_ASSERT(DtNmosSdp_Parse(NULL, 1, &Sdp) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Same -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks that the texts a and b are the same; a null one counts as empty.
//
static bool Same(const char* a, const char* b)
{
    return strcmp(a == NULL ? "" : a, b == NULL ? "" : b) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SameClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a and b are the same reference clock.
//
static bool SameClock(const DtNmosRefClock* a, const DtNmosRefClock* b)
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
static bool FlowsEqual(const DtNmosFlow* a, const DtNmosFlow* b)
{
    if (a->Media != b->Media || !Same(a->DestinationIp, b->DestinationIp) ||
        a->DestinationPort != b->DestinationPort || !Same(a->SourceIp, b->SourceIp) ||
        a->PayloadType != b->PayloadType || a->ClockRate != b->ClockRate ||
        !SameClock(&a->RefClock, &b->RefClock) ||
        a->MediaClockDirect != b->MediaClockDirect ||
        a->MediaClockOffset != b->MediaClockOffset || a->Leg != b->Leg)
    {
        return false;
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
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpWritesWhatItReadsBack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(SdpWritesWhatItReadsBack)
{
    const char* const Texts[] = {VideoSdp, AudioSdp, CompressedSdp, AncSdp};
    for (size_t t = 0; t < sizeof(Texts) / sizeof(Texts[0]); ++t)
    {
        DtNmosSdp* Original = Parse(Texts[t]);
        NMOS_ASSERT(Original != NULL);
        const size_t Count = DtNmosSdp_FlowCount(Original);
        DtNmosFlow* Flows = calloc(Count, sizeof(*Flows));
        NMOS_ASSERT(Flows != NULL);
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
        NMOS_ASSERT(Result == DTNMOS_OK);
        DtNmosSdp* Again = Parse(Written);
        NMOS_ASSERT(Again != NULL);
        if (Again != NULL)
        {
            NMOS_ASSERT_EQ(DtNmosSdp_FlowCount(Again), Count);
            for (size_t i = 0; i < Count && i < DtNmosSdp_FlowCount(Again); ++i)
            {
                if (!FlowsEqual(DtNmosSdp_Flow(Again, i), &Flows[i]))
                {
                    printf("  flow %zu of SDP %zu differs after writing:\n%s\n", i, t,
                           Written);
                    NMOS_ASSERT(0);
                }
            }
            NMOS_ASSERT(
                Same(DtNmosSdp_Session(Again)->Name, DtNmosSdp_Session(Original)->Name));
        }
        DtNmosSdp_Free(Again);
        free(Flows);
        DtNmosSdp_Free(Original);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpWritesAnAudioSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(SdpWritesAnAudioSender)
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
    NMOS_ASSERT(DtNmosSdp_Write(&Session, &Flow, 1, Text, &Size) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Size, strlen(Text));
    NMOS_ASSERT_STR(Text, "v=0\r\n"
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
    NMOS_ASSERT(DtNmosSdp_Write(&Session, &Flow, 1, Text, &Small) ==
                DTNMOS_E_BUFFER_TOO_SMALL);
    NMOS_ASSERT_EQ(Small, Size + 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- SdpRefusesToWriteAnIncompleteFlow -.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(SdpRefusesToWriteAnIncompleteFlow)
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
    NMOS_ASSERT(DtNmosSdp_Write(&Session, Flows, 1, Text, &Size) ==
                DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "destination") != NULL);
    snprintf(Flows[0].DestinationIp, sizeof(Flows[0].DestinationIp), "%s", "239.0.0.1");
    Flows[0].DestinationPort = 5000;
    Flows[1] = Flows[0];
    snprintf(Flows[1].DestinationIp, sizeof(Flows[1].DestinationIp), "%s", "239.0.0.2");
    Flows[1].Leg = 1;
    Flows[0].Leg = 1;
    Size = sizeof(Text);
    NMOS_ASSERT(DtNmosSdp_Write(&Session, Flows, 2, Text, &Size) ==
                DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "second path") != NULL);
    Flows[0].Leg = 0;
    Size = sizeof(Text);
    NMOS_ASSERT(DtNmosSdp_Write(&Session, Flows, 2, Text, &Size) == DTNMOS_OK);
    NMOS_ASSERT(strstr(Text, "a=group:DUP primary secondary\r\n") != NULL);
    DtNmosSession Empty = {0};
    Empty.Size = sizeof(Empty);
    Size = sizeof(Text);
    NMOS_ASSERT(DtNmosSdp_Write(&Empty, Flows, 1, Text, &Size) ==
                DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpNamesThePathsOfEachPair -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Five flows: a pair, a flow of one path, and a pair. The first pair is primary and
// secondary, the second primary2 and secondary2, and the flow of one path has no a=mid;
// read back, the pairs are the same.
NMOS_TEST(SdpNamesThePathsOfEachPair)
{
    DtNmosSession Session = {0};
    Session.Size = sizeof(Session);
    snprintf(Session.OriginIp, sizeof(Session.OriginIp), "%s", "10.0.0.1");
    DtNmosFlow Flows[5];
    memset(Flows, 0, sizeof(Flows));
    const uint32_t Legs[5] = {0, 1, 0, 0, 1};
    for (int i = 0; i < 5; i++)
    {
        Flows[i].Size = sizeof(Flows[i]);
        Flows[i].Media = DTNMOS_MEDIA_OTHER;
        snprintf(Flows[i].DestinationIp, sizeof(Flows[i].DestinationIp), "239.0.0.%d",
                 i + 1);
        Flows[i].DestinationPort = 5000;
        snprintf(Flows[i].Format.Other.Encoding, sizeof(Flows[i].Format.Other.Encoding),
                 "%s", "x");
        Flows[i].Leg = Legs[i];
    }
    char Text[4096];
    size_t Size = sizeof(Text);
    NMOS_ASSERT(DtNmosSdp_Write(&Session, Flows, 5, Text, &Size) == DTNMOS_OK);
    NMOS_ASSERT(strstr(Text, "a=group:DUP primary secondary\r\n"
                             "a=group:DUP primary2 secondary2\r\n") != NULL);
    // Each a=mid follows the media section it names; the third has none.
    const char* Third = strstr(Text, "c=IN IP4 239.0.0.3/64");
    const char* Fourth = strstr(Text, "c=IN IP4 239.0.0.4/64");
    NMOS_ASSERT(Third != NULL && Fourth != NULL);
    const char* Primary = strstr(Text, "a=mid:primary\r\n");
    const char* Secondary = strstr(Text, "a=mid:secondary\r\n");
    NMOS_ASSERT(Primary != NULL && Primary < Third);
    NMOS_ASSERT(Secondary != NULL && Secondary < Third);
    NMOS_ASSERT(strstr(Third, "a=mid:") > Fourth);
    NMOS_ASSERT(strstr(Fourth, "a=mid:primary2\r\n") != NULL);
    NMOS_ASSERT(strstr(Fourth, "a=mid:secondary2\r\n") != NULL);

    DtNmosSdp* Sdp = NULL;
    NMOS_ASSERT(DtNmosSdp_Parse(Text, Size, &Sdp) == DTNMOS_OK);
    NMOS_ASSERT_EQ(DtNmosSdp_FlowCount(Sdp), 5);
    for (size_t i = 0; i < 5; i++)
    {
        NMOS_ASSERT_EQ(DtNmosSdp_Flow(Sdp, i)->Leg, Legs[i]);
    }
    DtNmosSdp_Free(Sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- FlowIsCopiedWithAssignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A flow is copied with =. Its fixed arrays are its own and outlive the SDP it came
// from; its pointers, here DidSdid, point into that SDP.
NMOS_TEST(FlowIsCopiedWithAssignment)
{
    DtNmosSdp* Sdp = Parse(AncSdp);
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosFlow Copy = *DtNmosSdp_Flow(Sdp, 0);
    NMOS_ASSERT(Copy.Format.Anc.DidSdid == DtNmosSdp_Flow(Sdp, 0)->Format.Anc.DidSdid);
    NMOS_ASSERT_EQ(Copy.Format.Anc.DidSdidCount, 2);
    NMOS_ASSERT_EQ(Copy.Format.Anc.DidSdid[1].Did, 0x41);
    DtNmosSdp_Free(Sdp);
    NMOS_ASSERT_STR(Copy.Format.Anc.Ssn, "ST2110-40:2023");
    NMOS_ASSERT_STR(Copy.DestinationIp, "239.100.9.11");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SdpReadsTheFormsOfTsRefclk -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The forms of a=ts-refclk: a PTP domain of RFC 7273 and of ST 2110-10, a traceable PTP
// clock, a local MAC, and another kind, which keeps its text.
NMOS_TEST(SdpReadsTheFormsOfTsRefclk)
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
        NMOS_ASSERT(Sdp != NULL);
        Clocks[i] = DtNmosSdp_Flow(Sdp, 0)->RefClock;
        if (i == 4)
        {
            NMOS_ASSERT_STR(Clocks[i].Text, "ntp=203.0.113.10");
        }
        DtNmosSdp_Free(Sdp);
    }
    NMOS_ASSERT_EQ(Clocks[0].Kind, DTNMOS_REFCLOCK_PTP);
    NMOS_ASSERT_EQ(Clocks[0].Domain, 127);
    NMOS_ASSERT_EQ(Clocks[1].Kind, DTNMOS_REFCLOCK_PTP);
    NMOS_ASSERT_EQ(Clocks[1].Domain, -1);
    NMOS_ASSERT_STR(Clocks[1].Grandmaster, "39-A7-94-FF-FE-07-CB-D0");
    NMOS_ASSERT_EQ(Clocks[2].Kind, DTNMOS_REFCLOCK_PTP);
    NMOS_ASSERT(Clocks[2].Traceable);
    NMOS_ASSERT_STR(Clocks[2].PtpVersion, "IEEE802.1AS-2011");
    NMOS_ASSERT_STR(Clocks[2].Grandmaster, "");
    NMOS_ASSERT_EQ(Clocks[3].Kind, DTNMOS_REFCLOCK_LOCALMAC);
    NMOS_ASSERT_EQ(Clocks[4].Kind, DTNMOS_REFCLOCK_OTHER);
}

// .-.-.-.-.-.-.-.-.-.-.-.- SdpRefusesAValueLongerThanItsField -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A value of a=fmtp longer than its field, which no standard allows, fails the parse and
// names the line and the parameter.
NMOS_TEST(SdpRefusesAValueLongerThanItsField)
{
    CheckError(
        "v=0\no=- 1 1 IN IP4 10.0.0.1\ns=x\nt=0 0\n"
        "m=video 5000 RTP/AVP 96\nc=IN IP4 239.0.0.1/64\n"
        "a=rtpmap:96 raw/90000\n"
        "a=fmtp:96 sampling=YCbCr-4:2:2-and-far-more-than-it-may-be\n",
        DTNMOS_E_PARSE,
        "SDP line 8: a=fmtp has a value longer than its standard allows: 'sampling'");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FlowCopyOutlivesItsSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A flow copied with DtNmosFlow_Copy() owns what it points to: the DID and SDID pairs of
// an ANC flow stay readable after the SDP it came from is freed, at addresses of their
// own. Null is refused, and freeing null does nothing.
NMOS_TEST(FlowCopyOutlivesItsSdp)
{
    DtNmosSdp* Sdp = Parse(AncSdp);
    NMOS_ASSERT(Sdp != NULL);
    const DtNmosFlow* Original = DtNmosSdp_Flow(Sdp, 0);
    DtNmosFlow* Copy = NULL;
    NMOS_ASSERT_EQ(DtNmosFlow_Copy(Original, &Copy), DTNMOS_OK);
    NMOS_ASSERT(Copy != NULL);
    NMOS_ASSERT(Copy->Format.Anc.DidSdid != Original->Format.Anc.DidSdid);
    DtNmosSdp_Free(Sdp);
    NMOS_ASSERT_EQ(Copy->Format.Anc.DidSdidCount, 2);
    NMOS_ASSERT_EQ(Copy->Format.Anc.DidSdid[1].Did, 0x41);
    NMOS_ASSERT_STR(Copy->DestinationIp, "239.100.9.11");
    DtNmosFlow_Free(Copy);

    NMOS_ASSERT_EQ(DtNmosFlow_Copy(NULL, &Copy), DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(Copy == NULL);
    DtNmosFlow_Free(NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- VideoFormatTakesDefaults -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The colorimetry of an empty format follows its raster, SD, HD or UHD, whatever its
// width; the transfer characteristic is SDR; what the caller set stays, and a format
// without a height gets no colorimetry.
NMOS_TEST(VideoFormatTakesDefaults)
{
    static const struct
    {
        uint32_t Width;
        uint32_t Height;
        const char* Colorimetry;
    } Cases[] = {{720, 486, "BT601"},   {720, 576, "BT601"},   {1280, 720, "BT709"},
                 {1920, 1080, "BT709"}, {2048, 1080, "BT709"}, {3840, 2160, "BT2020"},
                 {7680, 4320, "BT2020"}};
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); ++i)
    {
        DtNmosVideoFormat Format;
        memset(&Format, 0, sizeof(Format));
        Format.Width = Cases[i].Width;
        Format.Height = Cases[i].Height;
        DtNmosVideoFormat_SetDefaults(&Format);
        NMOS_ASSERT_STR(Format.Colorimetry, Cases[i].Colorimetry);
        NMOS_ASSERT_STR(Format.Tcs, "SDR");
        NMOS_ASSERT_STR(Format.Range, "");
    }

    DtNmosVideoFormat Format;
    memset(&Format, 0, sizeof(Format));
    Format.Height = 2160;
    snprintf(Format.Colorimetry, sizeof(Format.Colorimetry), "BT2100");
    snprintf(Format.Tcs, sizeof(Format.Tcs), "PQ");
    DtNmosVideoFormat_SetDefaults(&Format);
    NMOS_ASSERT_STR(Format.Colorimetry, "BT2100");
    NMOS_ASSERT_STR(Format.Tcs, "PQ");

    memset(&Format, 0, sizeof(Format));
    DtNmosVideoFormat_SetDefaults(&Format);
    NMOS_ASSERT_STR(Format.Colorimetry, "");
    DtNmosVideoFormat_SetDefaults(NULL);
}

NMOS_TEST_MAIN("Sdp", NMOS_RUN(SdpReadsVideoOnTwoPaths), NMOS_RUN(SdpReadsAudio),
               NMOS_RUN(SdpReadsCompressedVideo), NMOS_RUN(SdpReadsAncillaryData),
               NMOS_RUN(SdpReadsOtherMediaAsTheyAre),
               NMOS_RUN(SdpTakesDefaultsOfTheSession), NMOS_RUN(SdpNamesTheLineOfAnError),
               NMOS_RUN(SdpWritesWhatItReadsBack), NMOS_RUN(SdpWritesAnAudioSender),
               NMOS_RUN(SdpRefusesToWriteAnIncompleteFlow),
               NMOS_RUN(SdpNamesThePathsOfEachPair), NMOS_RUN(FlowIsCopiedWithAssignment),
               NMOS_RUN(SdpReadsTheFormsOfTsRefclk),
               NMOS_RUN(SdpRefusesAValueLongerThanItsField),
               NMOS_RUN(FlowCopyOutlivesItsSdp), NMOS_RUN(VideoFormatTakesDefaults))
