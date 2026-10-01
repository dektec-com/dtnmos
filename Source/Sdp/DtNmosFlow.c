// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosFlow.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Clearing and copying flows and sessions, whose strings and arrays they own
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "dtnmos/sdp.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clear_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void clear_video(dtnmos_video_format* video)
{
    dtnmos_string_clear(&video->sampling);
    dtnmos_string_clear(&video->colorimetry);
    dtnmos_string_clear(&video->tcs);
    dtnmos_string_clear(&video->range);
    dtnmos_string_clear(&video->packing_mode);
    dtnmos_string_clear(&video->ssn);
    dtnmos_string_clear(&video->transmitter_type);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clear_compressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void clear_compressed(dtnmos_compressed_video_format* video)
{
    dtnmos_string_clear(&video->encoding);
    dtnmos_string_clear(&video->sampling);
    dtnmos_string_clear(&video->colorimetry);
    dtnmos_string_clear(&video->tcs);
    dtnmos_string_clear(&video->range);
    dtnmos_string_clear(&video->ssn);
    dtnmos_string_clear(&video->transmitter_type);
    dtnmos_string_clear(&video->profile);
    dtnmos_string_clear(&video->level);
    dtnmos_string_clear(&video->sublevel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_flow_clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void dtnmos_flow_clear(dtnmos_flow* flow)
{
    if (flow == NULL)
    {
        return;
    }
    dtnmos_string_clear(&flow->destination_ip);
    dtnmos_string_clear(&flow->source_ip);
    dtnmos_string_clear(&flow->ts_refclk);
    switch (flow->media)
    {
    case DTNMOS_MEDIA_VIDEO:
        clear_video(&flow->format.video);
        break;
    case DTNMOS_MEDIA_AUDIO:
        dtnmos_string_clear(&flow->format.audio.encoding);
        dtnmos_string_clear(&flow->format.audio.channel_order);
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        clear_compressed(&flow->format.compressed_video);
        break;
    case DTNMOS_MEDIA_ANC:
        free((void*)flow->format.anc.did_sdid);
        dtnmos_string_clear(&flow->format.anc.transmission_model);
        dtnmos_string_clear(&flow->format.anc.ssn);
        break;
    case DTNMOS_MEDIA_OTHER:
        dtnmos_string_clear(&flow->format.other.encoding);
        dtnmos_string_clear(&flow->format.other.fmtp);
        break;
    }
    memset(flow, 0, sizeof(*flow));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_flow_set_did_sdid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_flow_set_did_sdid(dtnmos_flow* flow, const dtnmos_did_sdid* pairs,
                                       size_t count)
{
    if (flow == NULL || (pairs == NULL && count > 0) || flow->media != DTNMOS_MEDIA_ANC)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    dtnmos_did_sdid* copy = NULL;
    if (count > 0)
    {
        copy = malloc(count * sizeof(*copy));
        if (copy == NULL)
        {
            return DTNMOS_E_NO_MEMORY;
        }
        memcpy(copy, pairs, count * sizeof(*copy));
    }
    free((void*)flow->format.anc.did_sdid);
    flow->format.anc.did_sdid = copy;
    flow->format.anc.did_sdid_count = count;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_strings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies each string of the pairs in target and source; returns the first failure.
//
static dtnmos_result copy_strings(dtnmos_string* const* targets,
                                  const dtnmos_string* const* sources, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        const dtnmos_result result = dtnmos_string_copy(targets[i], sources[i]);
        if (result != DTNMOS_OK)
        {
            return result;
        }
    }
    return DTNMOS_OK;
}

#define DTNMOS_COUNT(array) (sizeof(array) / sizeof((array)[0]))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_format -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static dtnmos_result copy_format(dtnmos_flow* target, const dtnmos_flow* source)
{
    switch (source->media)
    {
    case DTNMOS_MEDIA_VIDEO:
    {
        dtnmos_video_format* t = &target->format.video;
        const dtnmos_video_format* s = &source->format.video;
        dtnmos_string* const targets[] = {&t->sampling,        &t->colorimetry,  &t->tcs,
                                          &t->range,           &t->packing_mode, &t->ssn,
                                          &t->transmitter_type};
        const dtnmos_string* const sources[] = {
            &s->sampling, &s->colorimetry,     &s->tcs, &s->range, &s->packing_mode,
            &s->ssn,      &s->transmitter_type};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        dtnmos_audio_format* t = &target->format.audio;
        const dtnmos_audio_format* s = &source->format.audio;
        dtnmos_string* const targets[] = {&t->encoding, &t->channel_order};
        const dtnmos_string* const sources[] = {&s->encoding, &s->channel_order};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        dtnmos_compressed_video_format* t = &target->format.compressed_video;
        const dtnmos_compressed_video_format* s = &source->format.compressed_video;
        dtnmos_string* const targets[] = {
            &t->encoding, &t->sampling,         &t->colorimetry, &t->tcs,   &t->range,
            &t->ssn,      &t->transmitter_type, &t->profile,     &t->level, &t->sublevel};
        const dtnmos_string* const sources[] = {
            &s->encoding, &s->sampling,         &s->colorimetry, &s->tcs,   &s->range,
            &s->ssn,      &s->transmitter_type, &s->profile,     &s->level, &s->sublevel};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    case DTNMOS_MEDIA_ANC:
    {
        dtnmos_anc_format* t = &target->format.anc;
        const dtnmos_anc_format* s = &source->format.anc;
        dtnmos_string* const targets[] = {&t->transmission_model, &t->ssn};
        const dtnmos_string* const sources[] = {&s->transmission_model, &s->ssn};
        const dtnmos_result result =
            copy_strings(targets, sources, DTNMOS_COUNT(targets));
        if (result != DTNMOS_OK)
        {
            return result;
        }
        return dtnmos_flow_set_did_sdid(target, s->did_sdid, s->did_sdid_count);
    }
    case DTNMOS_MEDIA_OTHER:
    {
        dtnmos_other_format* t = &target->format.other;
        const dtnmos_other_format* s = &source->format.other;
        dtnmos_string* const targets[] = {&t->encoding, &t->fmtp};
        const dtnmos_string* const sources[] = {&s->encoding, &s->fmtp};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    }
    return DTNMOS_E_INVALID_ARGUMENT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_numbers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies the numbers of the format of source, which copy_format() leaves alone.
//
static void copy_numbers(dtnmos_flow* target, const dtnmos_flow* source)
{
    switch (source->media)
    {
    case DTNMOS_MEDIA_VIDEO:
    {
        const dtnmos_video_format* s = &source->format.video;
        dtnmos_video_format* t = &target->format.video;
        t->width = s->width;
        t->height = s->height;
        t->rate_numerator = s->rate_numerator;
        t->rate_denominator = s->rate_denominator;
        t->interlaced = s->interlaced;
        t->segmented = s->segmented;
        t->depth = s->depth;
        break;
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        const dtnmos_audio_format* s = &source->format.audio;
        dtnmos_audio_format* t = &target->format.audio;
        t->sample_rate = s->sample_rate;
        t->channels = s->channels;
        t->packet_time_ns = s->packet_time_ns;
        break;
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        const dtnmos_compressed_video_format* s = &source->format.compressed_video;
        dtnmos_compressed_video_format* t = &target->format.compressed_video;
        t->width = s->width;
        t->height = s->height;
        t->rate_numerator = s->rate_numerator;
        t->rate_denominator = s->rate_denominator;
        t->interlaced = s->interlaced;
        t->segmented = s->segmented;
        t->depth = s->depth;
        t->packet_mode = s->packet_mode;
        t->transmission_mode = s->transmission_mode;
        t->bandwidth_kbps = s->bandwidth_kbps;
        break;
    }
    case DTNMOS_MEDIA_ANC:
    {
        const dtnmos_anc_format* s = &source->format.anc;
        dtnmos_anc_format* t = &target->format.anc;
        t->vpid_code = s->vpid_code;
        t->rate_numerator = s->rate_numerator;
        t->rate_denominator = s->rate_denominator;
        break;
    }
    case DTNMOS_MEDIA_OTHER:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_flow_copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
dtnmos_result dtnmos_flow_copy(dtnmos_flow* target, const dtnmos_flow* source)
{
    if (target == NULL || source == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    if (target == source)
    {
        return DTNMOS_OK;
    }
    // The copy is built apart and moved into target only when all of it was copied.
    dtnmos_flow copy;
    memset(&copy, 0, sizeof(copy));
    copy.size = source->size;
    copy.media = source->media;
    copy.destination_port = source->destination_port;
    copy.payload_type = source->payload_type;
    copy.clock_rate = source->clock_rate;
    copy.media_clock_direct = source->media_clock_direct;
    copy.media_clock_offset = source->media_clock_offset;
    copy.leg = source->leg;
    copy_numbers(&copy, source);
    dtnmos_string* const targets[] = {&copy.destination_ip, &copy.source_ip,
                                      &copy.ts_refclk};
    const dtnmos_string* const sources[] = {&source->destination_ip, &source->source_ip,
                                            &source->ts_refclk};
    dtnmos_result result = copy_strings(targets, sources, DTNMOS_COUNT(targets));
    if (result == DTNMOS_OK)
    {
        result = copy_format(&copy, source);
    }
    if (result != DTNMOS_OK)
    {
        dtnmos_flow_clear(&copy);
        return result;
    }
    dtnmos_flow_clear(target);
    *target = copy;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_session_clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_session_clear(dtnmos_session* session)
{
    if (session == NULL)
    {
        return;
    }
    dtnmos_string_clear(&session->name);
    dtnmos_string_clear(&session->origin_ip);
    memset(session, 0, sizeof(*session));
}
