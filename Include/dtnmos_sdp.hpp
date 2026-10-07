// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_sdp.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The C++ API for reading and writing the SDP of SMPTE ST 2110 streams
//
// SPDX-License-Identifier: BSD-3-Clause
//
// This is the C++ API of dtnmos_sdp.h. An SDP file describes RTP streams. For each
// stream it gives the format, where the stream is sent, and how it is timed.
//
// Sdp::Parse() reads an SDP into an Sdp. An Sdp holds a Session and one Flow per stream.
// Sdp::Write() turns an Sdp back into text. The format of a flow is a std::variant with
// one struct per kind of media: ST 2110-20 video, -30 audio, -22 compressed video and
// -40 ancillary data. Every type here is a value that owns its strings, and is copied
// with =.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dtnmos.hpp"
#include "dtnmos_sdp.h"

namespace DtNmos
{

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Values +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// ST 2110 defines a set of values for each parameter of a stream. Each parameter has an
// enum class here, with the same values as the C enum. Two values have a special meaning
// in every one of them:
// - None (0) means that the SDP does not give the parameter.
// - Other means that the SDP gives a value dtnmos does not know, for example from a
//   later edition of the standard. dtnmos then keeps the parameter, as written, in the
//   OtherParameters of the format, so that it writes the parameter back unchanged.
//
// Text() returns the text that an SDP uses for a value, and "" for None and Other.
// FromText<E>() returns the value for a text: None for "", and Other for a text it does
// not know.
//

// What a stream carries, from the encoding in its a=rtpmap line.
enum class Media : int
{
    None = DTNMOS_MEDIA_NONE,   // Not set; refused where one is needed
    Video = DTNMOS_MEDIA_VIDEO, // ST 2110-20 uncompressed video
    Audio = DTNMOS_MEDIA_AUDIO, // ST 2110-30 and -31 audio
    CompressedVideo = DTNMOS_MEDIA_COMPRESSED_VIDEO, // ST 2110-22 JPEG XS
    Anc = DTNMOS_MEDIA_ANC,                          // ST 2110-40 ancillary data
    Other = DTNMOS_MEDIA_OTHER // Anything else; its encoding and a=fmtp are kept
};

// The sampling parameter (ST 2110-20 §7.4.1): how colour is sampled.
enum class Sampling : int
{
    None = DTNMOS_SAMPLING_NONE,
    Other = DTNMOS_SAMPLING_OTHER,
    YCbCr444 = DTNMOS_SAMPLING_YCBCR_444,     // YCbCr-4:4:4
    YCbCr422 = DTNMOS_SAMPLING_YCBCR_422,     // YCbCr-4:2:2
    YCbCr420 = DTNMOS_SAMPLING_YCBCR_420,     // YCbCr-4:2:0
    ClYCbCr444 = DTNMOS_SAMPLING_CLYCBCR_444, // CLYCbCr-4:4:4, constant luminance
    ClYCbCr422 = DTNMOS_SAMPLING_CLYCBCR_422, // CLYCbCr-4:2:2
    ClYCbCr420 = DTNMOS_SAMPLING_CLYCBCR_420, // CLYCbCr-4:2:0
    ICtCp444 = DTNMOS_SAMPLING_ICTCP_444,     // ICtCp-4:4:4
    ICtCp422 = DTNMOS_SAMPLING_ICTCP_422,     // ICtCp-4:2:2
    ICtCp420 = DTNMOS_SAMPLING_ICTCP_420,     // ICtCp-4:2:0
    Rgb = DTNMOS_SAMPLING_RGB,                // RGB
    Xyz = DTNMOS_SAMPLING_XYZ,                // XYZ
    Key = DTNMOS_SAMPLING_KEY                 // KEY, a key (alpha) signal
};

// The colorimetry parameter (ST 2110-20 §7.5): which colour space.
enum class Colorimetry : int
{
    None = DTNMOS_COLORIMETRY_NONE,
    Other = DTNMOS_COLORIMETRY_OTHER,
    Bt601 = DTNMOS_COLORIMETRY_BT601,
    Bt709 = DTNMOS_COLORIMETRY_BT709,
    Bt2020 = DTNMOS_COLORIMETRY_BT2020,
    Bt2100 = DTNMOS_COLORIMETRY_BT2100,
    St2065_1 = DTNMOS_COLORIMETRY_ST2065_1, // ST2065-1, ACES
    St2065_3 = DTNMOS_COLORIMETRY_ST2065_3, // ST2065-3, ADX
    Unspecified = DTNMOS_COLORIMETRY_UNSPECIFIED,
    Xyz = DTNMOS_COLORIMETRY_XYZ,
    Alpha = DTNMOS_COLORIMETRY_ALPHA // Of a key signal
};

// The TCS parameter (ST 2110-20 §7.6): the transfer characteristic, e.g. SDR or HDR. A
// stream without it is SDR.
enum class Tcs : int
{
    None = DTNMOS_TCS_NONE,
    Other = DTNMOS_TCS_OTHER,
    Sdr = DTNMOS_TCS_SDR,
    Pq = DTNMOS_TCS_PQ,
    Hlg = DTNMOS_TCS_HLG,
    Linear = DTNMOS_TCS_LINEAR,
    Bt2100LinPq = DTNMOS_TCS_BT2100LINPQ,
    Bt2100LinHlg = DTNMOS_TCS_BT2100LINHLG,
    St2065_1 = DTNMOS_TCS_ST2065_1, // ST2065-1
    St428_1 = DTNMOS_TCS_ST428_1,   // ST428-1
    Density = DTNMOS_TCS_DENSITY,
    St2115LogS3 = DTNMOS_TCS_ST2115LOGS3,
    Unspecified = DTNMOS_TCS_UNSPECIFIED
};

// The RANGE parameter (ST 2110-20 §7.3): which code values are used. A stream without it
// is Narrow.
enum class Range : int
{
    None = DTNMOS_RANGE_NONE,
    Other = DTNMOS_RANGE_OTHER,
    Narrow = DTNMOS_RANGE_NARROW,
    FullProtect = DTNMOS_RANGE_FULLPROTECT,
    Full = DTNMOS_RANGE_FULL
};

// The PM parameter (ST 2110-20 §6.3): how video is divided over packets.
enum class PackingMode : int
{
    None = DTNMOS_PACKING_MODE_NONE,
    Other = DTNMOS_PACKING_MODE_OTHER,
    General = DTNMOS_PACKING_MODE_GENERAL, // 2110GPM
    Block = DTNMOS_PACKING_MODE_BLOCK      // 2110BPM
};

// The TP parameter (ST 2110-21 §7.1): how evenly the sender spreads its packets.
enum class TransmitterType : int
{
    None = DTNMOS_TRANSMITTER_TYPE_NONE,
    Other = DTNMOS_TRANSMITTER_TYPE_OTHER,
    Narrow = DTNMOS_TRANSMITTER_TYPE_NARROW,              // 2110TPN
    NarrowLinear = DTNMOS_TRANSMITTER_TYPE_NARROW_LINEAR, // 2110TPNL
    Wide = DTNMOS_TRANSMITTER_TYPE_WIDE                   // 2110TPW
};

// The encoding of an audio stream (ST 2110-30 and -31).
enum class AudioEncoding : int
{
    None = DTNMOS_AUDIO_ENCODING_NONE,
    Other = DTNMOS_AUDIO_ENCODING_OTHER,
    L16 = DTNMOS_AUDIO_ENCODING_L16,
    L24 = DTNMOS_AUDIO_ENCODING_L24,
    Am824 = DTNMOS_AUDIO_ENCODING_AM824
};

// The kind of clock a stream's timestamps follow (a=ts-refclk, RFC 7273, ST 2110-10).
enum class RefClockKind : int
{
    None = DTNMOS_REFCLOCK_NONE,         // No a=ts-refclk
    Ptp = DTNMOS_REFCLOCK_PTP,           // A PTP grandmaster: ptp=<version>:<grandmaster>
    LocalMac = DTNMOS_REFCLOCK_LOCALMAC, // The sender's own clock: localmac=<MAC address>
    Other = DTNMOS_REFCLOCK_OTHER        // Another kind, kept as text
};

// Returns the value that Text, as an SDP writes it, stands for. It returns None for "",
// and Other for a text it does not know. E is one of AudioEncoding, Colorimetry,
// PackingMode, Range, Sampling, Tcs and TransmitterType.
template <typename E> E FromText(std::string_view Text);

// Returns a name for Kind to use in messages, e.g. "video".
std::string_view Name(Media Kind);

// Return the text that an SDP uses for Value, or "" for None and Other.
std::string_view Text(AudioEncoding Value);
std::string_view Text(Colorimetry Value);
std::string_view Text(PackingMode Value);
std::string_view Text(Range Value);
std::string_view Text(Sampling Value);
std::string_view Text(Tcs Value);
std::string_view Text(TransmitterType Value);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Formats +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Each kind of media has a struct for the format of its streams. A parameter that the SDP
// does not give is 0, None or "".
//
// OtherParameters holds the parameters that dtnmos does not know, and the known ones
// whose value it does not know. They are kept as the SDP wrote them, separated by "; ",
// e.g. "TCS=ST2115LOGS9; TROFF=37". Sdp::Write() writes them after the other parameters,
// unchanged. To write a value that dtnmos does not know, set the field to Other and put
// the parameter in OtherParameters.
//

// The format of an ST 2110-20 video stream, from its a=fmtp line.
struct VideoFormat
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t RateNumerator = 0; // Frame rate (exactframerate), e.g. 30000/1001 or 25/1
    uint32_t RateDenominator = 0;
    bool Interlaced = false; // Interlaced video (interlace)
    bool Segmented = false;  // Progressive frames sent as two fields, PsF (segmented)
    uint32_t Depth = 0;      // Bits per sample
    DtNmos::Sampling Sampling = DtNmos::Sampling::None;
    DtNmos::Colorimetry Colorimetry = DtNmos::Colorimetry::None;
    DtNmos::Tcs Tcs = DtNmos::Tcs::None;
    DtNmos::Range Range = DtNmos::Range::None;
    DtNmos::PackingMode PackingMode = DtNmos::PackingMode::None;
    DtNmos::TransmitterType TransmitterType = DtNmos::TransmitterType::None;
    std::string Ssn; // The edition of the standard (SSN), e.g. "ST2110-20:2017"
    std::string OtherParameters;

