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

// Returns the name of a media, e.g. "video"; a static string.
DTNMOS_API const char* DtNmosMedia_Name(DtNmosMedia media);

// An uncompressed video format as the a=fmtp of ST 2110-20 states it. The strings keep
// the text of the SDP, e.g. sampling "YCbCr-4:2:2", colorimetry "BT709", tcs "SDR",
// transmitter_type "2110TPN"; a parameter the SDP does not give is 0 or empty.
typedef struct DtNmosVideoFormat
{
    uint32_t Width;
    uint32_t Height;
    uint32_t RateNumerator; // exactframerate, e.g. 30000/1001, or 25/1
    uint32_t RateDenominator;
    int Interlaced; // interlace
    int Segmented;  // segmented, PsF
    uint32_t Depth; // bits per sample
    DtNmosString Sampling;
    DtNmosString Colorimetry;
    DtNmosString Tcs;             // TCS
    DtNmosString Range;           // RANGE
    DtNmosString PackingMode;     // PM
    DtNmosString Ssn;             // SSN, e.g. "ST2110-20:2017"
    DtNmosString TransmitterType; // TP
} DtNmosVideoFormat;

// An audio format as a=rtpmap, a=ptime and a=fmtp of ST 2110-30 state it.
typedef struct DtNmosAudioFormat
{
    DtNmosString Encoding; // "L24", "L16" or "AM824"
    uint32_t SampleRate;   // e.g. 48000
    uint32_t Channels;
    uint32_t PacketTimeNs;     // a=ptime: 1000000 for 1 ms, 125000 for 0.125 ms
    DtNmosString ChannelOrder; // channel-order, e.g. "SMPTE2110.(ST,ST)"; empty if absent
} DtNmosAudioFormat;

// A compressed video format as ST 2110-22 states it: the raster and colour of ST
// 2110-20, the parameters of the codec in a=fmtp (JPEG XS: profile, level, sublevel,
// packetmode and transmode), and the bandwidth of b=AS.
typedef struct DtNmosCompressedVideoFormat
{
    DtNmosString Encoding; // e.g. "jxsv"
    uint32_t Width;
    uint32_t Height;
    uint32_t RateNumerator;
    uint32_t RateDenominator;
    int Interlaced;
    int Segmented;
    uint32_t Depth;
    DtNmosString Sampling;
    DtNmosString Colorimetry;
    DtNmosString Tcs;
    DtNmosString Range;
    DtNmosString Ssn;
    DtNmosString TransmitterType;
    DtNmosString Profile;
    DtNmosString Level;
    DtNmosString Sublevel;
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
    const DtNmosDidSdid* DidSdid; // DID_SDID, owned by the flow; null when absent
    size_t DidSdidCount;
    uint32_t VpidCode;      // VPID_Code; 0 when absent
    uint32_t RateNumerator; // exactframerate; 0 when absent
    uint32_t RateDenominator;
    DtNmosString TransmissionModel; // TM, e.g. "CTM"; empty when absent
    DtNmosString Ssn;
} DtNmosAncFormat;

// A media section the parser does not know: its encoding from a=rtpmap and its a=fmtp
// as it stands.
typedef struct DtNmosOtherFormat
{
    DtNmosString Encoding;
    DtNmosString Fmtp;
} DtNmosOtherFormat;

// One RTP flow: a media section of an SDP.
typedef struct DtNmosFlow
{
    size_t Size; // sizeof(DtNmosFlow), set by whoever fills it
    DtNmosMedia Media;
    DtNmosString DestinationIp; // c=, IPv4 or IPv6, multicast or unicast, without TTL
    uint16_t DestinationPort;   // m=
    DtNmosString SourceIp;      // a=source-filter: incl; empty receives from any source
    uint8_t PayloadType;        // the first of m=
    uint32_t ClockRate;         // a=rtpmap: 90000 for video, the sample rate for audio
    DtNmosString TsRefclk;      // a=ts-refclk, e.g. "ptp=IEEE1588-2008:traceable"
    int MediaClockDirect;       // a=mediaclk:direct=<offset> is present
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

// Frees the strings and arrays of flow, by its media, and leaves it zeroed.
DTNMOS_API void DtNmosFlow_Clear(DtNmosFlow* flow);

// Makes target a copy of source; target is cleared first.
DTNMOS_API DtNmosResult DtNmosFlow_Copy(DtNmosFlow* target, const DtNmosFlow* source);

// Sets the DID and SDID pairs of an ANC flow to the count pairs of pairs, which it
// copies.
DTNMOS_API DtNmosResult DtNmosFlow_SetDidSdid(DtNmosFlow* flow,
                                              const DtNmosDidSdid* pairs, size_t count);

// The session level of an SDP.
typedef struct DtNmosSession
{
    size_t Size;
    DtNmosString Name;     // s=
    DtNmosString OriginIp; // o=, the address of the sender
    uint64_t SessionId;
    uint64_t SessionVersion;
} DtNmosSession;

DTNMOS_API void DtNmosSession_Clear(DtNmosSession* session);

// A parsed SDP: its session and its flows, which it owns.
typedef struct DtNmosSdp DtNmosSdp;

// Parses the SDP of length bytes of text. Fails with DTNMOS_E_PARSE, naming the line,
// on a malformed description, and with DTNMOS_E_INVALID_ARGUMENT on one without media
// sections.
DTNMOS_API DtNmosResult DtNmosSdp_Parse(const char* text, size_t length, DtNmosSdp** sdp);

// Returns the session of sdp; valid until sdp is freed.
DTNMOS_API const DtNmosSession* DtNmosSdp_Session(const DtNmosSdp* sdp);

// Returns the number of flows of sdp, one per media section, and the flow at index in
// the order of the sections, or null past them; valid until sdp is freed.
DTNMOS_API size_t DtNmosSdp_FlowCount(const DtNmosSdp* sdp);
DTNMOS_API const DtNmosFlow* DtNmosSdp_Flow(const DtNmosSdp* sdp, size_t index);

// Frees sdp and what it holds; null does nothing.
DTNMOS_API void DtNmosSdp_Free(DtNmosSdp* sdp);

// Writes the SDP of session with the count flows of flows into text, which is cleared
// first. A flow of leg 1 is the second path of the flow of leg 0 before it, which
// a=group:DUP pairs it with. An IPv4 multicast destination gets a TTL of 64.
DTNMOS_API DtNmosResult DtNmosSdp_Write(const DtNmosSession* session,
                                        const DtNmosFlow* flows, size_t count,
                                        DtNmosString* text);

#ifdef __cplusplus
}
#endif
