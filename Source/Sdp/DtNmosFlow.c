// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosFlow.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Clearing and copying flows and sessions, whose strings and arrays they own
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "dtnmos_sdp.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clear_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void clear_video(DtNmosVideoFormat* video)
{
    DtNmosString_Clear(&video->Sampling);
    DtNmosString_Clear(&video->Colorimetry);
    DtNmosString_Clear(&video->Tcs);
    DtNmosString_Clear(&video->Range);
    DtNmosString_Clear(&video->PackingMode);
    DtNmosString_Clear(&video->Ssn);
    DtNmosString_Clear(&video->TransmitterType);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clear_compressed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void clear_compressed(DtNmosCompressedVideoFormat* video)
{
    DtNmosString_Clear(&video->Encoding);
    DtNmosString_Clear(&video->Sampling);
    DtNmosString_Clear(&video->Colorimetry);
    DtNmosString_Clear(&video->Tcs);
    DtNmosString_Clear(&video->Range);
    DtNmosString_Clear(&video->Ssn);
    DtNmosString_Clear(&video->TransmitterType);
    DtNmosString_Clear(&video->Profile);
    DtNmosString_Clear(&video->Level);
    DtNmosString_Clear(&video->Sublevel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosFlow_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosFlow_Clear(DtNmosFlow* flow)
{
    if (flow == NULL)
    {
        return;
    }
    DtNmosString_Clear(&flow->DestinationIp);
    DtNmosString_Clear(&flow->SourceIp);
    DtNmosString_Clear(&flow->TsRefclk);
    switch (flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        clear_video(&flow->Format.Video);
        break;
    case DTNMOS_MEDIA_AUDIO:
        DtNmosString_Clear(&flow->Format.Audio.Encoding);
        DtNmosString_Clear(&flow->Format.Audio.ChannelOrder);
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        clear_compressed(&flow->Format.CompressedVideo);
        break;
    case DTNMOS_MEDIA_ANC:
        free((void*)flow->Format.Anc.DidSdid);
        DtNmosString_Clear(&flow->Format.Anc.TransmissionModel);
        DtNmosString_Clear(&flow->Format.Anc.Ssn);
        break;
    case DTNMOS_MEDIA_OTHER:
        DtNmosString_Clear(&flow->Format.Other.Encoding);
        DtNmosString_Clear(&flow->Format.Other.Fmtp);
        break;
    }
    memset(flow, 0, sizeof(*flow));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosFlow_SetDidSdid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosFlow_SetDidSdid(DtNmosFlow* flow, const DtNmosDidSdid* pairs,
                                   size_t count)
{
    if (flow == NULL || (pairs == NULL && count > 0) || flow->Media != DTNMOS_MEDIA_ANC)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    DtNmosDidSdid* copy = NULL;
    if (count > 0)
    {
        copy = malloc(count * sizeof(*copy));
        if (copy == NULL)
        {
            return DTNMOS_E_NO_MEMORY;
        }
        memcpy(copy, pairs, count * sizeof(*copy));
    }
    free((void*)flow->Format.Anc.DidSdid);
    flow->Format.Anc.DidSdid = copy;
    flow->Format.Anc.DidSdidCount = count;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_strings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies each string of the pairs in target and source; returns the first failure.
//
static DtNmosResult copy_strings(DtNmosString* const* targets,
                                 const DtNmosString* const* sources, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        const DtNmosResult result = DtNmosString_Copy(targets[i], sources[i]);
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
static DtNmosResult copy_format(DtNmosFlow* target, const DtNmosFlow* source)
{
    switch (source->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
    {
        DtNmosVideoFormat* t = &target->Format.Video;
        const DtNmosVideoFormat* s = &source->Format.Video;
        DtNmosString* const targets[] = {&t->Sampling,       &t->Colorimetry, &t->Tcs,
                                         &t->Range,          &t->PackingMode, &t->Ssn,
                                         &t->TransmitterType};
        const DtNmosString* const sources[] = {
            &s->Sampling, &s->Colorimetry,    &s->Tcs, &s->Range, &s->PackingMode,
            &s->Ssn,      &s->TransmitterType};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        DtNmosAudioFormat* t = &target->Format.Audio;
        const DtNmosAudioFormat* s = &source->Format.Audio;
        DtNmosString* const targets[] = {&t->Encoding, &t->ChannelOrder};
        const DtNmosString* const sources[] = {&s->Encoding, &s->ChannelOrder};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        DtNmosCompressedVideoFormat* t = &target->Format.CompressedVideo;
        const DtNmosCompressedVideoFormat* s = &source->Format.CompressedVideo;
        DtNmosString* const targets[] = {
            &t->Encoding, &t->Sampling,        &t->Colorimetry, &t->Tcs,   &t->Range,
            &t->Ssn,      &t->TransmitterType, &t->Profile,     &t->Level, &t->Sublevel};
        const DtNmosString* const sources[] = {
            &s->Encoding, &s->Sampling,        &s->Colorimetry, &s->Tcs,   &s->Range,
            &s->Ssn,      &s->TransmitterType, &s->Profile,     &s->Level, &s->Sublevel};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    case DTNMOS_MEDIA_ANC:
    {
        DtNmosAncFormat* t = &target->Format.Anc;
        const DtNmosAncFormat* s = &source->Format.Anc;
        DtNmosString* const targets[] = {&t->TransmissionModel, &t->Ssn};
        const DtNmosString* const sources[] = {&s->TransmissionModel, &s->Ssn};
        const DtNmosResult result = copy_strings(targets, sources, DTNMOS_COUNT(targets));
        if (result != DTNMOS_OK)
        {
            return result;
        }
        return DtNmosFlow_SetDidSdid(target, s->DidSdid, s->DidSdidCount);
    }
    case DTNMOS_MEDIA_OTHER:
    {
        DtNmosOtherFormat* t = &target->Format.Other;
        const DtNmosOtherFormat* s = &source->Format.Other;
        DtNmosString* const targets[] = {&t->Encoding, &t->Fmtp};
        const DtNmosString* const sources[] = {&s->Encoding, &s->Fmtp};
        return copy_strings(targets, sources, DTNMOS_COUNT(targets));
    }
    }
    return DTNMOS_E_INVALID_ARGUMENT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_numbers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies the numbers of the format of source, which copy_format() leaves alone.
//
static void copy_numbers(DtNmosFlow* target, const DtNmosFlow* source)
{
    switch (source->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
    {
        const DtNmosVideoFormat* s = &source->Format.Video;
        DtNmosVideoFormat* t = &target->Format.Video;
        t->Width = s->Width;
        t->Height = s->Height;
        t->RateNumerator = s->RateNumerator;
        t->RateDenominator = s->RateDenominator;
        t->Interlaced = s->Interlaced;
        t->Segmented = s->Segmented;
        t->Depth = s->Depth;
        break;
    }
    case DTNMOS_MEDIA_AUDIO:
    {
        const DtNmosAudioFormat* s = &source->Format.Audio;
        DtNmosAudioFormat* t = &target->Format.Audio;
        t->SampleRate = s->SampleRate;
        t->Channels = s->Channels;
        t->PacketTimeNs = s->PacketTimeNs;
        break;
    }
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
    {
        const DtNmosCompressedVideoFormat* s = &source->Format.CompressedVideo;
        DtNmosCompressedVideoFormat* t = &target->Format.CompressedVideo;
        t->Width = s->Width;
        t->Height = s->Height;
        t->RateNumerator = s->RateNumerator;
        t->RateDenominator = s->RateDenominator;
        t->Interlaced = s->Interlaced;
        t->Segmented = s->Segmented;
        t->Depth = s->Depth;
        t->PacketMode = s->PacketMode;
        t->TransmissionMode = s->TransmissionMode;
        t->BandwidthKbps = s->BandwidthKbps;
        break;
    }
    case DTNMOS_MEDIA_ANC:
    {
        const DtNmosAncFormat* s = &source->Format.Anc;
        DtNmosAncFormat* t = &target->Format.Anc;
        t->VpidCode = s->VpidCode;
        t->RateNumerator = s->RateNumerator;
        t->RateDenominator = s->RateDenominator;
        break;
    }
    case DTNMOS_MEDIA_OTHER:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosFlow_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosFlow_Copy(DtNmosFlow* target, const DtNmosFlow* source)
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
    DtNmosFlow copy;
    memset(&copy, 0, sizeof(copy));
    copy.Size = source->Size;
    copy.Media = source->Media;
    copy.DestinationPort = source->DestinationPort;
    copy.PayloadType = source->PayloadType;
    copy.ClockRate = source->ClockRate;
    copy.MediaClockDirect = source->MediaClockDirect;
    copy.MediaClockOffset = source->MediaClockOffset;
    copy.Leg = source->Leg;
    copy_numbers(&copy, source);
    DtNmosString* const targets[] = {&copy.DestinationIp, &copy.SourceIp, &copy.TsRefclk};
    const DtNmosString* const sources[] = {&source->DestinationIp, &source->SourceIp,
                                           &source->TsRefclk};
    DtNmosResult result = copy_strings(targets, sources, DTNMOS_COUNT(targets));
    if (result == DTNMOS_OK)
    {
        result = copy_format(&copy, source);
    }
    if (result != DTNMOS_OK)
    {
        DtNmosFlow_Clear(&copy);
        return result;
    }
    DtNmosFlow_Clear(target);
    *target = copy;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSession_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosSession_Clear(DtNmosSession* session)
{
    if (session == NULL)
    {
        return;
    }
    DtNmosString_Clear(&session->Name);
    DtNmosString_Clear(&session->OriginIp);
    memset(session, 0, sizeof(*session));
}