    // Fills in Colorimetry and Tcs when they are None, with the usual values for the
    // Height of the picture. Colorimetry becomes Bt601 up to 576 lines (SD), Bt709 up to
    // 1080 lines (HD), and Bt2020 above that (UHD). Tcs becomes Sdr. With a Height of 0,
    // Colorimetry stays None.
    //
    // The picture size does not tell whether a stream is HDR. For Pq or Hlg with Bt2100,
    // set Tcs and Colorimetry yourself. Range stays None, which means Narrow.
    void SetDefaults();

    friend bool operator==(const VideoFormat&, const VideoFormat&) = default;
};

// The format of an ST 2110-30 or -31 audio stream. The SDP gives it in its a=rtpmap,
// a=ptime and a=fmtp lines. dtnmos takes a stream as audio only when it has one of the
// encodings of AudioEncoding, so Encoding is never Other.
struct AudioFormat
{
    AudioEncoding Encoding = AudioEncoding::None;
    uint32_t SampleRate = 0;   // e.g. 48000
    uint32_t Channels = 0;     // Channels per packet
    uint32_t PacketTimeNs = 0; // Time per packet (a=ptime): 1000000 for 1 ms
    std::string ChannelOrder;  // channel-order, e.g. "SMPTE2110.(ST,ST)"
    std::string OtherParameters;

    friend bool operator==(const AudioFormat&, const AudioFormat&) = default;
};

