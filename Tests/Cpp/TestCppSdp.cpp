// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestCppSdp.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API of the SDP
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string>
#include <utility>
#include <vector>

#include "NmosTest.h"
#include "dtnmos_sdp.hpp"

// Camera.sdp of the examples: a video stream sent on two paths (ST 2022-7), and an
// audio stream.
static const char* const CameraSdp =
    "v=0\r\n"
    "o=- 1443716955 1443716955 IN IP4 192.168.1.10\r\n"
    "s=Camera 1\r\n"
    "t=0 0\r\n"
    "a=group:DUP primary secondary\r\n"
    "m=video 5004 RTP/AVP 96\r\n"
    "c=IN IP4 239.10.1.1/64\r\n"
    "a=source-filter: incl IN IP4 239.10.1.1 192.168.1.10\r\n"
    "a=rtpmap:96 raw/90000\r\n"
    "a=fmtp:96 sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=25; "
    "depth=10; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN; "
    "interlace\r\n"
    "a=ts-refclk:ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:127\r\n"
    "a=mediaclk:direct=0\r\n"
    "a=mid:primary\r\n"
    "m=video 5004 RTP/AVP 96\r\n"
    "c=IN IP4 239.20.1.1/64\r\n"
    "a=source-filter: incl IN IP4 239.20.1.1 192.168.2.10\r\n"
    "a=rtpmap:96 raw/90000\r\n"
    "a=fmtp:96 sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=25; "
    "depth=10; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN; "
    "interlace\r\n"
    "a=ts-refclk:ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:127\r\n"
    "a=mediaclk:direct=0\r\n"
    "a=mid:secondary\r\n"
    "m=audio 5006 RTP/AVP 97\r\n"
    "c=IN IP4 239.10.1.2/64\r\n"
    "a=source-filter: incl IN IP4 239.10.1.2 192.168.1.10\r\n"
    "a=rtpmap:97 L24/48000/2\r\n"
    "a=fmtp:97 channel-order=SMPTE2110.(ST)\r\n"
    "a=ptime:1\r\n"
    "a=ts-refclk:ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:127\r\n"
    "a=mediaclk:direct=0\r\n";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppSdpReadsCamera -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sdp::Parse() reads the session and the three flows of Camera.sdp. The first is
