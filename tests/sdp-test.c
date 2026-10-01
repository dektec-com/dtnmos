// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# sdp-test.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of reading and writing the SDP of ST 2110 flows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/sdp.h"

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
static dtnmos_sdp* parse(const char* text)
{
    dtnmos_sdp* sdp = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    if (dtnmos_sdp_parse(text, strlen(text), &sdp, &error) != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    return sdp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_video_on_two_paths -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_video_on_two_paths(void)
{
    dtnmos_sdp* sdp = parse(video_sdp);
    REQUIRE(sdp != NULL);
    const dtnmos_session* session = dtnmos_sdp_session(sdp);
    CHECK_STR(dtnmos_string_get(&session->name), "Example of a SMPTE ST2110-20 signal");
    CHECK_STR(dtnmos_string_get(&session->origin_ip), "192.168.100.2");
    CHECK_EQ(session->session_id, 123456);
    CHECK_EQ(session->session_version, 11);
    REQUIRE(dtnmos_sdp_flow_count(sdp) == 2);
    CHECK(dtnmos_sdp_flow(sdp, 2) == NULL);

    const dtnmos_flow* primary = dtnmos_sdp_flow(sdp, 0);
    CHECK_EQ(primary->media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(dtnmos_string_get(&primary->destination_ip), "239.100.9.10");
    CHECK_EQ(primary->destination_port, 50000);
    CHECK_STR(dtnmos_string_get(&primary->source_ip), "192.168.100.2");
    CHECK_EQ(primary->payload_type, 112);
    CHECK_EQ(primary->clock_rate, 90000);
    CHECK_STR(dtnmos_string_get(&primary->ts_refclk),
              "ptp=IEEE1588-2008:39-A7-94-FF-FE-07-CB-D0:37");
    CHECK(primary->media_clock_direct);
    CHECK_EQ(primary->media_clock_offset, 0);
    CHECK_EQ(primary->leg, 0);
    const dtnmos_video_format* video = &primary->format.video;
    CHECK_EQ(video->width, 1280);
    CHECK_EQ(video->height, 720);
    CHECK_EQ(video->rate_numerator, 60000);
    CHECK_EQ(video->rate_denominator, 1001);
    CHECK_EQ(video->depth, 10);
    CHECK(!video->interlaced);
    CHECK_STR(dtnmos_string_get(&video->sampling), "YCbCr-4:2:2");
    CHECK_STR(dtnmos_string_get(&video->colorimetry), "BT709");
    CHECK_STR(dtnmos_string_get(&video->tcs), "SDR");
    CHECK_STR(dtnmos_string_get(&video->packing_mode), "2110GPM");
    CHECK_STR(dtnmos_string_get(&video->ssn), "ST2110-20:2017");
    CHECK_STR(dtnmos_string_get(&video->transmitter_type), "2110TPN");

    const dtnmos_flow* secondary = dtnmos_sdp_flow(sdp, 1);
    CHECK_EQ(secondary->leg, 1);
    CHECK_STR(dtnmos_string_get(&secondary->destination_ip), "239.101.9.10");
    CHECK_EQ(secondary->format.video.height, 1080);
    CHECK_EQ(secondary->format.video.rate_numerator, 25);
    CHECK_EQ(secondary->format.video.rate_denominator, 1);
    CHECK(secondary->format.video.interlaced);
    dtnmos_sdp_free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_reads_audio(void)
{
    dtnmos_sdp* sdp = parse(audio_sdp);
    REQUIRE(sdp != NULL);
    REQUIRE(dtnmos_sdp_flow_count(sdp) == 1);
    const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, 0);
    CHECK_EQ(flow->media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(dtnmos_string_get(&flow->source_ip), "");
    CHECK_EQ(flow->media_clock_offset, 1234);
    const dtnmos_audio_format* audio = &flow->format.audio;
    CHECK_STR(dtnmos_string_get(&audio->encoding), "L24");
    CHECK_EQ(audio->sample_rate, 48000);
    CHECK_EQ(audio->channels, 8);
    CHECK_EQ(audio->packet_time_ns, 125000);
    CHECK_STR(dtnmos_string_get(&audio->channel_order), "SMPTE2110.(SGRP,SGRP)");
    dtnmos_sdp_free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_compressed_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_compressed_video(void)
{
    dtnmos_sdp* sdp = parse(compressed_sdp);
    REQUIRE(sdp != NULL);
    const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_EQ(flow->media, DTNMOS_MEDIA_COMPRESSED_VIDEO);
    const dtnmos_compressed_video_format* video = &flow->format.compressed_video;
    CHECK_STR(dtnmos_string_get(&video->encoding), "jxsv");
    CHECK_STR(dtnmos_string_get(&video->profile), "High444.12");
    CHECK_STR(dtnmos_string_get(&video->level), "2k-1");
    CHECK_STR(dtnmos_string_get(&video->sublevel), "Sublev3bpp");
    CHECK_EQ(video->packet_mode, 0);
    CHECK_EQ(video->transmission_mode, 1);
    CHECK_EQ(video->bandwidth_kbps, 116000);
    CHECK_EQ(video->width, 1920);
    CHECK_EQ(video->rate_denominator, 1001);
    CHECK_STR(dtnmos_string_get(&video->range), "FULL");
    CHECK_STR(dtnmos_string_get(&video->transmitter_type), "2110TPNL");
    dtnmos_sdp_free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_ancillary_data -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_reads_ancillary_data(void)
{
    dtnmos_sdp* sdp = parse(anc_sdp);
    REQUIRE(sdp != NULL);
    const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_EQ(flow->media, DTNMOS_MEDIA_ANC);
    const dtnmos_anc_format* anc = &flow->format.anc;
    REQUIRE(anc->did_sdid_count == 2);
    CHECK_EQ(anc->did_sdid[0].did, 0x61);
    CHECK_EQ(anc->did_sdid[0].sdid, 0x02);
    CHECK_EQ(anc->did_sdid[1].did, 0x41);
    CHECK_EQ(anc->did_sdid[1].sdid, 0x05);
    CHECK_EQ(anc->vpid_code, 133);
    CHECK_EQ(anc->rate_numerator, 30000);
    CHECK_STR(dtnmos_string_get(&anc->transmission_model), "CTM");
    CHECK_STR(dtnmos_string_get(&anc->ssn), "ST2110-40:2023");
    dtnmos_sdp_free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_reads_other_media_as_they_are -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_reads_other_media_as_they_are(void)
{
    dtnmos_sdp* sdp = parse("v=0\r\no=- 1 1 IN IP4 10.0.0.8\r\ns=H.264\r\nt=0 0\r\n"
                            "m=video 5000 RTP/AVP 96\r\nc=IN IP4 239.0.0.9/16\r\n"
                            "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1; "
                            "profile-level-id=42e01f\r\n");
    REQUIRE(sdp != NULL);
    const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_EQ(flow->media, DTNMOS_MEDIA_OTHER);
    CHECK_STR(dtnmos_string_get(&flow->format.other.encoding), "H264");
    CHECK_STR(dtnmos_string_get(&flow->format.other.fmtp),
              "packetization-mode=1; profile-level-id=42e01f");
    dtnmos_sdp_free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- sdp_takes_defaults_of_the_session -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_takes_defaults_of_the_session(void)
{
    // c=, a=source-filter, a=ts-refclk and a=mediaclk of the session, IPv6, and a line
    // without its carriage return at the end.
    dtnmos_sdp* sdp =
        parse("v=0\no=- 2 3 IN IP6 fd00::5\ns=Defaults\nc=IN IP6 ff15::7\nt=0 0\n"
              "a=source-filter: incl IN IP6 ff15::7 "
              "fd00::5\na=ts-refclk:localmac=CA-FE-01-02-03-04\n"
              "a=mediaclk:direct=5\n"
              "m=audio 5006 RTP/AVP 98\na=rtpmap:98 L16/48000/2\na=ptime:1");
    REQUIRE(sdp != NULL);
    const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, 0);
    REQUIRE(flow != NULL);
    CHECK_STR(dtnmos_string_get(&flow->destination_ip), "ff15::7");
    CHECK_STR(dtnmos_string_get(&flow->source_ip), "fd00::5");
    CHECK_STR(dtnmos_string_get(&flow->ts_refclk), "localmac=CA-FE-01-02-03-04");
    CHECK_EQ(flow->media_clock_offset, 5);
    CHECK_STR(dtnmos_string_get(&flow->format.audio.encoding), "L16");
    CHECK_EQ(flow->format.audio.packet_time_ns, 1000000);
    CHECK_STR(dtnmos_string_get(&dtnmos_sdp_session(sdp)->origin_ip), "fd00::5");
    dtnmos_sdp_free(sdp);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- check_error -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Parses text, which must fail with code, and checks that the message holds fragment.
//
static void check_error(const char* text, dtnmos_result code, const char* fragment)
{
    dtnmos_sdp* sdp = (dtnmos_sdp*)&sdp;
    dtnmos_error error = {DTNMOS_OK, ""};
    const dtnmos_result result = dtnmos_sdp_parse(text, strlen(text), &sdp, &error);
    CHECK_EQ(result, code);
    CHECK_EQ(error.code, code);
    CHECK(sdp == NULL);
    if (strstr(error.message, fragment) == NULL)
    {
        printf("  the message \"%s\" lacks \"%s\"\n", error.message, fragment);
        CHECK(strstr(error.message, fragment) != NULL);
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
    dtnmos_sdp* sdp = NULL;
    CHECK(dtnmos_sdp_parse(NULL, 1, &sdp, NULL) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- same -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks that the strings a and b hold the same text.
//
static int same(const dtnmos_string* a, const dtnmos_string* b)
{
    return strcmp(dtnmos_string_get(a), dtnmos_string_get(b)) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- flows_equal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether flows a and b describe the same flow.
//
static int flows_equal(const dtnmos_flow* a, const dtnmos_flow* b)
{
    if (a->media != b->media || !same(&a->destination_ip, &b->destination_ip) ||
        a->destination_port != b->destination_port ||
        !same(&a->source_ip, &b->source_ip) || a->payload_type != b->payload_type ||
        a->clock_rate != b->clock_rate || !same(&a->ts_refclk, &b->ts_refclk) ||
        a->media_clock_direct != b->media_clock_direct ||
        a->media_clock_offset != b->media_clock_offset || a->leg != b->leg)
    {
        return 0;
    }
    switch (a->media)
    {
    case DTNMOS_MEDIA_VIDEO:
    {
        const dtnmos_video_format* x = &a->format.video;
        const dtnmos_video_format* y = &b->format.video;
        return x->width == y->width && x->height == y->height &&
               x->rate_numerator == y->rate_numerator &&
               x->rate_denominator == y->rate_denominator &&
               x->interlaced == y->interlaced && x->segmented == y->segmented &&
               x->depth == y->depth && same(&x->sampling, &y->sampling) &&
               same(&x->colorimetry, &y->colorimetry) && same(&x->tcs, &y->tcs) &&
               same(&x->range, &y->range) && same(&x->packing_mode, &y->packing_mode) &&
               same(&x->ssn, &y->ssn) && same(&x->transmitter_type, &y->transmitter_type);
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        const dtnmos_audio_format* x = &a->format.audio;
        const dtnmos_audio_format* y = &b->format.audio;
        return same(&x->encoding, &y->encoding) && x->sample_rate == y->sample_rate &&
               x->channels == y->channels && x->packet_time_ns == y->packet_time_ns &&
               same(&x->channel_order, &y->channel_order);
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        const dtnmos_compressed_video_format* x = &a->format.compressed_video;
        const dtnmos_compressed_video_format* y = &b->format.compressed_video;
        return same(&x->encoding, &y->encoding) && x->width == y->width &&
               x->height == y->height && x->rate_numerator == y->rate_numerator &&
               x->rate_denominator == y->rate_denominator && x->depth == y->depth &&
               same(&x->sampling, &y->sampling) && same(&x->profile, &y->profile) &&
               same(&x->level, &y->level) && same(&x->sublevel, &y->sublevel) &&
               same(&x->range, &y->range) && same(&x->ssn, &y->ssn) &&
               x->packet_mode == y->packet_mode &&
               x->transmission_mode == y->transmission_mode &&
               x->bandwidth_kbps == y->bandwidth_kbps;
    }
    case DTNMOS_MEDIA_ANC:
    {
        const dtnmos_anc_format* x = &a->format.anc;
        const dtnmos_anc_format* y = &b->format.anc;
        return x->did_sdid_count == y->did_sdid_count &&
               (x->did_sdid_count == 0 ||
                memcmp(x->did_sdid, y->did_sdid,
                       x->did_sdid_count * sizeof(*x->did_sdid)) == 0) &&
               x->vpid_code == y->vpid_code && x->rate_numerator == y->rate_numerator &&
               same(&x->transmission_model, &y->transmission_model) &&
               same(&x->ssn, &y->ssn);
    }
    case DTNMOS_MEDIA_OTHER:
        return same(&a->format.other.encoding, &b->format.other.encoding) &&
               same(&a->format.other.fmtp, &b->format.other.fmtp);
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
        dtnmos_sdp* original = parse(texts[t]);
        REQUIRE(original != NULL);
        const size_t count = dtnmos_sdp_flow_count(original);
        dtnmos_flow* flows = calloc(count, sizeof(*flows));
        REQUIRE(flows != NULL);
        for (size_t i = 0; i < count; ++i)
        {
            REQUIRE(dtnmos_flow_copy(&flows[i], dtnmos_sdp_flow(original, i)) ==
                    DTNMOS_OK);
        }
        dtnmos_string written = {0};
        dtnmos_error error = {DTNMOS_OK, ""};
        const dtnmos_result result = dtnmos_sdp_write(dtnmos_sdp_session(original), flows,
                                                      count, &written, &error);
        if (result != DTNMOS_OK)
        {
            printf("  %s\n", error.message);
        }
        CHECK(result == DTNMOS_OK);
        dtnmos_sdp* again = parse(dtnmos_string_get(&written));
        CHECK(again != NULL);
        if (again != NULL)
        {
            CHECK_EQ(dtnmos_sdp_flow_count(again), count);
            for (size_t i = 0; i < count && i < dtnmos_sdp_flow_count(again); ++i)
            {
                if (!flows_equal(dtnmos_sdp_flow(again, i), &flows[i]))
                {
                    printf("  flow %zu of SDP %zu differs after writing:\n%s\n", i, t,
                           dtnmos_string_get(&written));
                    CHECK(0);
                }
            }
            CHECK(same(&dtnmos_sdp_session(again)->name,
                       &dtnmos_sdp_session(original)->name));
        }
        dtnmos_sdp_free(again);
        dtnmos_string_clear(&written);
        for (size_t i = 0; i < count; ++i)
        {
            dtnmos_flow_clear(&flows[i]);
        }
        free(flows);
        dtnmos_sdp_free(original);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- sdp_writes_an_audio_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void sdp_writes_an_audio_sender(void)
{
    dtnmos_session session = {0};
    session.size = sizeof(session);
    dtnmos_string_set_text(&session.name, "dt2110audiosink");
    dtnmos_string_set_text(&session.origin_ip, "192.168.1.10");
    session.session_id = 42;
    session.session_version = 1;
    dtnmos_flow flow = {0};
    flow.size = sizeof(flow);
    flow.media = DTNMOS_MEDIA_AUDIO;
    dtnmos_string_set_text(&flow.destination_ip, "239.0.0.2");
    flow.destination_port = 5004;
    flow.payload_type = 97;
    flow.clock_rate = 48000;
    dtnmos_string_set_text(&flow.ts_refclk, "localmac=00-14-F4-01-02-03");
    flow.media_clock_direct = 1;
    dtnmos_string_set_text(&flow.format.audio.encoding, "L24");
    flow.format.audio.sample_rate = 48000;
    flow.format.audio.channels = 2;
    flow.format.audio.packet_time_ns = 1000000;
    dtnmos_string text = {0};
    REQUIRE(dtnmos_sdp_write(&session, &flow, 1, &text, NULL) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&text), "v=0\r\n"
                                        "o=- 42 1 IN IP4 192.168.1.10\r\n"
                                        "s=dt2110audiosink\r\n"
                                        "t=0 0\r\n"
                                        "m=audio 5004 RTP/AVP 97\r\n"
                                        "c=IN IP4 239.0.0.2/64\r\n"
                                        "a=rtpmap:97 L24/48000/2\r\n"
                                        "a=ptime:1\r\n"
                                        "a=ts-refclk:localmac=00-14-F4-01-02-03\r\n"
                                        "a=mediaclk:direct=0\r\n");
    dtnmos_string_clear(&text);
    dtnmos_flow_clear(&flow);
    dtnmos_session_clear(&session);
}

// .-.-.-.-.-.-.-.-.-.-.- sdp_refuses_to_write_an_incomplete_flow -.-.-.-.-.-.-.-.-.-.-.-.
//
void sdp_refuses_to_write_an_incomplete_flow(void)
{
    dtnmos_session session = {0};
    session.size = sizeof(session);
    dtnmos_string_set_text(&session.origin_ip, "10.0.0.1");
    dtnmos_flow flows[2] = {{0}, {0}};
    flows[0].size = sizeof(flows[0]);
    flows[1].size = sizeof(flows[1]);
    flows[0].media = DTNMOS_MEDIA_OTHER;
    dtnmos_string text = {0};
    dtnmos_error error = {DTNMOS_OK, ""};
    CHECK(dtnmos_sdp_write(&session, flows, 1, &text, &error) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(error.message, "destination") != NULL);
    dtnmos_string_set_text(&flows[0].destination_ip, "239.0.0.1");
    flows[0].destination_port = 5000;
    REQUIRE(dtnmos_flow_copy(&flows[1], &flows[0]) == DTNMOS_OK);
    dtnmos_string_set_text(&flows[1].destination_ip, "239.0.0.2");
    flows[1].leg = 1;
    flows[0].leg = 1;
    CHECK(dtnmos_sdp_write(&session, flows, 2, &text, &error) ==
          DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(error.message, "second path") != NULL);
    flows[0].leg = 0;
    REQUIRE(dtnmos_sdp_write(&session, flows, 2, &text, &error) == DTNMOS_OK);
    CHECK(strstr(dtnmos_string_get(&text), "a=group:DUP primary0 secondary1\r\n") !=
          NULL);
    dtnmos_session empty = {0};
    empty.size = sizeof(empty);
    CHECK(dtnmos_sdp_write(&empty, flows, 1, &text, &error) == DTNMOS_E_INVALID_ARGUMENT);
    dtnmos_string_clear(&text);
    dtnmos_flow_clear(&flows[0]);
    dtnmos_flow_clear(&flows[1]);
    dtnmos_session_clear(&session);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- flow_copy_owns_its_strings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void flow_copy_owns_its_strings(void)
{
    dtnmos_sdp* sdp = parse(anc_sdp);
    REQUIRE(sdp != NULL);
    dtnmos_flow copy = {0};
    REQUIRE(dtnmos_flow_copy(&copy, dtnmos_sdp_flow(sdp, 0)) == DTNMOS_OK);
    CHECK(copy.format.anc.did_sdid != dtnmos_sdp_flow(sdp, 0)->format.anc.did_sdid);
    dtnmos_sdp_free(sdp);
    // What the copy holds survives the SDP it came from.
    CHECK_EQ(copy.format.anc.did_sdid_count, 2);
    CHECK_EQ(copy.format.anc.did_sdid[1].did, 0x41);
    CHECK_STR(dtnmos_string_get(&copy.format.anc.ssn), "ST2110-40:2023");
    CHECK_STR(dtnmos_string_get(&copy.destination_ip), "239.100.9.11");
    // Copying over a flow that holds strings frees them.
    dtnmos_sdp* other = parse(video_sdp);
    REQUIRE(other != NULL);
    REQUIRE(dtnmos_flow_copy(&copy, dtnmos_sdp_flow(other, 0)) == DTNMOS_OK);
    CHECK_EQ(copy.media, DTNMOS_MEDIA_VIDEO);
    dtnmos_sdp_free(other);
    CHECK_STR(dtnmos_string_get(&copy.format.video.transmitter_type), "2110TPN");
    dtnmos_flow_clear(&copy);
    CHECK(copy.media == DTNMOS_MEDIA_VIDEO);
}