// The format of an ST 2110-22 compressed video stream. It has the picture parameters
// that ST 2110-20 video has, the parameters of the codec, and the bandwidth (b=AS). For
// JPEG XS, the parameters of the codec are profile, level, sublevel, packetmode and
// transmode.
struct CompressedVideoFormat
{
    std::string Encoding; // The codec, e.g. "jxsv"
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t RateNumerator = 0;
    uint32_t RateDenominator = 0;
    bool Interlaced = false;
    bool Segmented = false;
    uint32_t Depth = 0;
    DtNmos::Sampling Sampling = DtNmos::Sampling::None;
    DtNmos::Colorimetry Colorimetry = DtNmos::Colorimetry::None;
    DtNmos::Tcs Tcs = DtNmos::Tcs::None;
    DtNmos::Range Range = DtNmos::Range::None;
    DtNmos::TransmitterType TransmitterType = DtNmos::TransmitterType::None;
    std::string Ssn;
    std::string Profile;
    std::string Level;
    std::string Sublevel;
    uint32_t PacketMode = 0;       // packetmode
    uint32_t TransmissionMode = 0; // transmode; Parse() gives 1 when the SDP has none
    uint64_t BandwidthKbps = 0;    // b=AS; 0 when not given
    std::string OtherParameters;

    friend bool operator==(const CompressedVideoFormat&,
                           const CompressedVideoFormat&) = default;
};

// One kind of ancillary data packet: its DID and SDID.
struct DidSdid
{
    uint8_t Did = 0;
    uint8_t Sdid = 0;

    friend bool operator==(const DidSdid&, const DidSdid&) = default;
};

// The format of an ST 2110-40 ancillary data stream, from its a=fmtp line.
struct AncFormat
{
    std::vector<DtNmos::DidSdid> DidSdid; // The kinds of packet the stream carries
    uint32_t VpidCode = 0;                // VPID_Code; 0 when not given
    uint32_t RateNumerator = 0;           // exactframerate; 0 when not given
    uint32_t RateDenominator = 0;
    std::string TransmissionModel; // TM, e.g. "CTM"
    std::string Ssn;

    friend bool operator==(const AncFormat&, const AncFormat&) = default;
};

// The format of a stream of a kind that dtnmos does not know. It keeps the encoding and
// the a=fmtp line as the SDP wrote them.
struct OtherFormat
{
    std::string Encoding;            // The encoding of the a=rtpmap line
    std::optional<std::string> Fmtp; // The parameters of a=fmtp; none without that line

    friend bool operator==(const OtherFormat&, const OtherFormat&) = default;
};

// The format of a flow. The struct it holds tells the media of the flow. std::monostate
// means that the flow has no format, which is Media::None. A function that needs a
// format refuses a flow without one.
using FlowFormat = std::variant<std::monostate, VideoFormat, AudioFormat,
                                CompressedVideoFormat, AncFormat, OtherFormat>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The clock that the timestamps of a stream follow (a=ts-refclk). Parse() reads a PTP
// domain in both spellings, ":<number>" (ST 2110-10) and ":domain-nmbr=<number>" (RFC
// 7273). Write() writes the first.
struct RefClock
{
    RefClockKind Kind = RefClockKind::None;
    std::string PtpVersion;  // For Ptp: the version, e.g. "IEEE1588-2008"
    std::string Grandmaster; // For Ptp: the EUI-64 of the grandmaster; "" when Traceable
    bool Traceable = false;  // For Ptp: traceable to TAI, without a grandmaster named
    int Domain = -1;         // For Ptp: the domain, 0 to 127; -1 when the SDP gives none
    std::string LocalMac;    // For LocalMac: the MAC address of the sender
    std::string Text;        // For Other: the value of the line, as written

    friend bool operator==(const RefClock&, const RefClock&) = default;
};

// One RTP stream, as one media section of an SDP describes it.
struct Flow
{
    // The address the stream is sent to (c=), without TTL. It may be a host name.
    std::string DestinationIp;
    uint16_t DestinationPort = 0; // The UDP port (m=)
    // The only source that a receiver takes the stream from; "" for any source.
    std::string SourceIp;
    uint8_t PayloadType = 0; // The RTP payload type, the first one in m=
    // The RTP clock rate (a=rtpmap): 90000 for video, the sample rate for audio.
    uint32_t ClockRate = 0;
    DtNmos::RefClock RefClock;     // The clock the timestamps follow (a=ts-refclk)
    bool MediaClockDirect = false; // True when the SDP has a=mediaclk:direct=<offset>
    uint32_t MediaClockOffset = 0; // The offset of a=mediaclk:direct
    // 0, or 1 for the second path of an ST 2022-7 pair (a=group:DUP).
    uint32_t Leg = 0;
    FlowFormat Format; // The format; its struct tells the media

    // Returns the media of the flow, which the struct in Format tells.
    Media GetMedia() const;

    friend bool operator==(const Flow&, const Flow&) = default;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The session part of an SDP, which all its streams share.
struct Session
{
    std::string Name;            // The name of the session (s=)
    std::string OriginIp;        // The address or host name of the sender (o=)
    uint64_t SessionId = 0;      // The session ID (o=)
    uint64_t SessionVersion = 0; // The version (o=); increase it when the SDP changes

    friend bool operator==(const Session&, const Session&) = default;
};

// An SDP, with its session and its flows.
struct Sdp
{
    // Reads the SDP in Text. Fails with:
    //   Result::Parse            the SDP is malformed, or a value is longer than its
    //                            field; the message names the line
    //   Result::InvalidArgument  the SDP has no media sections
    [[nodiscard]] static Expected<Sdp> Parse(std::string_view Text);

    DtNmos::Session Session; // The session part
    std::vector<Flow> Flows; // One per media section, in the order of the SDP

    // Writes the SDP as text, and returns it.
    //
    // A flow with Leg 1 is the second path of the flow with Leg 0 just before it, and the
    // two are written as an ST 2022-7 pair. An IPv4 multicast destination gets a TTL of
    // 64. Fails with Result::InvalidArgument when:
    // - there are no flows, or the session has no OriginIp;
    // - a flow has no format, no destination, or no audio encoding;
    // - a flow with Leg 1 has no flow with Leg 0 before it;
    // - a text is longer than the C API can hold.
    [[nodiscard]] Expected<std::string> Write() const;

