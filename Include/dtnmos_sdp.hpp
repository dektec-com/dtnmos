// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_sdp.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The C++ API of the SDP of SMPTE ST 2110 flows, read and written
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos_sdp.h. An SDP file describes RTP streams: for each one, its
// format, where it is sent and how it is timed. Sdp::Parse() reads one into an Sdp, which
// holds its Session and one Flow per stream, and Sdp::Write() writes it back. A flow's
// format is a std::variant of a struct per kind of media: ST 2110-20 video, -30 audio,
// -22 compressed video and -40 ancillary data. Every type here is a value that owns its
// strings, and is copied with =.

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
// The values ST 2110 defines for the parameters of a stream, one enum class per
// parameter, with the values of the C enums. In each:
// - None (0) means the parameter is not given;
// - Other means a value dtnmos does not know, e.g. of a later edition of the standard.
//   The parameter is then kept, as written, in the format's OtherParameters, so that it
//   is written back unchanged.
// Text() gives the SDP's text of a value ("" for None and Other), and FromText<E>() the
// value of a text (None for "", Other for one it does not know).
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

// Returns the value of an SDP's text: None for "", Other for a text it does not know. E
// is AudioEncoding, Colorimetry, PackingMode, Range, Sampling, Tcs or TransmitterType.
template <typename E> E FromText(std::string_view Text);

// Returns the name of Kind, e.g. "video", for messages.
std::string_view Name(Media Kind);

// Return the SDP's text of a value, "" for None and Other.
std::string_view Text(AudioEncoding Value);
std::string_view Text(Colorimetry Value);
std::string_view Text(PackingMode Value);
std::string_view Text(Range Value);
std::string_view Text(Sampling Value);
std::string_view Text(Tcs Value);
std::string_view Text(TransmitterType Value);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Formats +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The format of a stream, one struct per kind of media. A parameter the SDP does not give
// is 0, None or "". OtherParameters holds the parameters dtnmos does not know, or whose
// value it does not know, as written and separated by "; ", e.g. "TCS=ST2115LOGS9;
// TROFF=37"; the writer writes them after the others, unchanged. To write a value dtnmos
// does not know, set the field to Other and put the parameter there.
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
    // picture's Height: Bt601 up to 576 lines (SD), Bt709 up to 1080 (HD), Bt2020 above
    // (UHD); and Sdr. With a Height of 0 Colorimetry stays None. HDR cannot be told from
    // the picture size: for Pq or Hlg with Bt2100, set them yourself. Range is left None,
    // which means Narrow.
    void SetDefaults();

    friend bool operator==(const VideoFormat&, const VideoFormat&) = default;
};

// The format of an ST 2110-30 or -31 audio stream, from its a=rtpmap, a=ptime and a=fmtp
// lines. A stream is audio only for the encodings of AudioEncoding, so Encoding is never
// Other.
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

// The format of an ST 2110-22 compressed video stream: the picture parameters of ST
// 2110-20, the codec's parameters (for JPEG XS: profile, level, sublevel, packetmode
// and transmode) and the bandwidth (b=AS).
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
    uint32_t TransmissionMode = 0; // transmode; a parsed SDP without it has 1 (RFC 9134)
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

// The format of a stream dtnmos does not know: its encoding and its a=fmtp line as
// written.
struct OtherFormat
{
    std::string Encoding;
    std::optional<std::string> Fmtp; // The a=fmtp line's parameters; none without one

    friend bool operator==(const OtherFormat&, const OtherFormat&) = default;
};

// The format of a flow: which of them it holds is its Media. std::monostate is a flow
// without a format, Media::None, which is refused where a format is needed.
using FlowFormat = std::variant<std::monostate, VideoFormat, AudioFormat,
                                CompressedVideoFormat, AncFormat, OtherFormat>;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The clock a stream's timestamps follow (a=ts-refclk). A PTP domain is read in both
// spellings, ":<number>" (ST 2110-10) and ":domain-nmbr=<number>" (RFC 7273), and written
// in the first.
struct RefClock
{
    RefClockKind Kind = RefClockKind::None;
    std::string PtpVersion;  // Ptp: e.g. "IEEE1588-2008"
    std::string Grandmaster; // Ptp: the grandmaster's EUI-64; "" when Traceable
    bool Traceable = false;  // Ptp: traceable to TAI, without a grandmaster named
    int Domain = -1;         // Ptp: 0 to 127; -1 when not given
    std::string LocalMac;    // LocalMac: the sender's MAC address
    std::string Text;        // Other: the line's value

    friend bool operator==(const RefClock&, const RefClock&) = default;
};

// One RTP stream: a media section of an SDP.
struct Flow
{
    std::string DestinationIp; // Where it is sent (c=), without TTL; may be a host name
    uint16_t DestinationPort = 0; // The UDP port (m=)
    std::string SourceIp;         // The only source to receive from; "" for any source
    uint8_t PayloadType = 0;      // The RTP payload type (the first in m=)
    uint32_t ClockRate = 0; // RTP clock (a=rtpmap): 90000 for video, the sample rate for
                            // audio
    DtNmos::RefClock RefClock;     // The clock the timestamps follow (a=ts-refclk)
    bool MediaClockDirect = false; // a=mediaclk:direct=<offset> is present
    uint32_t MediaClockOffset = 0; // Its offset
    uint32_t Leg = 0; // 0, or 1 for the second path of an ST 2022-7 pair (a=group:DUP)
    FlowFormat Format;

