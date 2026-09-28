// SPDX-License-Identifier: BSD-3-Clause
//
// dtnmos: the flows of an SDP (RFC 8866) as SMPTE ST 2110-20, -22, -30 and -40 describe
// them, read from its text and written to it. The parser reports what a description says
// and judges no format: whether a receiver can carry a flow is for the receiver to say.

#ifndef DTNMOS_SDP_H
#define DTNMOS_SDP_H

#include "dtnmos/dtnmos.h"

#ifdef __cplusplus
extern "C"
{
#endif

  // What a media section carries, from the encoding of its a=rtpmap.
  typedef enum dtnmos_media
  {
    DTNMOS_MEDIA_VIDEO = 0,             // ST 2110-20, uncompressed video: raw (RFC 4175)
    DTNMOS_MEDIA_AUDIO = 1,             // ST 2110-30 and -31: L16, L24 or AM824
    DTNMOS_MEDIA_COMPRESSED_VIDEO = 2,  // ST 2110-22: jxsv, JPEG XS (RFC 9134)
    DTNMOS_MEDIA_ANC = 3,               // ST 2110-40: smpte291, ancillary data (RFC 8331)
    DTNMOS_MEDIA_OTHER = 99             // anything else, with its encoding and raw fmtp
  } dtnmos_media;

  // Returns the name of a media, e.g. "video"; a static string.
  DTNMOS_API const char* dtnmos_media_name(dtnmos_media media);

  // An uncompressed video format as the a=fmtp of ST 2110-20 states it. The strings keep
  // the text of the SDP, e.g. sampling "YCbCr-4:2:2", colorimetry "BT709", tcs "SDR",
  // transmitter_type "2110TPN"; a parameter the SDP does not give is 0 or empty.
  typedef struct dtnmos_video_format
  {
    uint32_t width;
    uint32_t height;
    uint32_t rate_numerator;  // exactframerate, e.g. 30000/1001, or 25/1
    uint32_t rate_denominator;
    int interlaced;  // interlace
    int segmented;   // segmented, PsF
    uint32_t depth;  // bits per sample
    dtnmos_string sampling;
    dtnmos_string colorimetry;
    dtnmos_string tcs;               // TCS
    dtnmos_string range;             // RANGE
    dtnmos_string packing_mode;      // PM
    dtnmos_string ssn;               // SSN, e.g. "ST2110-20:2017"
    dtnmos_string transmitter_type;  // TP
  } dtnmos_video_format;

  // An audio format as a=rtpmap, a=ptime and a=fmtp of ST 2110-30 state it.
  typedef struct dtnmos_audio_format
  {
    dtnmos_string encoding;  // "L24", "L16" or "AM824"
    uint32_t sample_rate;    // e.g. 48000
    uint32_t channels;
    uint32_t packet_time_ns;  // a=ptime: 1000000 for 1 ms, 125000 for 0.125 ms
    dtnmos_string
        channel_order;  // channel-order, e.g. "SMPTE2110.(ST,ST)"; empty if absent
  } dtnmos_audio_format;

  // A compressed video format as ST 2110-22 states it: the raster and colour of ST
  // 2110-20, the parameters of the codec in a=fmtp (JPEG XS: profile, level, sublevel,
  // packetmode and transmode), and the bandwidth of b=AS.
  typedef struct dtnmos_compressed_video_format
  {
    dtnmos_string encoding;  // e.g. "jxsv"
    uint32_t width;
    uint32_t height;
    uint32_t rate_numerator;
    uint32_t rate_denominator;
    int interlaced;
    int segmented;
    uint32_t depth;
    dtnmos_string sampling;
    dtnmos_string colorimetry;
    dtnmos_string tcs;
    dtnmos_string range;
    dtnmos_string ssn;
    dtnmos_string transmitter_type;
    dtnmos_string profile;
    dtnmos_string level;
    dtnmos_string sublevel;
    uint32_t packet_mode;        // packetmode
    uint32_t transmission_mode;  // transmode; 1 when absent, as RFC 9134 says
    uint64_t bandwidth_kbps;     // b=AS; 0 when absent
  } dtnmos_compressed_video_format;

  // One DID and SDID pair of ancillary data.
  typedef struct dtnmos_did_sdid
  {
    uint8_t did;
    uint8_t sdid;
  } dtnmos_did_sdid;

  // An ANC format as ST 2110-40 states it in a=fmtp.
  typedef struct dtnmos_anc_format
  {
    const dtnmos_did_sdid* did_sdid;  // DID_SDID, owned by the flow; null when absent
    size_t did_sdid_count;
    uint32_t vpid_code;       // VPID_Code; 0 when absent
    uint32_t rate_numerator;  // exactframerate; 0 when absent
    uint32_t rate_denominator;
    dtnmos_string transmission_model;  // TM, e.g. "CTM"; empty when absent
    dtnmos_string ssn;
  } dtnmos_anc_format;

  // A media section the parser does not know: its encoding from a=rtpmap and its a=fmtp
  // as it stands.
  typedef struct dtnmos_other_format
  {
    dtnmos_string encoding;
    dtnmos_string fmtp;
  } dtnmos_other_format;

  // One RTP flow: a media section of an SDP.
  typedef struct dtnmos_flow
  {
    size_t size;  // sizeof(dtnmos_flow), set by whoever fills it
    dtnmos_media media;
    dtnmos_string destination_ip;  // c=, IPv4 or IPv6, multicast or unicast, without TTL
    uint16_t destination_port;     // m=
    dtnmos_string source_ip;  // a=source-filter: incl; empty receives from any source
    uint8_t payload_type;     // the first of m=
    uint32_t clock_rate;      // a=rtpmap: 90000 for video, the sample rate for audio
    dtnmos_string ts_refclk;  // a=ts-refclk, e.g. "ptp=IEEE1588-2008:traceable"
    int media_clock_direct;   // a=mediaclk:direct=<offset> is present
    uint32_t media_clock_offset;
    uint32_t leg;  // 0, or 1 for the second path of ST 2022-7 (a=group:DUP)
    union
    {
      dtnmos_video_format video;
      dtnmos_audio_format audio;
      dtnmos_compressed_video_format compressed_video;
      dtnmos_anc_format anc;
      dtnmos_other_format other;
    } format;  // the member of media
  } dtnmos_flow;

  // Frees the strings and arrays of flow, by its media, and leaves it zeroed.
  DTNMOS_API void dtnmos_flow_clear(dtnmos_flow* flow);

  // Makes target a copy of source; target is cleared first.
  DTNMOS_API dtnmos_result dtnmos_flow_copy(dtnmos_flow* target,
                                            const dtnmos_flow* source);

  // Sets the DID and SDID pairs of an ANC flow to the count pairs of pairs, which it
  // copies.
  DTNMOS_API dtnmos_result dtnmos_flow_set_did_sdid(dtnmos_flow* flow,
                                                    const dtnmos_did_sdid* pairs,
                                                    size_t count);

  // The session level of an SDP.
  typedef struct dtnmos_session
  {
    size_t size;
    dtnmos_string name;       // s=
    dtnmos_string origin_ip;  // o=, the address of the sender
    uint64_t session_id;
    uint64_t session_version;
  } dtnmos_session;

  DTNMOS_API void dtnmos_session_clear(dtnmos_session* session);

  // A parsed SDP: its session and its flows, which it owns.
  typedef struct dtnmos_sdp dtnmos_sdp;

  // Parses the SDP of length bytes of text. Fails with DTNMOS_E_PARSE, naming the line,
  // on a malformed description, and with DTNMOS_E_INVALID_ARGUMENT on one without media
  // sections.
  DTNMOS_API dtnmos_result dtnmos_sdp_parse(const char* text, size_t length,
                                            dtnmos_sdp** sdp, dtnmos_error* error);

  // Returns the session of sdp; valid until sdp is freed.
  DTNMOS_API const dtnmos_session* dtnmos_sdp_session(const dtnmos_sdp* sdp);

  // Returns the number of flows of sdp, one per media section, and the flow at index in
  // the order of the sections, or null past them; valid until sdp is freed.
  DTNMOS_API size_t dtnmos_sdp_flow_count(const dtnmos_sdp* sdp);
  DTNMOS_API const dtnmos_flow* dtnmos_sdp_flow(const dtnmos_sdp* sdp, size_t index);

  // Frees sdp and what it holds; null does nothing.
  DTNMOS_API void dtnmos_sdp_free(dtnmos_sdp* sdp);

  // Writes the SDP of session with the count flows of flows into text, which is cleared
  // first. A flow of leg 1 is the second path of the flow of leg 0 before it, which
  // a=group:DUP pairs it with. An IPv4 multicast destination gets a TTL of 64.
  DTNMOS_API dtnmos_result dtnmos_sdp_write(const dtnmos_session* session,
                                            const dtnmos_flow* flows, size_t count,
                                            dtnmos_string* text, dtnmos_error* error);

#ifdef __cplusplus
}
#endif

#endif  // DTNMOS_SDP_H