    friend bool operator==(const Sdp&, const Sdp&) = default;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Not for programs: what the C++ headers use to convert the types of an SDP to and from
// those of the C API.
//

namespace Detail
{

// Convert an enum of the C API to the enum class with the same values. Each value of the
// C enum has a case, so that the compiler warns about a value that the conversion
// misses. A value from a newer library is passed on as it is.
inline Media FromNative(DtNmosMedia Native)
{
    switch (Native)
    {
    case DTNMOS_MEDIA_NONE:
        return Media::None;
    case DTNMOS_MEDIA_VIDEO:
        return Media::Video;
    case DTNMOS_MEDIA_AUDIO:
        return Media::Audio;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return Media::CompressedVideo;
    case DTNMOS_MEDIA_ANC:
        return Media::Anc;
    case DTNMOS_MEDIA_OTHER:
        return Media::Other;
    }
    return static_cast<Media>(Native);
}

inline Sampling FromNative(DtNmosSampling Native)
{
    switch (Native)
    {
    case DTNMOS_SAMPLING_NONE:
        return Sampling::None;
    case DTNMOS_SAMPLING_OTHER:
        return Sampling::Other;
    case DTNMOS_SAMPLING_YCBCR_444:
        return Sampling::YCbCr444;
    case DTNMOS_SAMPLING_YCBCR_422:
        return Sampling::YCbCr422;
    case DTNMOS_SAMPLING_YCBCR_420:
        return Sampling::YCbCr420;
    case DTNMOS_SAMPLING_CLYCBCR_444:
        return Sampling::ClYCbCr444;
    case DTNMOS_SAMPLING_CLYCBCR_422:
        return Sampling::ClYCbCr422;
    case DTNMOS_SAMPLING_CLYCBCR_420:
        return Sampling::ClYCbCr420;
    case DTNMOS_SAMPLING_ICTCP_444:
        return Sampling::ICtCp444;
    case DTNMOS_SAMPLING_ICTCP_422:
        return Sampling::ICtCp422;
    case DTNMOS_SAMPLING_ICTCP_420:
        return Sampling::ICtCp420;
    case DTNMOS_SAMPLING_RGB:
        return Sampling::Rgb;
    case DTNMOS_SAMPLING_XYZ:
        return Sampling::Xyz;
    case DTNMOS_SAMPLING_KEY:
        return Sampling::Key;
    }
    return static_cast<Sampling>(Native);
}

inline Colorimetry FromNative(DtNmosColorimetry Native)
{
    switch (Native)
    {
    case DTNMOS_COLORIMETRY_NONE:
        return Colorimetry::None;
    case DTNMOS_COLORIMETRY_OTHER:
        return Colorimetry::Other;
    case DTNMOS_COLORIMETRY_BT601:
        return Colorimetry::Bt601;
    case DTNMOS_COLORIMETRY_BT709:
        return Colorimetry::Bt709;
    case DTNMOS_COLORIMETRY_BT2020:
        return Colorimetry::Bt2020;
    case DTNMOS_COLORIMETRY_BT2100:
        return Colorimetry::Bt2100;
    case DTNMOS_COLORIMETRY_ST2065_1:
        return Colorimetry::St2065_1;
    case DTNMOS_COLORIMETRY_ST2065_3:
        return Colorimetry::St2065_3;
    case DTNMOS_COLORIMETRY_UNSPECIFIED:
        return Colorimetry::Unspecified;
    case DTNMOS_COLORIMETRY_XYZ:
        return Colorimetry::Xyz;
    case DTNMOS_COLORIMETRY_ALPHA:
        return Colorimetry::Alpha;
    }
    return static_cast<Colorimetry>(Native);
}

inline Tcs FromNative(DtNmosTcs Native)
{
    switch (Native)
    {
    case DTNMOS_TCS_NONE:
        return Tcs::None;
    case DTNMOS_TCS_OTHER:
        return Tcs::Other;
    case DTNMOS_TCS_SDR:
        return Tcs::Sdr;
    case DTNMOS_TCS_PQ:
        return Tcs::Pq;
    case DTNMOS_TCS_HLG:
        return Tcs::Hlg;
    case DTNMOS_TCS_LINEAR:
        return Tcs::Linear;
    case DTNMOS_TCS_BT2100LINPQ:
        return Tcs::Bt2100LinPq;
    case DTNMOS_TCS_BT2100LINHLG:
        return Tcs::Bt2100LinHlg;
    case DTNMOS_TCS_ST2065_1:
        return Tcs::St2065_1;
    case DTNMOS_TCS_ST428_1:
        return Tcs::St428_1;
    case DTNMOS_TCS_DENSITY:
        return Tcs::Density;
    case DTNMOS_TCS_ST2115LOGS3:
        return Tcs::St2115LogS3;
    case DTNMOS_TCS_UNSPECIFIED:
        return Tcs::Unspecified;
    }
    return static_cast<Tcs>(Native);
}

inline Range FromNative(DtNmosRange Native)
{
    switch (Native)
    {
    case DTNMOS_RANGE_NONE:
        return Range::None;
    case DTNMOS_RANGE_OTHER:
        return Range::Other;
    case DTNMOS_RANGE_NARROW:
        return Range::Narrow;
    case DTNMOS_RANGE_FULLPROTECT:
        return Range::FullProtect;
    case DTNMOS_RANGE_FULL:
        return Range::Full;
    }
    return static_cast<Range>(Native);
}

inline PackingMode FromNative(DtNmosPackingMode Native)
{
    switch (Native)
    {
    case DTNMOS_PACKING_MODE_NONE:
        return PackingMode::None;
    case DTNMOS_PACKING_MODE_OTHER:
        return PackingMode::Other;
    case DTNMOS_PACKING_MODE_GENERAL:
        return PackingMode::General;
    case DTNMOS_PACKING_MODE_BLOCK:
        return PackingMode::Block;
    }
    return static_cast<PackingMode>(Native);
}

inline TransmitterType FromNative(DtNmosTransmitterType Native)
{
    switch (Native)
    {
    case DTNMOS_TRANSMITTER_TYPE_NONE:
        return TransmitterType::None;
    case DTNMOS_TRANSMITTER_TYPE_OTHER:
        return TransmitterType::Other;
    case DTNMOS_TRANSMITTER_TYPE_NARROW:
        return TransmitterType::Narrow;
    case DTNMOS_TRANSMITTER_TYPE_NARROW_LINEAR:
        return TransmitterType::NarrowLinear;
    case DTNMOS_TRANSMITTER_TYPE_WIDE:
        return TransmitterType::Wide;
    }
    return static_cast<TransmitterType>(Native);
}

inline AudioEncoding FromNative(DtNmosAudioEncoding Native)
{
    switch (Native)
    {
    case DTNMOS_AUDIO_ENCODING_NONE:
        return AudioEncoding::None;
    case DTNMOS_AUDIO_ENCODING_OTHER:
        return AudioEncoding::Other;
    case DTNMOS_AUDIO_ENCODING_L16:
        return AudioEncoding::L16;
    case DTNMOS_AUDIO_ENCODING_L24:
        return AudioEncoding::L24;
    case DTNMOS_AUDIO_ENCODING_AM824:
        return AudioEncoding::Am824;
    }
    return static_cast<AudioEncoding>(Native);
}

inline RefClockKind FromNative(DtNmosRefClockKind Native)
{
    switch (Native)
    {
    case DTNMOS_REFCLOCK_NONE:
        return RefClockKind::None;
    case DTNMOS_REFCLOCK_PTP:
        return RefClockKind::Ptp;
    case DTNMOS_REFCLOCK_LOCALMAC:
        return RefClockKind::LocalMac;
    case DTNMOS_REFCLOCK_OTHER:
        return RefClockKind::Other;
    }
    return static_cast<RefClockKind>(Native);
}

// Convert an enum class to the enum of the C API with the same values.
inline DtNmosMedia ToNative(Media Value)
{
    return static_cast<DtNmosMedia>(Value);
}

inline DtNmosSampling ToNative(Sampling Value)
{
    return static_cast<DtNmosSampling>(Value);
}

inline DtNmosColorimetry ToNative(Colorimetry Value)
{
    return static_cast<DtNmosColorimetry>(Value);
}

inline DtNmosTcs ToNative(Tcs Value)
{
    return static_cast<DtNmosTcs>(Value);
}

inline DtNmosRange ToNative(Range Value)
{
    return static_cast<DtNmosRange>(Value);
}

inline DtNmosPackingMode ToNative(PackingMode Value)
{
    return static_cast<DtNmosPackingMode>(Value);
}

inline DtNmosTransmitterType ToNative(TransmitterType Value)
{
    return static_cast<DtNmosTransmitterType>(Value);
}

inline DtNmosAudioEncoding ToNative(AudioEncoding Value)
{
    return static_cast<DtNmosAudioEncoding>(Value);
}

inline DtNmosRefClockKind ToNative(RefClockKind Value)
{
    return static_cast<DtNmosRefClockKind>(Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromNative -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Convert a struct of the C API to the C++ type. Each conversion asserts which field it
// knows to be the last one of the C struct, so that a field the C API adds fails the
// build until the conversion handles it.
//
inline VideoFormat FromNative(const DtNmosVideoFormat& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosVideoFormat, OtherParameters);
    VideoFormat Value;
    Value.Width = Native.Width;
    Value.Height = Native.Height;
    Value.RateNumerator = Native.RateNumerator;
    Value.RateDenominator = Native.RateDenominator;
    Value.Interlaced = Native.Interlaced;
    Value.Segmented = Native.Segmented;
    Value.Depth = Native.Depth;
    Value.Sampling = FromNative(Native.Sampling);
    Value.Colorimetry = FromNative(Native.Colorimetry);
    Value.Tcs = FromNative(Native.Tcs);
    Value.Range = FromNative(Native.Range);
    Value.PackingMode = FromNative(Native.PackingMode);
    Value.TransmitterType = FromNative(Native.TransmitterType);
    Value.Ssn = FromArray(Native.Ssn);
    Value.OtherParameters = FromNative(Native.OtherParameters);
    return Value;
}

inline AudioFormat FromNative(const DtNmosAudioFormat& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosAudioFormat, OtherParameters);
    AudioFormat Value;
    Value.Encoding = FromNative(Native.Encoding);
    Value.SampleRate = Native.SampleRate;
    Value.Channels = Native.Channels;
    Value.PacketTimeNs = Native.PacketTimeNs;
    Value.ChannelOrder = FromNative(Native.ChannelOrder);
    Value.OtherParameters = FromNative(Native.OtherParameters);
    return Value;
}

inline CompressedVideoFormat FromNative(const DtNmosCompressedVideoFormat& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosCompressedVideoFormat, OtherParameters);
    CompressedVideoFormat Value;
    Value.Encoding = FromArray(Native.Encoding);
    Value.Width = Native.Width;
    Value.Height = Native.Height;
    Value.RateNumerator = Native.RateNumerator;
    Value.RateDenominator = Native.RateDenominator;
    Value.Interlaced = Native.Interlaced;
    Value.Segmented = Native.Segmented;
    Value.Depth = Native.Depth;
    Value.Sampling = FromNative(Native.Sampling);
    Value.Colorimetry = FromNative(Native.Colorimetry);
    Value.Tcs = FromNative(Native.Tcs);
    Value.Range = FromNative(Native.Range);
    Value.TransmitterType = FromNative(Native.TransmitterType);
    Value.Ssn = FromArray(Native.Ssn);
    Value.Profile = FromArray(Native.Profile);
    Value.Level = FromArray(Native.Level);
    Value.Sublevel = FromArray(Native.Sublevel);
    Value.PacketMode = Native.PacketMode;
    Value.TransmissionMode = Native.TransmissionMode;
    Value.BandwidthKbps = Native.BandwidthKbps;
    Value.OtherParameters = FromNative(Native.OtherParameters);
    return Value;
}

inline AncFormat FromNative(const DtNmosAncFormat& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosAncFormat, Ssn);
    DTNMOS_DETAIL_LAST_FIELD(DtNmosDidSdid, Sdid);
    AncFormat Value;
    for (std::size_t i = 0; Native.DidSdid != nullptr && i < Native.DidSdidCount; ++i)
    {
        Value.DidSdid.push_back({Native.DidSdid[i].Did, Native.DidSdid[i].Sdid});
    }
    Value.VpidCode = Native.VpidCode;
    Value.RateNumerator = Native.RateNumerator;
    Value.RateDenominator = Native.RateDenominator;
    Value.TransmissionModel = FromArray(Native.TransmissionModel);
    Value.Ssn = FromArray(Native.Ssn);
    return Value;
}

inline OtherFormat FromNative(const DtNmosOtherFormat& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosOtherFormat, Fmtp);
    OtherFormat Value;
    Value.Encoding = FromArray(Native.Encoding);
    if (Native.Fmtp != nullptr)
    {
        Value.Fmtp = std::string(Native.Fmtp);
    }
    return Value;
}