// 1920x1080i 25 video to 239.10.1.1:5004 from 192.168.1.10, on PTP domain 127. The
// second is its second path, to 239.20.1.1, as Leg 1. The third is L24 audio at 48000
// Hz with 2 channels to 239.10.1.2:5006. Writing the SDP and reading it again gives the
// same Sdp.
//
NMOS_TEST(CppSdpReadsCamera)
{
    const DtNmos::Expected<DtNmos::Sdp> Read = DtNmos::Sdp::Parse(CameraSdp);
    NMOS_ASSERT(Read.has_value());
    NMOS_ASSERT(Read->Session.Name == "Camera 1");
    NMOS_ASSERT(Read->Session.OriginIp == "192.168.1.10");
    NMOS_ASSERT_EQ(Read->Session.SessionId, 1443716955u);
    NMOS_ASSERT_EQ(Read->Flows.size(), 3);

    const DtNmos::Flow& Primary = Read->Flows[0];
    NMOS_ASSERT(Primary.GetMedia() == DtNmos::Media::Video);
    NMOS_ASSERT(Primary.DestinationIp == "239.10.1.1");
    NMOS_ASSERT_EQ(Primary.DestinationPort, 5004);
    NMOS_ASSERT(Primary.SourceIp == "192.168.1.10");
    NMOS_ASSERT_EQ(Primary.Leg, 0);
    NMOS_ASSERT(Primary.RefClock.Kind == DtNmos::RefClockKind::Ptp);
    NMOS_ASSERT(Primary.RefClock.Grandmaster == "39-A7-94-FF-FE-07-CB-D0");
    NMOS_ASSERT_EQ(Primary.RefClock.Domain, 127);
    const auto& Video = std::get<DtNmos::VideoFormat>(Primary.Format);
    NMOS_ASSERT_EQ(Video.Width, 1920);
    NMOS_ASSERT_EQ(Video.Height, 1080);
    NMOS_ASSERT(Video.Interlaced);
    NMOS_ASSERT(Video.Sampling == DtNmos::Sampling::YCbCr422);
    NMOS_ASSERT(Video.Colorimetry == DtNmos::Colorimetry::Bt709);
    NMOS_ASSERT(Video.Ssn == "ST2110-20:2017");
    NMOS_ASSERT_EQ(Read->Flows[1].Leg, 1);
    NMOS_ASSERT(Read->Flows[1].DestinationIp == "239.20.1.1");

    const DtNmos::Flow& Sound = Read->Flows[2];
    NMOS_ASSERT(Sound.GetMedia() == DtNmos::Media::Audio);
    const auto& Audio = std::get<DtNmos::AudioFormat>(Sound.Format);
    NMOS_ASSERT(Audio.Encoding == DtNmos::AudioEncoding::L24);
    NMOS_ASSERT_EQ(Audio.SampleRate, 48000);
    NMOS_ASSERT_EQ(Audio.Channels, 2);
    NMOS_ASSERT_EQ(Audio.PacketTimeNs, 1000000);
    NMOS_ASSERT(Audio.ChannelOrder == "SMPTE2110.(ST)");

    const DtNmos::Expected<std::string> Written = Read->Write();
    NMOS_ASSERT(Written.has_value());
    NMOS_ASSERT(Written->find("a=group:DUP") != std::string::npos);
    const DtNmos::Expected<DtNmos::Sdp> Again = DtNmos::Sdp::Parse(*Written);
    NMOS_ASSERT(Again.has_value());
    NMOS_ASSERT(*Again == *Read);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RoundTrip -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether Value converts to the C API and back to itself.
//
static bool RoundTrip(const DtNmos::Flow& Value)
{
    DtNmos::Detail::NativeFlow Native;
    if (!Native.Set(Value))
    {
        return false;
    }
    return DtNmos::Detail::FromNative(Native.Get()) == Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transport -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns a flow with Format whose other fields all differ from their defaults.
//
static DtNmos::Flow Transport(DtNmos::FlowFormat Format)
{
    DtNmos::Flow Value;
    Value.DestinationIp = "239.1.2.3";
    Value.DestinationPort = 5010;
    Value.SourceIp = "192.168.1.9";
    Value.PayloadType = 98;
    Value.ClockRate = 90000;
    Value.RefClock.Kind = DtNmos::RefClockKind::Ptp;
    Value.RefClock.PtpVersion = "IEEE1588-2008";
    Value.RefClock.Grandmaster = "39-A7-94-FF-FE-07-CB-D0";
    Value.RefClock.Traceable = true;
    Value.RefClock.Domain = 3;
    Value.RefClock.LocalMac = "00-14-F4-01-02-03";
    Value.RefClock.Text = "ntp=pool.ntp.org";
    Value.MediaClockDirect = true;
    Value.MediaClockOffset = 77;
    Value.Leg = 1;
    Value.Format = std::move(Format);
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppSdpRoundTrips -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every field converts to the C API and back unchanged. The test sets every field of a
// flow of each kind, and of its format, to a value that is not its default. It also
// converts a flow without a format, an OtherFormat without an a=fmtp line, and the
// session. A field that one direction forgets comes back as its default, and fails the
// test.
//
NMOS_TEST(CppSdpRoundTrips)
{
    DtNmos::VideoFormat Video;
    Video.Width = 1280;
    Video.Height = 720;
    Video.RateNumerator = 60000;
    Video.RateDenominator = 1001;
    Video.Interlaced = true;
    Video.Segmented = true;
    Video.Depth = 12;
    Video.Sampling = DtNmos::Sampling::Rgb;
    Video.Colorimetry = DtNmos::Colorimetry::Bt2100;
    Video.Tcs = DtNmos::Tcs::Hlg;
    Video.Range = DtNmos::Range::Full;
    Video.PackingMode = DtNmos::PackingMode::Block;
    Video.TransmitterType = DtNmos::TransmitterType::Wide;
    Video.Ssn = "ST2110-20:2022";
    Video.OtherParameters = "TROFF=37";
    NMOS_EXPECT(RoundTrip(Transport(Video)));

    DtNmos::AudioFormat Audio;
    Audio.Encoding = DtNmos::AudioEncoding::Am824;
    Audio.SampleRate = 96000;
    Audio.Channels = 8;
    Audio.PacketTimeNs = 125000;
    Audio.ChannelOrder = "SMPTE2110.(SGRP,SGRP)";
    Audio.OtherParameters = "MAXPTIME=1";
    NMOS_EXPECT(RoundTrip(Transport(Audio)));

    DtNmos::CompressedVideoFormat Compressed;
    Compressed.Encoding = "jxsv";
    Compressed.Width = 3840;
    Compressed.Height = 2160;
    Compressed.RateNumerator = 50;
    Compressed.RateDenominator = 1;
    Compressed.Interlaced = true;
    Compressed.Segmented = true;
    Compressed.Depth = 10;
    Compressed.Sampling = DtNmos::Sampling::YCbCr444;
    Compressed.Colorimetry = DtNmos::Colorimetry::Bt2020;
    Compressed.Tcs = DtNmos::Tcs::Pq;
    Compressed.Range = DtNmos::Range::Narrow;
    Compressed.TransmitterType = DtNmos::TransmitterType::NarrowLinear;
    Compressed.Ssn = "ST2110-22:2019";
    Compressed.Profile = "High444.12";
    Compressed.Level = "4k-1";
    Compressed.Sublevel = "Sublev3bpp";
    Compressed.PacketMode = 1;
    Compressed.TransmissionMode = 2;
    Compressed.BandwidthKbps = 116000;
    Compressed.OtherParameters = "MAXUDP=8960";
    NMOS_EXPECT(RoundTrip(Transport(Compressed)));

    DtNmos::AncFormat Anc;
    Anc.DidSdid = {{0x61, 0x02}, {0x41, 0x05}};
    Anc.VpidCode = 133;
    Anc.RateNumerator = 30000;
    Anc.RateDenominator = 1001;
    Anc.TransmissionModel = "CTM";
    Anc.Ssn = "ST2110-40:2023";
    NMOS_EXPECT(RoundTrip(Transport(Anc)));

    DtNmos::OtherFormat Other;
    Other.Encoding = "H264";
    Other.Fmtp = "profile-level-id=42e01f";
    NMOS_EXPECT(RoundTrip(Transport(Other)));
    Other.Fmtp = std::string();
    NMOS_EXPECT(RoundTrip(Transport(Other)));
    Other.Fmtp.reset();
    NMOS_EXPECT(RoundTrip(Transport(Other)));
    NMOS_EXPECT(RoundTrip(Transport(std::monostate())));

    DtNmos::Session Session;
    Session.Name = "Camera 2";
    Session.OriginIp = "192.168.1.20";
    Session.SessionId = 12;
    Session.SessionVersion = 13;
    DtNmos::Detail::NativeSession Native;
    NMOS_ASSERT(Native.Set(Session).has_value());
    NMOS_ASSERT(DtNmos::Detail::FromNative(Native.Get()) == Session);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppSdpGetsMedia -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A flow's media is the kind of its format. Each kind has the C API's name.
//
NMOS_TEST(CppSdpGetsMedia)
{
    const std::pair<DtNmos::FlowFormat, DtNmos::Media> Kinds[] = {
        {std::monostate(), DtNmos::Media::None},
        {DtNmos::VideoFormat(), DtNmos::Media::Video},
        {DtNmos::AudioFormat(), DtNmos::Media::Audio},
        {DtNmos::CompressedVideoFormat(), DtNmos::Media::CompressedVideo},
        {DtNmos::AncFormat(), DtNmos::Media::Anc},
        {DtNmos::OtherFormat(), DtNmos::Media::Other},
    };
    for (const auto& [Format, Kind] : Kinds)
    {
        DtNmos::Flow Value;
        Value.Format = Format;
        NMOS_EXPECT(Value.GetMedia() == Kind);
    }
    NMOS_ASSERT(DtNmos::Name(DtNmos::Media::Video) == "video");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppSdpTexts -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A value has the SDP's text, and that text reads back as the value. "" reads as None.
// A text the library does not know reads as Other, and Other has the text "".
//
NMOS_TEST(CppSdpTexts)
{
    NMOS_ASSERT(DtNmos::Text(DtNmos::Sampling::YCbCr422) == "YCbCr-4:2:2");
    NMOS_ASSERT(DtNmos::FromText<DtNmos::Sampling>("YCbCr-4:2:2") ==
                DtNmos::Sampling::YCbCr422);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::Sampling>("") == DtNmos::Sampling::None);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::Sampling>("YCbCr-4:1:1") ==
                DtNmos::Sampling::Other);
    NMOS_ASSERT(DtNmos::Text(DtNmos::Sampling::Other).empty());
    NMOS_ASSERT(DtNmos::FromText<DtNmos::Colorimetry>(DtNmos::Text(
                    DtNmos::Colorimetry::Bt2100)) == DtNmos::Colorimetry::Bt2100);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::Tcs>(DtNmos::Text(DtNmos::Tcs::Hlg)) ==
                DtNmos::Tcs::Hlg);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::Range>(DtNmos::Text(DtNmos::Range::Full)) ==
                DtNmos::Range::Full);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::PackingMode>(DtNmos::Text(
                    DtNmos::PackingMode::Block)) == DtNmos::PackingMode::Block);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::TransmitterType>(DtNmos::Text(
                    DtNmos::TransmitterType::Wide)) == DtNmos::TransmitterType::Wide);
    NMOS_ASSERT(DtNmos::FromText<DtNmos::AudioEncoding>("L24") ==
                DtNmos::AudioEncoding::L24);
    NMOS_ASSERT(DtNmos::Text(DtNmos::AudioEncoding::Am824) == "AM824");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppSdpDefaults -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SetDefaults() gives a picture of 1080 lines Bt709 and Sdr, and one of 2160 lines
