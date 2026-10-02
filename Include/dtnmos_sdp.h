// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_sdp.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The SDP of SMPTE ST 2110 flows, read and written
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos.h"

#ifdef __cplusplus
extern "C"
{
#endif

// What a media section carries, from the encoding of its a=rtpmap.
typedef enum DtNmosMedia
{
    DTNMOS_MEDIA_VIDEO = 0,            // ST 2110-20, uncompressed video: raw (RFC 4175)
    DTNMOS_MEDIA_AUDIO = 1,            // ST 2110-30 and -31: L16, L24 or AM824
    DTNMOS_MEDIA_COMPRESSED_VIDEO = 2, // ST 2110-22: jxsv, JPEG XS (RFC 9134)
    DTNMOS_MEDIA_ANC = 3,              // ST 2110-40: smpte291, ancillary data (RFC 8331)
    DTNMOS_MEDIA_OTHER = 99            // anything else, with its encoding and raw fmtp
} DtNmosMedia;

// The values ST 2110 lists for the text fields of a flow, as enums beside the text, which
// the fields keep so that a value of a later edition or of another maker passes through
// unchanged. Each has _FromText(), which gives _OTHER for null, empty or a value it does
// not know, and _Text(), which gives the SDP's spelling, or "" for _OTHER.

// sampling, ST 2110-20 §7.4.1.
typedef enum DtNmosSampling
{
    DTNMOS_SAMPLING_OTHER = 0,
    DTNMOS_SAMPLING_YCBCR_444,   // YCbCr-4:4:4
    DTNMOS_SAMPLING_YCBCR_422,   // YCbCr-4:2:2
    DTNMOS_SAMPLING_YCBCR_420,   // YCbCr-4:2:0
    DTNMOS_SAMPLING_CLYCBCR_444, // CLYCbCr-4:4:4, constant luminance
    DTNMOS_SAMPLING_CLYCBCR_422, // CLYCbCr-4:2:2
    DTNMOS_SAMPLING_CLYCBCR_420, // CLYCbCr-4:2:0
    DTNMOS_SAMPLING_ICTCP_444,   // ICtCp-4:4:4
    DTNMOS_SAMPLING_ICTCP_422,   // ICtCp-4:2:2
    DTNMOS_SAMPLING_ICTCP_420,   // ICtCp-4:2:0
    DTNMOS_SAMPLING_RGB,         // RGB
    DTNMOS_SAMPLING_XYZ,         // XYZ
    DTNMOS_SAMPLING_KEY          // KEY, a key (alpha) signal
} DtNmosSampling;

// colorimetry, ST 2110-20 §7.5.
typedef enum DtNmosColorimetry
{
    DTNMOS_COLORIMETRY_OTHER = 0,
    DTNMOS_COLORIMETRY_BT601,
    DTNMOS_COLORIMETRY_BT709,
    DTNMOS_COLORIMETRY_BT2020,
    DTNMOS_COLORIMETRY_BT2100,
    DTNMOS_COLORIMETRY_ST2065_1, // ST2065-1, ACES
    DTNMOS_COLORIMETRY_ST2065_3, // ST2065-3, ADX
    DTNMOS_COLORIMETRY_UNSPECIFIED,
    DTNMOS_COLORIMETRY_XYZ,
    DTNMOS_COLORIMETRY_ALPHA // of a key signal
} DtNmosColorimetry;

// TCS, the transfer characteristic system, ST 2110-20 §7.6; SDR when absent.
typedef enum DtNmosTcs
{
    DTNMOS_TCS_OTHER = 0,
    DTNMOS_TCS_SDR,
    DTNMOS_TCS_PQ,
    DTNMOS_TCS_HLG,
    DTNMOS_TCS_LINEAR,
    DTNMOS_TCS_BT2100LINPQ,
    DTNMOS_TCS_BT2100LINHLG,
    DTNMOS_TCS_ST2065_1, // ST2065-1
    DTNMOS_TCS_ST428_1,  // ST428-1
    DTNMOS_TCS_DENSITY,
    DTNMOS_TCS_ST2115LOGS3,
    DTNMOS_TCS_UNSPECIFIED
} DtNmosTcs;

// RANGE, ST 2110-20 §7.3; NARROW when absent.
typedef enum DtNmosRange
{
    DTNMOS_RANGE_OTHER = 0,
    DTNMOS_RANGE_NARROW,
    DTNMOS_RANGE_FULLPROTECT,
    DTNMOS_RANGE_FULL
} DtNmosRange;

// PM, the packing mode, ST 2110-20 §6.3.
typedef enum DtNmosPackingMode
{
    DTNMOS_PACKING_MODE_OTHER = 0,
    DTNMOS_PACKING_MODE_GENERAL, // 2110GPM
    DTNMOS_PACKING_MODE_BLOCK    // 2110BPM
} DtNmosPackingMode;

// TP, the type of a sender, ST 2110-21 §7.1.
typedef enum DtNmosTransmitterType
{
    DTNMOS_TRANSMITTER_TYPE_OTHER = 0,
    DTNMOS_TRANSMITTER_TYPE_NARROW,        // 2110TPN
    DTNMOS_TRANSMITTER_TYPE_NARROW_LINEAR, // 2110TPNL
    DTNMOS_TRANSMITTER_TYPE_WIDE           // 2110TPW
} DtNmosTransmitterType;

// The encoding of an audio flow, ST 2110-30 and -31.
typedef enum DtNmosAudioEncoding
{
    DTNMOS_AUDIO_ENCODING_OTHER = 0,
    DTNMOS_AUDIO_ENCODING_L16,
    DTNMOS_AUDIO_ENCODING_L24,
    DTNMOS_AUDIO_ENCODING_AM824
} DtNmosAudioEncoding;

// The sizes of the strings of a flow, each the longest value the standards allow and its
// terminating null, with room. The parser refuses a value that does not fit.
#define DTNMOS_MAX_SHORT_SIZE 16 // PM, TP, TM, an audio encoding

#define DTNMOS_MAX_VALUE_SIZE 32 // sampling, colorimetry, TCS, RANGE, SSN, JPEG XS

#define DTNMOS_MAX_ENCODING_SIZE 128 // a media subtype name, as RFC 6838 bounds it

#define DTNMOS_MAX_ADDRESS_SIZE 256 // an IP address or a fully qualified domain name

#define DTNMOS_MAX_EUI64_SIZE 24 // "39-A7-94-FF-FE-07-CB-D0"

#define DTNMOS_MAX_EUI48_SIZE 18 // "00-14-F4-01-02-03"

// An uncompressed video format as the a=fmtp of ST 2110-20 states it. The strings keep
// the text of the SDP, e.g. Sampling "YCbCr-4:2:2", Colorimetry "BT709", Tcs "SDR",
// TransmitterType "2110TPN"; a parameter the SDP does not give is 0 or empty.
typedef struct DtNmosVideoFormat
{
    uint32_t Width;
    uint32_t Height;
    uint32_t RateNumerator; // exactframerate, e.g. 30000/1001, or 25/1
    uint32_t RateDenominator;
    bool Interlaced; // interlace
    bool Segmented;  // segmented, PsF
    uint32_t Depth;  // bits per sample
    char Sampling[DTNMOS_MAX_VALUE_SIZE];
    char Colorimetry[DTNMOS_MAX_VALUE_SIZE];
    char Tcs[DTNMOS_MAX_VALUE_SIZE];             // TCS
    char Range[DTNMOS_MAX_VALUE_SIZE];           // RANGE
    char PackingMode[DTNMOS_MAX_SHORT_SIZE];     // PM
    char Ssn[DTNMOS_MAX_VALUE_SIZE];             // SSN, e.g. "ST2110-20:2017"
    char TransmitterType[DTNMOS_MAX_SHORT_SIZE]; // TP
} DtNmosVideoFormat;

// An audio format as a=rtpmap, a=ptime and a=fmtp of ST 2110-30 state it.
typedef struct DtNmosAudioFormat
{
    char Encoding[DTNMOS_MAX_SHORT_SIZE]; // "L24", "L16" or "AM824"
    uint32_t SampleRate;                  // e.g. 48000
    uint32_t Channels;
    uint32_t PacketTimeNs; // a=ptime: 1000000 for 1 ms, 125000 for 0.125 ms
    // channel-order, e.g. "SMPTE2110.(ST,ST)", which no standard bounds; null when
    // absent. Owned as the strings of the flow are (DtNmosFlow).
    const char* ChannelOrder;
} DtNmosAudioFormat;

// A compressed video format as ST 2110-22 states it: the raster and colour of ST
// 2110-20, the parameters of the codec in a=fmtp (JPEG XS: profile, level, sublevel,
// packetmode and transmode), and the bandwidth of b=AS.
typedef struct DtNmosCompressedVideoFormat
{
    char Encoding[DTNMOS_MAX_ENCODING_SIZE]; // e.g. "jxsv"
    uint32_t Width;
    uint32_t Height;
    uint32_t RateNumerator;
    uint32_t RateDenominator;
    bool Interlaced;
    bool Segmented;
    uint32_t Depth;
    char Sampling[DTNMOS_MAX_VALUE_SIZE];
    char Colorimetry[DTNMOS_MAX_VALUE_SIZE];
    char Tcs[DTNMOS_MAX_VALUE_SIZE];
    char Range[DTNMOS_MAX_VALUE_SIZE];
    char Ssn[DTNMOS_MAX_VALUE_SIZE];
    char TransmitterType[DTNMOS_MAX_SHORT_SIZE];
    char Profile[DTNMOS_MAX_VALUE_SIZE];
    char Level[DTNMOS_MAX_VALUE_SIZE];
    char Sublevel[DTNMOS_MAX_VALUE_SIZE];
    uint32_t PacketMode;       // packetmode
    uint32_t TransmissionMode; // transmode; 1 when absent, as RFC 9134 says
    uint64_t BandwidthKbps;    // b=AS; 0 when absent
} DtNmosCompressedVideoFormat;

// One DID and SDID pair of ancillary data.
typedef struct DtNmosDidSdid
{
    uint8_t Did;
    uint8_t Sdid;
} DtNmosDidSdid;

// An ANC format as ST 2110-40 states it in a=fmtp.
typedef struct DtNmosAncFormat
{
    const DtNmosDidSdid* DidSdid; // DID_SDID, owned as the strings of the flow are
    size_t DidSdidCount;
    uint32_t VpidCode;      // VPID_Code; 0 when absent
    uint32_t RateNumerator; // exactframerate; 0 when absent
    uint32_t RateDenominator;
    char TransmissionModel[DTNMOS_MAX_SHORT_SIZE]; // TM, e.g. "CTM"; empty when absent
    char Ssn[DTNMOS_MAX_VALUE_SIZE];
} DtNmosAncFormat;

// A media section the parser does not know: its encoding from a=rtpmap and its a=fmtp
// as it stands.
typedef struct DtNmosOtherFormat
{
    char Encoding[DTNMOS_MAX_ENCODING_SIZE];
    const char* Fmtp; // which no standard bounds; null when absent
} DtNmosOtherFormat;

// The kind of reference clock of a=ts-refclk (RFC 7273, ST 2110-10).
typedef enum DtNmosRefClockKind
{
    DTNMOS_REFCLOCK_NONE = 0,     // no a=ts-refclk
    DTNMOS_REFCLOCK_PTP = 1,      // ptp=<version>:<grandmaster>[:<domain>], or :traceable
    DTNMOS_REFCLOCK_LOCALMAC = 2, // localmac=<MAC>, ST 2110-10 §8.2
    DTNMOS_REFCLOCK_OTHER = 3     // any other kind, kept as its text
} DtNmosRefClockKind;

// The reference clock of a=ts-refclk. ST 2110-10 §8.2 writes a PTP domain as
// ":<number>" and RFC 7273 §4.8 as ":domain-nmbr=<number>"; both are read, and the
// first is written.
typedef struct DtNmosRefClock
{
    DtNmosRefClockKind Kind;
    char PtpVersion[DTNMOS_MAX_VALUE_SIZE];  // e.g. "IEEE1588-2008"
    char Grandmaster[DTNMOS_MAX_EUI64_SIZE]; // its EUI-64; empty when traceable
    bool Traceable;                          // ptp=<version>:traceable
    int Domain;                              // 0 to 127; -1 when absent
    char LocalMac[DTNMOS_MAX_EUI48_SIZE];    // of DTNMOS_REFCLOCK_LOCALMAC
    const char* Text; // of DTNMOS_REFCLOCK_OTHER: the value of a=ts-refclk
} DtNmosRefClock;

// One RTP flow: a media section of an SDP. Its strings and arrays that have no fixed
// size, ChannelOrder, Fmtp, DidSdid and RefClock.Text, are owned by what the flow came
// from: the DtNmosSdp it was parsed into, the node that passes it to a callback, or the
// caller that filled it in. A flow is copied with =, which copies those pointers.
typedef struct DtNmosFlow
{
    size_t Size; // sizeof(DtNmosFlow), set by whoever fills it
    DtNmosMedia Media;
    char DestinationIp[DTNMOS_MAX_ADDRESS_SIZE]; // c=, without TTL; may be a domain name
    uint16_t DestinationPort;                    // m=
    // a=source-filter: incl; empty receives from any source
    char SourceIp[DTNMOS_MAX_ADDRESS_SIZE];
    uint8_t PayloadType;     // the first of m=
    uint32_t ClockRate;      // a=rtpmap: 90000 for video, the sample rate for audio
    DtNmosRefClock RefClock; // a=ts-refclk
    bool MediaClockDirect;   // a=mediaclk:direct=<offset> is present
    uint32_t MediaClockOffset;
    uint32_t Leg; // 0, or 1 for the second path of ST 2022-7 (a=group:DUP)
    union
    {
        DtNmosVideoFormat Video;
        DtNmosAudioFormat Audio;
        DtNmosCompressedVideoFormat CompressedVideo;
        DtNmosAncFormat Anc;
        DtNmosOtherFormat Other;
    } Format; // the member of media
} DtNmosFlow;

// The session level of an SDP.
typedef struct DtNmosSession
{
    size_t Size;
    const char* Name; // s=, which no standard bounds
    char
        OriginIp[DTNMOS_MAX_ADDRESS_SIZE]; // o=, the address or domain name of the sender
    uint64_t SessionId;
    uint64_t SessionVersion;
} DtNmosSession;

// A parsed SDP: its session and its flows, which it owns with their strings.
typedef struct DtNmosSdp DtNmosSdp;

// The audio encoding of Text, or DTNMOS_AUDIO_ENCODING_OTHER.
DTNMOS_API DtNmosAudioEncoding DtNmosAudioEncoding_FromText(const char* Text);

// The SDP's spelling of AudioEncoding, or "" for DTNMOS_AUDIO_ENCODING_OTHER; a static
// string.
DTNMOS_API const char* DtNmosAudioEncoding_Text(DtNmosAudioEncoding AudioEncoding);

// The colorimetry of Text, or DTNMOS_COLORIMETRY_OTHER.
DTNMOS_API DtNmosColorimetry DtNmosColorimetry_FromText(const char* Text);

// The SDP's spelling of Colorimetry, or "" for DTNMOS_COLORIMETRY_OTHER; a static
// string.
DTNMOS_API const char* DtNmosColorimetry_Text(DtNmosColorimetry Colorimetry);

// Makes *Copy a copy of Flow that owns its strings and arrays, ChannelOrder, Fmtp,
// DidSdid and RefClock.Text, so that it stays valid after what Flow came from is gone:
// a flow a node passes to a callback, or one of an SDP that is freed. Frees it with
// DtNmosFlow_Free(). Fails with DTNMOS_E_INVALID_ARGUMENT for a null flow or copy or a
// Size the library does not know, and with DTNMOS_E_NO_MEMORY; *Copy is null after a
// failure.
DTNMOS_API DtNmosResult DtNmosFlow_Copy(const DtNmosFlow* Flow, DtNmosFlow** Copy);

// Frees a flow DtNmosFlow_Copy() made, with what it owns; null does nothing. A flow made
// otherwise must not be given.
DTNMOS_API void DtNmosFlow_Free(DtNmosFlow* Flow);

// Returns the name of a media, e.g. "video"; a static string.
DTNMOS_API const char* DtNmosMedia_Name(DtNmosMedia Media);

// The packing mode of Text, or DTNMOS_PACKING_MODE_OTHER.
DTNMOS_API DtNmosPackingMode DtNmosPackingMode_FromText(const char* Text);

// The SDP's spelling of PackingMode, or "" for DTNMOS_PACKING_MODE_OTHER; a static
// string.
DTNMOS_API const char* DtNmosPackingMode_Text(DtNmosPackingMode PackingMode);

// The range of Text, or DTNMOS_RANGE_OTHER.
DTNMOS_API DtNmosRange DtNmosRange_FromText(const char* Text);

// The SDP's spelling of Range, or "" for DTNMOS_RANGE_OTHER; a static
// string.
DTNMOS_API const char* DtNmosRange_Text(DtNmosRange Range);

// The sampling of Text, or DTNMOS_SAMPLING_OTHER.
DTNMOS_API DtNmosSampling DtNmosSampling_FromText(const char* Text);

// The SDP's spelling of Sampling, or "" for DTNMOS_SAMPLING_OTHER; a static
// string.
DTNMOS_API const char* DtNmosSampling_Text(DtNmosSampling Sampling);

// Returns the flow at Index, in the order of the media sections, or null past them;
// valid, with its strings, until the SDP is freed.
DTNMOS_API const DtNmosFlow* DtNmosSdp_Flow(const DtNmosSdp* Sdp, size_t Index);

// Returns the number of flows of the SDP, one per media section.
DTNMOS_API size_t DtNmosSdp_FlowCount(const DtNmosSdp* Sdp);

// Frees sdp and what it holds; null does nothing.
DTNMOS_API void DtNmosSdp_Free(DtNmosSdp* Sdp);

// Parses the SDP of length bytes of text. Fails with DTNMOS_E_PARSE, naming the line,
// on a malformed description or a value longer than its field, and with
// DTNMOS_E_INVALID_ARGUMENT on one without media sections.
DTNMOS_API DtNmosResult DtNmosSdp_Parse(const char* Text, size_t Length, DtNmosSdp** Sdp);

// Returns the session of sdp; valid until sdp is freed.
DTNMOS_API const DtNmosSession* DtNmosSdp_Session(const DtNmosSdp* Sdp);

// Writes the SDP of session with the count flows of flows into buffer, which holds
// *Size bytes, with a terminating null. A flow of leg 1 is the second path of the flow
// of leg 0 before it, which a=group:DUP pairs it with: the paths of the first pair are
// a=mid:primary and a=mid:secondary, those of the next primary2 and secondary2, and so
// on, and a flow of one path has no a=mid. An IPv4 multicast destination gets a TTL of
// 64. When the text does not fit, or buffer is null, it fails with
// DTNMOS_E_BUFFER_TOO_SMALL and sets *Size to the bytes it needs; on success *Size is
// the length of the text.
DTNMOS_API DtNmosResult DtNmosSdp_Write(const DtNmosSession* Session,
                                        const DtNmosFlow* Flows, size_t Count,
                                        char* Buffer, size_t* Size);

// The transfer characteristic system of Text, or DTNMOS_TCS_OTHER.
DTNMOS_API DtNmosTcs DtNmosTcs_FromText(const char* Text);

// The SDP's spelling of Tcs, or "" for DTNMOS_TCS_OTHER; a static
// string.
DTNMOS_API const char* DtNmosTcs_Text(DtNmosTcs Tcs);

// The transmitter type of Text, or DTNMOS_TRANSMITTER_TYPE_OTHER.
DTNMOS_API DtNmosTransmitterType DtNmosTransmitterType_FromText(const char* Text);

// The SDP's spelling of TransmitterType, or "" for DTNMOS_TRANSMITTER_TYPE_OTHER; a
// static string.
DTNMOS_API const char* DtNmosTransmitterType_Text(DtNmosTransmitterType TransmitterType);

// Fills an empty Colorimetry and Tcs of format with what its raster has when nothing
// says otherwise: by the height of the active picture, which tells SD, HD and UHD apart
// whatever the width, BT601 up to 576 lines, BT709 up to 1080 and BT2020 above, as
// ST 2036-1 has UHD; Tcs SDR. A height of 0 leaves Colorimetry empty. HDR, PQ or HLG
// with BT2100, cannot be told from the raster, and is the caller's to set. Range is left
// out, which ST 2110-20 reads as narrow.
DTNMOS_API void DtNmosVideoFormat_SetDefaults(DtNmosVideoFormat* Format);

#ifdef __cplusplus
}
#endif