inline RefClock FromNative(const DtNmosRefClock& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosRefClock, Text);
    RefClock Value;
    Value.Kind = FromNative(Native.Kind);
    Value.PtpVersion = FromArray(Native.PtpVersion);
    Value.Grandmaster = FromArray(Native.Grandmaster);
    Value.Traceable = Native.Traceable;
    Value.Domain = Native.Domain;
    Value.LocalMac = FromArray(Native.LocalMac);
    Value.Text = FromNative(Native.Text);
    return Value;
}

inline Flow FromNative(const DtNmosFlow& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosFlow, Format);
    Flow Value;
    Value.DestinationIp = FromArray(Native.DestinationIp);
    Value.DestinationPort = Native.DestinationPort;
    Value.SourceIp = FromArray(Native.SourceIp);
    Value.PayloadType = Native.PayloadType;
    Value.ClockRate = Native.ClockRate;
    Value.RefClock = FromNative(Native.RefClock);
    Value.MediaClockDirect = Native.MediaClockDirect;
    Value.MediaClockOffset = Native.MediaClockOffset;
    Value.Leg = Native.Leg;
    switch (Native.Media)
    {
    case DTNMOS_MEDIA_NONE:
        break;
    case DTNMOS_MEDIA_VIDEO:
        Value.Format = FromNative(Native.Format.Video);
        break;
    case DTNMOS_MEDIA_AUDIO:
        Value.Format = FromNative(Native.Format.Audio);
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        Value.Format = FromNative(Native.Format.CompressedVideo);
        break;
    case DTNMOS_MEDIA_ANC:
        Value.Format = FromNative(Native.Format.Anc);
        break;
    case DTNMOS_MEDIA_OTHER:
        Value.Format = FromNative(Native.Format.Other);
        break;
    }
    return Value;
}