// Bt2020. It keeps a colorimetry and TCS that are set. With a height of 0, the
// colorimetry stays None.
//
NMOS_TEST(CppSdpDefaults)
{
    DtNmos::VideoFormat Hd;
    Hd.Height = 1080;
    Hd.SetDefaults();
    NMOS_ASSERT(Hd.Colorimetry == DtNmos::Colorimetry::Bt709);
    NMOS_ASSERT(Hd.Tcs == DtNmos::Tcs::Sdr);
    DtNmos::VideoFormat Uhd;
    Uhd.Height = 2160;
    Uhd.SetDefaults();
    NMOS_ASSERT(Uhd.Colorimetry == DtNmos::Colorimetry::Bt2020);
    DtNmos::VideoFormat Hdr;
    Hdr.Height = 2160;
    Hdr.Colorimetry = DtNmos::Colorimetry::Bt2100;
    Hdr.Tcs = DtNmos::Tcs::Pq;
    Hdr.SetDefaults();
    NMOS_ASSERT(Hdr.Colorimetry == DtNmos::Colorimetry::Bt2100);
    NMOS_ASSERT(Hdr.Tcs == DtNmos::Tcs::Pq);
    DtNmos::VideoFormat Unsized;
    Unsized.SetDefaults();
    NMOS_ASSERT(Unsized.Colorimetry == DtNmos::Colorimetry::None);
    NMOS_ASSERT(Unsized.Tcs == DtNmos::Tcs::Sdr);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppSdpRefusesToWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Write() refuses an SDP without flows. It also refuses one whose flow has a
// destination of 300 characters, longer than the C API's field, and names the field.
// Parse() refuses text that is no SDP.
//
NMOS_TEST(CppSdpRefusesToWrite)
{
    DtNmos::Sdp Empty;
    Empty.Session.OriginIp = "192.168.1.10";
    const DtNmos::Expected<std::string> NoFlows = Empty.Write();
    NMOS_ASSERT(!NoFlows.has_value());
    NMOS_ASSERT(NoFlows.error().Code == DtNmos::Result::InvalidArgument);

    DtNmos::Sdp Long = Empty;
    Long.Flows.push_back(Transport(DtNmos::AudioFormat()));
    Long.Flows[0].DestinationIp = std::string(300, 'a');
    const DtNmos::Expected<std::string> TooLong = Long.Write();
    NMOS_ASSERT(!TooLong.has_value());
    NMOS_ASSERT(TooLong.error().Code == DtNmos::Result::InvalidArgument);
    NMOS_ASSERT(TooLong.error().Message.find("DestinationIp") != std::string::npos);

    const DtNmos::Expected<DtNmos::Sdp> NoSdp = DtNmos::Sdp::Parse("no SDP");
    NMOS_ASSERT(!NoSdp.has_value());
    NMOS_ASSERT(!NoSdp.error().Message.empty());
}

NMOS_TEST_MAIN("CppSdp", NMOS_RUN(CppSdpReadsCamera), NMOS_RUN(CppSdpRoundTrips),
               NMOS_RUN(CppSdpGetsMedia), NMOS_RUN(CppSdpTexts), NMOS_RUN(CppSdpDefaults),
               NMOS_RUN(CppSdpRefusesToWrite))