    // Returns what the flow carries: the kind of its Format.
    Media GetMedia() const;

    friend bool operator==(const Flow&, const Flow&) = default;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

// The session part of an SDP, which all its streams share.
struct Session
{
    std::string Name;     // The session name (s=)
    std::string OriginIp; // The sender's address or host name (o=)
    uint64_t SessionId = 0;
    uint64_t SessionVersion = 0; // Increase it when the SDP changes

    friend bool operator==(const Session&, const Session&) = default;
};

// An SDP: its session and its flows.
struct Sdp
{
    // Reads the SDP in Text. Fails with:
    //   Result::Parse            the SDP is malformed, or a value is longer than its
    //                            field; the message names the line
    //   Result::InvalidArgument  the SDP has no media sections
    [[nodiscard]] static Expected<Sdp> Parse(std::string_view Text);

    DtNmos::Session Session;
    std::vector<Flow> Flows; // In the order of the media sections

    // Writes the SDP as text. A flow with Leg 1 is the second path of the flow with Leg 0
    // just before it: the two are written as an ST 2022-7 pair. An IPv4 multicast
    // destination is written with a TTL of 64. Fails with Result::InvalidArgument when
    // there are no flows, the session has no OriginIp, a flow has no format, no
    // destination or no audio encoding, a Leg 1 has no Leg 0 before it, or a text is
    // longer than the C API's field.
    [[nodiscard]] Expected<std::string> Write() const;

    friend bool operator==(const Sdp&, const Sdp&) = default;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

namespace Detail
{

// Returns the C text of an optional text: NULL for "".
inline const char* ToNativeOrNull(const std::string& Text)
{
    return Text.empty() ? nullptr : Text.c_str();
}

// Convert the enums of an SDP from the C API, as FromNative(DtNmosResult) does.
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

// Convert the enums of an SDP to the C API.
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
// Convert the structs of an SDP from the C API. Each asserts the last field it knows.
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

// A Flow as the C API takes it: a DtNmosFlow whose pointer fields point into the texts
// and the list that this object keeps. It is not copied or moved, as that would leave
// the pointers behind.
class NativeFlow
{
  public:
    NativeFlow() = default;
    NativeFlow(const NativeFlow&) = delete;
    NativeFlow& operator=(const NativeFlow&) = delete;

    // Returns the C flow, valid while this object is and until the next Set().
    const DtNmosFlow& Get() const { return Native; }

    // Makes the C flow that Value is. Fails with Result::InvalidArgument when a text is
    // longer than its C field.
    [[nodiscard]] Status Set(const Flow& Value);

  private:
    [[nodiscard]] Status SetFormat(const Flow& Value);

    DtNmosFlow Native{};
    std::string OtherParameters;
    std::string ChannelOrder;
    std::string Fmtp;
    std::string RefClockText;
    std::vector<DtNmosDidSdid> Pairs;
};

// A Session as the C API takes it, as NativeFlow is a Flow.
class NativeSession
{
  public:
    NativeSession() = default;
    NativeSession(const NativeSession&) = delete;
    NativeSession& operator=(const NativeSession&) = delete;

    // Returns the C session, valid while this object is and until the next Set().
    const DtNmosSession& Get() const { return Native; }

    // Makes the C session that Value is. Fails with Result::InvalidArgument when its
    // OriginIp is longer than the C field.
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
    NativeClock.Text = ToNativeOrNull(RefClockText);
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
        v.OtherParameters = ToNativeOrNull(OtherParameters);
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
        a.ChannelOrder = ToNativeOrNull(ChannelOrder);
        OtherParameters = Audio->OtherParameters;
        a.OtherParameters = ToNativeOrNull(OtherParameters);
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
        c.OtherParameters = ToNativeOrNull(OtherParameters);
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
    Native.Name = ToNativeOrNull(Name);
    Native.SessionId = Value.SessionId;
    Native.SessionVersion = Value.SessionVersion;
    return CopyText(Native.OriginIp, Value.OriginIp, "Session.OriginIp");
}

// Frees a DtNmosSdp, for a std::unique_ptr.
struct SdpFree
{
    void operator()(DtNmosSdp* Sdp) const { DtNmosSdp_Free(Sdp); }
};

} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C functions take a text with its null, which a std::string_view need not have.
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
// The C function reads the height, the colorimetry and the TCS, and sets the last two.
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
// The alternatives of FlowFormat are in the order of their values of Media.
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
    Sdp Value;
    Value.Session = Detail::FromNative(*DtNmosSdp_Session(Parsed));
    for (std::size_t i = 0; i < DtNmosSdp_FlowCount(Parsed); ++i)
    {
        Value.Flows.push_back(Detail::FromNative(*DtNmosSdp_Flow(Parsed, i)));
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sdp::Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C function is asked for the size first, and then writes into a buffer of it.
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