inline Session FromNative(const DtNmosSession& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosSession, SessionVersion);
    Session Value;
    Value.Name = FromNative(Native.Name);
    Value.OriginIp = FromArray(Native.OriginIp);
    Value.SessionId = Native.SessionId;
    Value.SessionVersion = Native.SessionVersion;
    return Value;
}

// Holds a Flow as the C API takes it: a DtNmosFlow, and the texts and the list that its
// pointer fields point to. It cannot be copied or moved, because the pointers would then
// point into the old object.
class NativeFlow
{
  public:
    NativeFlow() = default;
    NativeFlow(const NativeFlow&) = delete;
    NativeFlow& operator=(const NativeFlow&) = delete;

    // Returns the C flow. It stays valid while this object lives, until the next Set().
    const DtNmosFlow& Get() const { return Native; }

    // Fills the C flow from Value. Fails with Result::InvalidArgument when a text of
    // Value is longer than the C field can hold.
    [[nodiscard]] Status Set(const Flow& Value);

  private:
    // Fills the format of the C flow from the format of Value.
    [[nodiscard]] Status SetFormat(const Flow& Value);

    DtNmosFlow Native{}; // The C flow
    // The texts that the pointer fields of Native point to.
    std::string OtherParameters;
    std::string ChannelOrder;
    std::string Fmtp;
    std::string RefClockText;
    std::vector<DtNmosDidSdid> Pairs; // The list that Native.Format.Anc.DidSdid points to
};

// Holds a Session as the C API takes it, in the way that NativeFlow holds a Flow.
class NativeSession
{
  public:
    NativeSession() = default;
    NativeSession(const NativeSession&) = delete;
    NativeSession& operator=(const NativeSession&) = delete;

    // Returns the C session. It stays valid while this object lives, until the next
    // Set().
    const DtNmosSession& Get() const { return Native; }

    // Fills the C session from Value. Fails with Result::InvalidArgument when the
    // OriginIp of Value is longer than the C field can hold.
    [[nodiscard]] Status Set(const DtNmos::Session& Value);

