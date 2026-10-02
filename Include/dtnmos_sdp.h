// #*#*#*#*#*#*#*#*#*#*#*#*#*#* dtnmos_sdp.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The SDP of SMPTE ST 2110 flows, read and written
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An SDP file describes RTP streams: for each one, its format, where it is sent and how
// it is timed. dtnmos reads an SDP into a DtNmosSdp, which holds one DtNmosFlow per
// stream (DtNmosSdp_Parse), and writes flows back into SDP text (DtNmosSdp_Write). A
// flow's format is a struct per kind of media: ST 2110-20 video, -30 audio, -22
// compressed video and -40 ancillary data.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos.h"

#ifdef __cplusplus
extern "C"
{
#endif

// What a stream carries, from the encoding in its a=rtpmap line. 0 is _NONE, so that a
// field a program forgets to set is refused rather than taken as video.
typedef enum DtNmosMedia
{
    DTNMOS_MEDIA_NONE = 0,             // Not set; refused where a media is needed
    DTNMOS_MEDIA_VIDEO = 1,            // ST 2110-20 uncompressed video ("raw", RFC 4175)
    DTNMOS_MEDIA_AUDIO = 2,            // ST 2110-30 and -31 audio: L16, L24 or AM824
    DTNMOS_MEDIA_COMPRESSED_VIDEO = 3, // ST 2110-22 JPEG XS ("jxsv", RFC 9134)
    DTNMOS_MEDIA_ANC = 4,              // ST 2110-40 ancillary data ("smpte291", RFC 8331)
    DTNMOS_MEDIA_OTHER = 99            // Anything else; its encoding and a=fmtp are kept
} DtNmosMedia;

// The values ST 2110 defines for the parameters of a video stream, one enum per
// parameter. In each enum:
// - _NONE (0) means the parameter is not given;
// - _OTHER means a value dtnmos does not know, e.g. of a later edition of the standard.
//   The parameter is then kept, as written, in the format's OtherParameters, so that it
//   is written back unchanged.
// Each enum has two functions: _FromText() converts the SDP's text to the enum (_NONE
// for NULL or "", _OTHER for an unknown value), and _Text() converts the enum to the
// SDP's text ("" for _NONE and _OTHER).

// The sampling parameter (ST 2110-20 §7.4.1): how colour is sampled.
typedef enum DtNmosSampling
{
    DTNMOS_SAMPLING_NONE = 0,    // Not given
    DTNMOS_SAMPLING_OTHER,       // Not known
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

// The colorimetry parameter (ST 2110-20 §7.5): which colour space.
typedef enum DtNmosColorimetry
{
    DTNMOS_COLORIMETRY_NONE = 0, // Not given
    DTNMOS_COLORIMETRY_OTHER,    // Not known
    DTNMOS_COLORIMETRY_BT601,
    DTNMOS_COLORIMETRY_BT709,
    DTNMOS_COLORIMETRY_BT2020,
    DTNMOS_COLORIMETRY_BT2100,
    DTNMOS_COLORIMETRY_ST2065_1, // ST2065-1, ACES
    DTNMOS_COLORIMETRY_ST2065_3, // ST2065-3, ADX
    DTNMOS_COLORIMETRY_UNSPECIFIED,
    DTNMOS_COLORIMETRY_XYZ,
    DTNMOS_COLORIMETRY_ALPHA // Of a key signal
} DtNmosColorimetry;

// The TCS parameter (ST 2110-20 §7.6): the transfer characteristic, e.g. SDR or HDR. A
// stream without it is SDR.
typedef enum DtNmosTcs
{
    DTNMOS_TCS_NONE = 0, // Not given
    DTNMOS_TCS_OTHER,    // Not known
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

// The RANGE parameter (ST 2110-20 §7.3): which code values are used. A stream without it
// is NARROW.
typedef enum DtNmosRange
{
    DTNMOS_RANGE_NONE = 0, // Not given
    DTNMOS_RANGE_OTHER,    // Not known
    DTNMOS_RANGE_NARROW,
    DTNMOS_RANGE_FULLPROTECT,
    DTNMOS_RANGE_FULL
} DtNmosRange;

// The PM parameter (ST 2110-20 §6.3): how video is divided over packets.
typedef enum DtNmosPackingMode
{
    DTNMOS_PACKING_MODE_NONE = 0, // Not given
    DTNMOS_PACKING_MODE_OTHER,    // Not known
    DTNMOS_PACKING_MODE_GENERAL,  // 2110GPM
    DTNMOS_PACKING_MODE_BLOCK     // 2110BPM
} DtNmosPackingMode;

// The TP parameter (ST 2110-21 §7.1): how evenly the sender spreads its packets.
typedef enum DtNmosTransmitterType
{
    DTNMOS_TRANSMITTER_TYPE_NONE = 0,      // Not given
    DTNMOS_TRANSMITTER_TYPE_OTHER,         // Not known
    DTNMOS_TRANSMITTER_TYPE_NARROW,        // 2110TPN
    DTNMOS_TRANSMITTER_TYPE_NARROW_LINEAR, // 2110TPNL
    DTNMOS_TRANSMITTER_TYPE_WIDE           // 2110TPW
} DtNmosTransmitterType;

// The encoding of an audio stream (ST 2110-30 and -31).
typedef enum DtNmosAudioEncoding
{
    DTNMOS_AUDIO_ENCODING_NONE = 0, // Not given
    DTNMOS_AUDIO_ENCODING_OTHER,    // Not known
    DTNMOS_AUDIO_ENCODING_L16,
    DTNMOS_AUDIO_ENCODING_L24,
    DTNMOS_AUDIO_ENCODING_AM824
} DtNmosAudioEncoding;

// The sizes of the text fields of a flow, with room for the null. Each is larger than
// the longest value the standards allow. The parser refuses a value that does not fit.
#define DTNMOS_MAX_SHORT_SIZE 16 // TM, an API's protocol

#define DTNMOS_MAX_VALUE_SIZE 32 // SSN, JPEG XS, PTP version

#define DTNMOS_MAX_ENCODING_SIZE 128 // A media subtype name (RFC 6838)

#define DTNMOS_MAX_ADDRESS_SIZE 256 // An IP address or a fully qualified domain name

#define DTNMOS_MAX_EUI64_SIZE 24 // e.g. "39-A7-94-FF-FE-07-CB-D0"

#define DTNMOS_MAX_EUI48_SIZE 18 // e.g. "00-14-F4-01-02-03"

// The format of an ST 2110-20 video stream, from its a=fmtp line. A parameter the SDP
// does not give is 0, _NONE or "".
typedef struct DtNmosVideoFormat
{
    uint32_t Width;
    uint32_t Height;
    uint32_t RateNumerator; // Frame rate (exactframerate), e.g. 30000/1001 or 25/1
    uint32_t RateDenominator;
    bool Interlaced; // Interlaced video (interlace)
    bool Segmented;  // Progressive frames sent as two fields, PsF (segmented)
    uint32_t Depth;  // Bits per sample
    DtNmosSampling Sampling;
    DtNmosColorimetry Colorimetry;
    DtNmosTcs Tcs;
    DtNmosRange Range;
    DtNmosPackingMode PackingMode;
    DtNmosTransmitterType TransmitterType;
    char Ssn[DTNMOS_MAX_VALUE_SIZE]; // The edition of the standard (SSN), e.g.
                                     // "ST2110-20:2017"
    // The parameters dtnmos does not know, or whose value it does not know, as written
    // and separated by "; ", e.g. "TCS=ST2115LOGS9; TROFF=37"; NULL when there are none.
    // The writer writes them after the others, unchanged. To write a value dtnmos does
    // not know, set the field to _OTHER and put the parameter here. Owned as the flow's
    // other strings are (see DtNmosFlow).
    const char* OtherParameters;
} DtNmosVideoFormat;

// The format of an ST 2110-30 or -31 audio stream, from its a=rtpmap, a=ptime and a=fmtp
// lines. A stream is audio only for the encodings of DtNmosAudioEncoding, so Encoding is
// never _OTHER.
typedef struct DtNmosAudioFormat
{
    DtNmosAudioEncoding Encoding;
    uint32_t SampleRate;   // e.g. 48000
    uint32_t Channels;     // Channels per packet
    uint32_t PacketTimeNs; // Time per packet (a=ptime): 1000000 for 1 ms
    // Which channel is which (channel-order), e.g. "SMPTE2110.(ST,ST)"; NULL when not
    // given. Owned as the flow's other strings are.
    const char* ChannelOrder;
    // The other parameters of a=fmtp, as in DtNmosVideoFormat.
    const char* OtherParameters;
} DtNmosAudioFormat;

// The format of an ST 2110-22 compressed video stream: the picture parameters of ST
// 2110-20, the codec's parameters (for JPEG XS: profile, level, sublevel, packetmode
// and transmode) and the bandwidth (b=AS). The enums and OtherParameters are as in
// DtNmosVideoFormat.
typedef struct DtNmosCompressedVideoFormat
{
    char Encoding[DTNMOS_MAX_ENCODING_SIZE]; // The codec, e.g. "jxsv"
    uint32_t Width;
    uint32_t Height;
    uint32_t RateNumerator;
    uint32_t RateDenominator;
    bool Interlaced;
    bool Segmented;
    uint32_t Depth;
    DtNmosSampling Sampling;
    DtNmosColorimetry Colorimetry;
    DtNmosTcs Tcs;
    DtNmosRange Range;
    DtNmosTransmitterType TransmitterType;
    char Ssn[DTNMOS_MAX_VALUE_SIZE];
    char Profile[DTNMOS_MAX_VALUE_SIZE];
    char Level[DTNMOS_MAX_VALUE_SIZE];
    char Sublevel[DTNMOS_MAX_VALUE_SIZE];
    uint32_t PacketMode;       // packetmode
    uint32_t TransmissionMode; // transmode; 1 when not given (RFC 9134)
    uint64_t BandwidthKbps;    // b=AS; 0 when not given
    const char* OtherParameters;
} DtNmosCompressedVideoFormat;

// One kind of ancillary data packet: its DID and SDID.
typedef struct DtNmosDidSdid
{
    uint8_t Did;
    uint8_t Sdid;
} DtNmosDidSdid;

// The format of an ST 2110-40 ancillary data stream, from its a=fmtp line.
typedef struct DtNmosAncFormat
{
    // The kinds of packet the stream carries (DID_SDID). Owned as the flow's strings are.
    const DtNmosDidSdid* DidSdid;
    size_t DidSdidCount;    // Entries in DidSdid
    uint32_t VpidCode;      // VPID_Code; 0 when not given
    uint32_t RateNumerator; // exactframerate; 0 when not given
    uint32_t RateDenominator;
    char TransmissionModel[DTNMOS_MAX_SHORT_SIZE]; // TM, e.g. "CTM"; "" when not given
    char Ssn[DTNMOS_MAX_VALUE_SIZE];
} DtNmosAncFormat;

// The format of a stream dtnmos does not know: its encoding and its a=fmtp line as
// written.
typedef struct DtNmosOtherFormat
{
    char Encoding[DTNMOS_MAX_ENCODING_SIZE];
    const char* Fmtp; // The a=fmtp line's parameters; NULL when there is none
} DtNmosOtherFormat;

// The kind of clock a stream's timestamps follow (a=ts-refclk, RFC 7273, ST 2110-10).
typedef enum DtNmosRefClockKind
{
    DTNMOS_REFCLOCK_NONE = 0,     // No a=ts-refclk
    DTNMOS_REFCLOCK_PTP = 1,      // A PTP grandmaster: ptp=<version>:<grandmaster>
    DTNMOS_REFCLOCK_LOCALMAC = 2, // The sender's own clock: localmac=<MAC address>
    DTNMOS_REFCLOCK_OTHER = 3     // Another kind, kept as text
} DtNmosRefClockKind;

// The clock a stream's timestamps follow (a=ts-refclk). A PTP domain is read in both
// spellings, ":<number>" (ST 2110-10) and ":domain-nmbr=<number>" (RFC 7273), and written
// in the first.
typedef struct DtNmosRefClock
{
    DtNmosRefClockKind Kind;
    char PtpVersion[DTNMOS_MAX_VALUE_SIZE];  // PTP: e.g. "IEEE1588-2008"
    char Grandmaster[DTNMOS_MAX_EUI64_SIZE]; // PTP: the grandmaster's EUI-64; "" when
                                             // Traceable
    bool Traceable;                          // PTP: traceable to TAI, without a
                                             // grandmaster named
    int Domain;                              // PTP: 0 to 127; -1 when not given
    char LocalMac[DTNMOS_MAX_EUI48_SIZE];    // LOCALMAC: the sender's MAC address
    const char* Text; // OTHER: the line's value. Owned as the flow's strings are
} DtNmosRefClock;

// One RTP stream: a media section of an SDP.
//
// The flow's pointer fields (ChannelOrder, OtherParameters, Fmtp, DidSdid and
// RefClock.Text) point into memory owned by where the flow came from: the DtNmosSdp it
// was read into, the node that passes it to a callback, or the program that filled it
// in. Copying a flow with = copies those pointers; DtNmosFlow_Copy() makes a copy that
// owns its memory.
typedef struct DtNmosFlow
{
    size_t Size;       // sizeof(DtNmosFlow), set by whoever fills it in
    DtNmosMedia Media; // Which member of Format is valid
    char DestinationIp[DTNMOS_MAX_ADDRESS_SIZE]; // Where it is sent (c=), without TTL;
                                                 // may be a host name
    uint16_t DestinationPort;                    // The UDP port (m=)
    // The only source to receive from (a=source-filter: incl); "" for any source
    char SourceIp[DTNMOS_MAX_ADDRESS_SIZE];
    uint8_t PayloadType;     // The RTP payload type (the first in m=)
    uint32_t ClockRate;      // RTP clock (a=rtpmap): 90000 for video, the sample rate for
                             // audio
    DtNmosRefClock RefClock; // The clock the timestamps follow (a=ts-refclk)
    bool MediaClockDirect;   // a=mediaclk:direct=<offset> is present
    uint32_t MediaClockOffset; // Its offset
    uint32_t Leg; // 0, or 1 for the second path of an ST 2022-7 pair (a=group:DUP)
    union
    {
        DtNmosVideoFormat Video;
        DtNmosAudioFormat Audio;
        DtNmosCompressedVideoFormat CompressedVideo;
        DtNmosAncFormat Anc;
        DtNmosOtherFormat Other;
    } Format; // The member that Media names
} DtNmosFlow;

// The session part of an SDP, which all its streams share.
typedef struct DtNmosSession
{
    size_t Size;                            // sizeof(DtNmosSession)
    const char* Name;                       // The session name (s=)
    char OriginIp[DTNMOS_MAX_ADDRESS_SIZE]; // The sender's address or host name (o=)
    uint64_t SessionId;
    uint64_t SessionVersion; // Increase it when the SDP changes
} DtNmosSession;

// A parsed SDP: its session and its flows. It owns them and their strings.
typedef struct DtNmosSdp DtNmosSdp;

// Converts the text of an audio encoding to DtNmosAudioEncoding: _NONE for NULL or "",
// _OTHER for an unknown encoding.
DTNMOS_API DtNmosAudioEncoding DtNmosAudioEncoding_FromText(const char* Text);

// Returns the SDP text of AudioEncoding, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosAudioEncoding_Text(DtNmosAudioEncoding AudioEncoding);

// Converts the text of a colorimetry to DtNmosColorimetry: _NONE for NULL or "", _OTHER
// for an unknown value.
DTNMOS_API DtNmosColorimetry DtNmosColorimetry_FromText(const char* Text);

// Returns the SDP text of Colorimetry, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosColorimetry_Text(DtNmosColorimetry Colorimetry);

// Makes *Copy a copy of Flow that owns its memory, so that it stays valid after the SDP
// or callback it came from is gone. Free it with DtNmosFlow_Free().
//
// Returns DTNMOS_OK, or, with *Copy set to NULL:
//   DTNMOS_E_INVALID_ARGUMENT  Flow or Copy is NULL, or Flow->Size is not known
//   DTNMOS_E_NO_MEMORY         not enough memory
DTNMOS_API DtNmosResult DtNmosFlow_Copy(const DtNmosFlow* Flow, DtNmosFlow** Copy);

// Frees a flow that DtNmosFlow_Copy() made, with its memory. Do not pass another flow.
// NULL does nothing.
DTNMOS_API void DtNmosFlow_Free(DtNmosFlow* Flow);

// Returns the name of Media, e.g. "video", for messages. Do not free the string.
DTNMOS_API const char* DtNmosMedia_Name(DtNmosMedia Media);

// Converts the text of a packing mode to DtNmosPackingMode: _NONE for NULL or "", _OTHER
// for an unknown value.
DTNMOS_API DtNmosPackingMode DtNmosPackingMode_FromText(const char* Text);

// Returns the SDP text of PackingMode, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosPackingMode_Text(DtNmosPackingMode PackingMode);

// Converts the text of a range to DtNmosRange: _NONE for NULL or "", _OTHER for an
// unknown value.
DTNMOS_API DtNmosRange DtNmosRange_FromText(const char* Text);

// Returns the SDP text of Range, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosRange_Text(DtNmosRange Range);

// Converts the text of a sampling to DtNmosSampling: _NONE for NULL or "", _OTHER for an
// unknown value.
DTNMOS_API DtNmosSampling DtNmosSampling_FromText(const char* Text);

// Returns the SDP text of Sampling, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosSampling_Text(DtNmosSampling Sampling);

// Returns flow Index (from 0) of Sdp, in the order of its media sections, or NULL past
// the last. The flow is valid until Sdp is freed.
DTNMOS_API const DtNmosFlow* DtNmosSdp_Flow(const DtNmosSdp* Sdp, size_t Index);

// Returns how many flows Sdp has: one per media section.
DTNMOS_API size_t DtNmosSdp_FlowCount(const DtNmosSdp* Sdp);

// Frees Sdp, with its flows. NULL does nothing.
DTNMOS_API void DtNmosSdp_Free(DtNmosSdp* Sdp);

// Reads the SDP in Text, Length bytes long, into *Sdp, for the program to free.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_PARSE             the SDP is malformed, or a value is longer than its
//                              field; the message names the line
//   DTNMOS_E_INVALID_ARGUMENT  the SDP has no media sections
DTNMOS_API DtNmosResult DtNmosSdp_Parse(const char* Text, size_t Length, DtNmosSdp** Sdp);

// Returns the session of Sdp. It is valid until Sdp is freed.
DTNMOS_API const DtNmosSession* DtNmosSdp_Session(const DtNmosSdp* Sdp);

// Writes an SDP with Session and Count flows into Buffer, of *Size bytes, with its null.
// On success *Size is the length of the text.
//
// A flow with Leg 1 is the second path of the flow with Leg 0 just before it: the two are
// written as an ST 2022-7 pair. An IPv4 multicast destination is written with a TTL of
// 64.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_BUFFER_TOO_SMALL  Buffer is too small or NULL; *Size is the size needed
//   DTNMOS_E_INVALID_ARGUMENT  Session, Flows or Size is NULL, a Size field is too
//                              small, Count is 0, the session has no OriginIp, or a flow
//                              has no Media, no destination, no audio encoding, or a
//                              Leg 1 without a Leg 0 before it
DTNMOS_API DtNmosResult DtNmosSdp_Write(const DtNmosSession* Session,
                                        const DtNmosFlow* Flows, size_t Count,
                                        char* Buffer, size_t* Size);

// Converts the text of a transfer characteristic to DtNmosTcs: _NONE for NULL or "",
// _OTHER for an unknown value.
DTNMOS_API DtNmosTcs DtNmosTcs_FromText(const char* Text);

// Returns the SDP text of Tcs, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosTcs_Text(DtNmosTcs Tcs);

// Converts the text of a transmitter type to DtNmosTransmitterType: _NONE for NULL or
// "", _OTHER for an unknown value.
DTNMOS_API DtNmosTransmitterType DtNmosTransmitterType_FromText(const char* Text);

// Returns the SDP text of TransmitterType, or "" for _NONE and _OTHER.
DTNMOS_API const char* DtNmosTransmitterType_Text(DtNmosTransmitterType TransmitterType);

// Fills in the colorimetry and transfer characteristic of Format when they are _NONE,
// with the usual values for its picture height: BT601 up to 576 lines (SD), BT709 up to
// 1080 (HD), BT2020 above (UHD); and SDR. With a height of 0 the colorimetry stays _NONE.
// HDR cannot be told from the picture size: for PQ or HLG with BT2100, set them yourself.
// RANGE is left _NONE, which means narrow.
DTNMOS_API void DtNmosVideoFormat_SetDefaults(DtNmosVideoFormat* Format);

#ifdef __cplusplus
}
#endif