  private:
    DtNmosSession Native{};
    std::string Name;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NativeFlow::Set -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Status NativeFlow::Set(const Flow& Value)
{
    Native = DtNmosFlow{};
    Native.Size = sizeof(Native);
    Native.Media = ToNative(Value.GetMedia());
    Status Copied = CopyText(Native.DestinationIp, Value.DestinationIp, "DestinationIp");
    Native.DestinationPort = Value.DestinationPort;
    if (Copied)
    {
        Copied = CopyText(Native.SourceIp, Value.SourceIp, "SourceIp");
    }
    Native.PayloadType = Value.PayloadType;
    Native.ClockRate = Value.ClockRate;
    const RefClock& Clock = Value.RefClock;
    DtNmosRefClock& NativeClock = Native.RefClock;
    NativeClock.Kind = ToNative(Clock.Kind);
    if (Copied)
    {
        Copied =
            CopyText(NativeClock.PtpVersion, Clock.PtpVersion, "RefClock.PtpVersion");
    }
    if (Copied)
    {
        Copied =
            CopyText(NativeClock.Grandmaster, Clock.Grandmaster, "RefClock.Grandmaster");
    }
    NativeClock.Traceable = Clock.Traceable;
    NativeClock.Domain = Clock.Domain;
    if (Copied)
    {
        Copied = CopyText(NativeClock.LocalMac, Clock.LocalMac, "RefClock.LocalMac");
    }
    RefClockText = Clock.Text;
    NativeClock.Text = NullIfEmpty(RefClockText);
    Native.MediaClockDirect = Value.MediaClockDirect;
    Native.MediaClockOffset = Value.MediaClockOffset;
    Native.Leg = Value.Leg;
    return Copied ? SetFormat(Value) : Copied;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NativeFlow::SetFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Status NativeFlow::SetFormat(const Flow& Value)
{
    if (const VideoFormat* Video = std::get_if<VideoFormat>(&Value.Format))
    {
        DtNmosVideoFormat& v = Native.Format.Video;
        v.Width = Video->Width;
        v.Height = Video->Height;
        v.RateNumerator = Video->RateNumerator;
        v.RateDenominator = Video->RateDenominator;
        v.Interlaced = Video->Interlaced;
        v.Segmented = Video->Segmented;
        v.Depth = Video->Depth;
        v.Sampling = ToNative(Video->Sampling);
        v.Colorimetry = ToNative(Video->Colorimetry);
        v.Tcs = ToNative(Video->Tcs);
        v.Range = ToNative(Video->Range);
        v.PackingMode = ToNative(Video->PackingMode);
        v.TransmitterType = ToNative(Video->TransmitterType);
        OtherParameters = Video->OtherParameters;
        v.OtherParameters = NullIfEmpty(OtherParameters);
        return CopyText(v.Ssn, Video->Ssn, "VideoFormat.Ssn");
    }
    if (const AudioFormat* Audio = std::get_if<AudioFormat>(&Value.Format))
    {
        DtNmosAudioFormat& a = Native.Format.Audio;
        a.Encoding = ToNative(Audio->Encoding);
        a.SampleRate = Audio->SampleRate;
        a.Channels = Audio->Channels;
        a.PacketTimeNs = Audio->PacketTimeNs;
        ChannelOrder = Audio->ChannelOrder;
        a.ChannelOrder = NullIfEmpty(ChannelOrder);
        OtherParameters = Audio->OtherParameters;
        a.OtherParameters = NullIfEmpty(OtherParameters);
        return {};
    }
    if (const CompressedVideoFormat* Compressed =
            std::get_if<CompressedVideoFormat>(&Value.Format))
    {
        DtNmosCompressedVideoFormat& c = Native.Format.CompressedVideo;
        c.Width = Compressed->Width;
        c.Height = Compressed->Height;
        c.RateNumerator = Compressed->RateNumerator;
        c.RateDenominator = Compressed->RateDenominator;
        c.Interlaced = Compressed->Interlaced;
        c.Segmented = Compressed->Segmented;
        c.Depth = Compressed->Depth;
        c.Sampling = ToNative(Compressed->Sampling);
        c.Colorimetry = ToNative(Compressed->Colorimetry);
        c.Tcs = ToNative(Compressed->Tcs);
        c.Range = ToNative(Compressed->Range);
        c.TransmitterType = ToNative(Compressed->TransmitterType);
        c.PacketMode = Compressed->PacketMode;
        c.TransmissionMode = Compressed->TransmissionMode;
        c.BandwidthKbps = Compressed->BandwidthKbps;
        OtherParameters = Compressed->OtherParameters;
        c.OtherParameters = NullIfEmpty(OtherParameters);
        Status Copied =
            CopyText(c.Encoding, Compressed->Encoding, "CompressedVideoFormat.Encoding");
        if (Copied)
        {
            Copied = CopyText(c.Ssn, Compressed->Ssn, "CompressedVideoFormat.Ssn");
        }
        if (Copied)
        {
            Copied =
                CopyText(c.Profile, Compressed->Profile, "CompressedVideoFormat.Profile");
        }
        if (Copied)
        {
            Copied = CopyText(c.Level, Compressed->Level, "CompressedVideoFormat.Level");
        }
        if (Copied)
        {
            Copied = CopyText(c.Sublevel, Compressed->Sublevel,
                              "CompressedVideoFormat.Sublevel");
        }
        return Copied;
    }
    if (const AncFormat* Anc = std::get_if<AncFormat>(&Value.Format))
    {
        DtNmosAncFormat& n = Native.Format.Anc;
        Pairs.clear();
        for (const DtNmos::DidSdid& Pair : Anc->DidSdid)
        {
            Pairs.push_back({Pair.Did, Pair.Sdid});
        }
        n.DidSdid = Pairs.empty() ? nullptr : Pairs.data();
        n.DidSdidCount = Pairs.size();
        n.VpidCode = Anc->VpidCode;
        n.RateNumerator = Anc->RateNumerator;
        n.RateDenominator = Anc->RateDenominator;
        Status Copied = CopyText(n.TransmissionModel, Anc->TransmissionModel,
                                 "AncFormat.TransmissionModel");
        if (Copied)
        {
            Copied = CopyText(n.Ssn, Anc->Ssn, "AncFormat.Ssn");
        }
        return Copied;
    }
    if (const OtherFormat* Other = std::get_if<OtherFormat>(&Value.Format))
    {
        DtNmosOtherFormat& o = Native.Format.Other;
        Fmtp = Other->Fmtp.value_or("");
        o.Fmtp = Other->Fmtp.has_value() ? Fmtp.c_str() : nullptr;
        return CopyText(o.Encoding, Other->Encoding, "OtherFormat.Encoding");
    }
    return {};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NativeSession::Set -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Status NativeSession::Set(const DtNmos::Session& Value)
{
    Native = DtNmosSession{};
    Native.Size = sizeof(Native);
    Name = Value.Name;
    Native.Name = NullIfEmpty(Name);
    Native.SessionId = Value.SessionId;
    Native.SessionVersion = Value.SessionVersion;
    return CopyText(Native.OriginIp, Value.OriginIp, "Session.OriginIp");
}

// Converts an SDP that the C API parsed into an Sdp. The caller still owns Native.
inline Sdp FromNative(const DtNmosSdp* Native)
{
    Sdp Value;
    Value.Session = FromNative(*DtNmosSdp_Session(Native));
    for (std::size_t i = 0; i < DtNmosSdp_FlowCount(Native); ++i)
    {
        Value.Flows.push_back(FromNative(*DtNmosSdp_Flow(Native, i)));
    }
    return Value;
}

// Frees a DtNmosSdp; the deleter of a std::unique_ptr.
struct SdpFree
{
    void operator()(DtNmosSdp* Sdp) const { DtNmosSdp_Free(Sdp); }
};

} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C functions need a text that ends with a null, and a std::string_view may not have
// one, so Text is copied into a std::string first.
//
template <> inline AudioEncoding FromText<AudioEncoding>(std::string_view Text)
{
    return Detail::FromNative(DtNmosAudioEncoding_FromText(std::string(Text).c_str()));
}

template <> inline Colorimetry FromText<Colorimetry>(std::string_view Text)
{
    return Detail::FromNative(DtNmosColorimetry_FromText(std::string(Text).c_str()));
}

template <> inline PackingMode FromText<PackingMode>(std::string_view Text)
{
    return Detail::FromNative(DtNmosPackingMode_FromText(std::string(Text).c_str()));
}

template <> inline Range FromText<Range>(std::string_view Text)
{
    return Detail::FromNative(DtNmosRange_FromText(std::string(Text).c_str()));
}

template <> inline Sampling FromText<Sampling>(std::string_view Text)
{
    return Detail::FromNative(DtNmosSampling_FromText(std::string(Text).c_str()));
}

template <> inline Tcs FromText<Tcs>(std::string_view Text)
{
    return Detail::FromNative(DtNmosTcs_FromText(std::string(Text).c_str()));
}

template <> inline TransmitterType FromText<TransmitterType>(std::string_view Text)
{
    return Detail::FromNative(DtNmosTransmitterType_FromText(std::string(Text).c_str()));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline std::string_view Name(Media Kind)
{
    return DtNmosMedia_Name(Detail::ToNative(Kind));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline std::string_view Text(AudioEncoding Value)
{
    return DtNmosAudioEncoding_Text(Detail::ToNative(Value));
}

inline std::string_view Text(Colorimetry Value)
{
    return DtNmosColorimetry_Text(Detail::ToNative(Value));
}

inline std::string_view Text(PackingMode Value)
{
    return DtNmosPackingMode_Text(Detail::ToNative(Value));
}

inline std::string_view Text(Range Value)
{
    return DtNmosRange_Text(Detail::ToNative(Value));
}

inline std::string_view Text(Sampling Value)
{
    return DtNmosSampling_Text(Detail::ToNative(Value));
}

inline std::string_view Text(Tcs Value)
{
    return DtNmosTcs_Text(Detail::ToNative(Value));
}

inline std::string_view Text(TransmitterType Value)
{
    return DtNmosTransmitterType_Text(Detail::ToNative(Value));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- VideoFormat::SetDefaults -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C function reads only Height, Colorimetry and Tcs, and changes only the last two,
// so only those are copied.
//
inline void VideoFormat::SetDefaults()
{
    DtNmosVideoFormat Native{};
    Native.Height = Height;
    Native.Colorimetry = Detail::ToNative(Colorimetry);
    Native.Tcs = Detail::ToNative(Tcs);
    DtNmosVideoFormat_SetDefaults(&Native);
    Colorimetry = Detail::FromNative(Native.Colorimetry);
    Tcs = Detail::FromNative(Native.Tcs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Flow::GetMedia -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The types in FlowFormat come in the same order as the values of Media, so the index
// of the type in the variant picks the media.
//
inline Media Flow::GetMedia() const
{
    static constexpr Media Kinds[] = {Media::None,  Media::Video,
                                      Media::Audio, Media::CompressedVideo,
                                      Media::Anc,   Media::Other};
    static_assert(std::size(Kinds) == std::variant_size_v<FlowFormat>);
    return Kinds[Format.index()];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sdp::Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<Sdp> Sdp::Parse(std::string_view Text)
{
    DtNmosSdp* Parsed = nullptr;
    const Status Checked =
        Detail::Check(DtNmosSdp_Parse(Text.data(), Text.size(), &Parsed));
    const std::unique_ptr<DtNmosSdp, Detail::SdpFree> Owned(Parsed);
    if (!Checked)
    {
        return std::unexpected(Checked.error());
    }
    return Detail::FromNative(Parsed);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sdp::Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C function is called twice: first to learn the size of the text, then to write
// the text into a buffer of that size.
//
inline Expected<std::string> Sdp::Write() const
{
    Detail::NativeSession Head;
    Status Made = Head.Set(Session);
    const std::unique_ptr<Detail::NativeFlow[]> Natives(
        new Detail::NativeFlow[Flows.size()]);
    std::vector<DtNmosFlow> NativeFlows;
    for (std::size_t i = 0; Made && i < Flows.size(); ++i)
    {
        Made = Natives[i].Set(Flows[i]);
        NativeFlows.push_back(Natives[i].Get());
    }
    if (!Made)
    {
        return std::unexpected(Made.error());
    }
    std::size_t Size = 0;
    const DtNmosResult Asked = DtNmosSdp_Write(&Head.Get(), NativeFlows.data(),
                                               NativeFlows.size(), nullptr, &Size);
    if (Asked != DTNMOS_E_BUFFER_TOO_SMALL)
    {
        return std::unexpected(
            Detail::LastError(Asked == DTNMOS_OK ? DTNMOS_E_INTERNAL : Asked));
    }
    std::string Text(Size, '\0');
    Size = Text.size();
    const Status Written = Detail::Check(DtNmosSdp_Write(
        &Head.Get(), NativeFlows.data(), NativeFlows.size(), Text.data(), &Size));
    if (!Written)
    {
        return std::unexpected(Written.error());
    }
    Text.resize(Size);
    return Text;
}

} // namespace DtNmos
