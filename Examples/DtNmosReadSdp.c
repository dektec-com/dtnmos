// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtNmosReadSdp.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Example: reads the SDP of an ST 2110 sender and prints its flows
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Prints the session and one line per flow, a media section: its media, where it is
// sent, from where, and its format. With --write it writes the flows back as the SDP a
// sender of dtnmos would give.
//
//     session "Camera 1" from 192.168.1.10
//     flow 0: video 239.10.1.1:5004 from 192.168.1.10, PT 96, 1920x1080i 25/1 ...
//     flow 1: video 239.20.1.1:5004 from 192.168.2.10, PT 96, 1920x1080i 25/1 ... (leg 1)
//     flow 2: audio 239.10.1.2:5006 from 192.168.1.10, PT 97, L24 48000 Hz 2 ch, 1000 us
//
// Exits with 0 when the file is read, and 1 when it cannot be.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>

#include "Common/ExampleCommon.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption Options[] = {
    {"--file", true, "The SDP file to read"},
    {"--write", false, "Writes the flows back as an SDP"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints the format of Flow, which its media says how to read.
//
static void PrintFormat(const DtNmosFlow* Flow)
{
    if (Flow->Media == DTNMOS_MEDIA_VIDEO)
    {
        const DtNmosVideoFormat* Video = &Flow->Format.Video;
        printf("%ux%u%s %u/%u %s %u bit", (unsigned)Video->Width, (unsigned)Video->Height,
               Video->Interlaced ? "i" : "p", (unsigned)Video->RateNumerator,
               (unsigned)Video->RateDenominator, DtNmosSampling_Text(Video->Sampling),
               (unsigned)Video->Depth);
    }
    else if (Flow->Media == DTNMOS_MEDIA_AUDIO)
    {
        const DtNmosAudioFormat* Audio = &Flow->Format.Audio;
        printf("%s %u Hz %u ch, %u us", DtNmosAudioEncoding_Text(Audio->Encoding),
               (unsigned)Audio->SampleRate, (unsigned)Audio->Channels,
               (unsigned)(Audio->PacketTimeNs / 1000));
    }
    else if (Flow->Media == DTNMOS_MEDIA_COMPRESSED_VIDEO)
    {
        const DtNmosCompressedVideoFormat* Video = &Flow->Format.CompressedVideo;
        printf("%s %ux%u %u/%u, %llu kb/s", Video->Encoding, (unsigned)Video->Width,
               (unsigned)Video->Height, (unsigned)Video->RateNumerator,
               (unsigned)Video->RateDenominator,
               (unsigned long long)Video->BandwidthKbps);
    }
    else if (Flow->Media == DTNMOS_MEDIA_ANC)
    {
        printf("%zu DID/SDID pair(s)", Flow->Format.Anc.DidSdidCount);
    }
    else
    {
        printf("%s", Flow->Format.Other.Encoding);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteBack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtNmosSdp_Write with no buffer reports the size the text needs, with
// DTNMOS_E_BUFFER_TOO_SMALL; the second call fills a buffer of that size.
//
static int WriteBack(const DtNmosSdp* Sdp)
{
    const size_t Count = DtNmosSdp_FlowCount(Sdp);
    DtNmosFlow* Flows = calloc(Count, sizeof(DtNmosFlow));
    if (Flows == NULL)
    {
        printf("Out of memory\n");
        return EXAMPLE_FAILED;
    }
    for (size_t i = 0; i < Count; i++)
    {
        Flows[i] = *DtNmosSdp_Flow(Sdp, i);
    }
    size_t Size = 0;
    DtNmosResult Result =
        DtNmosSdp_Write(DtNmosSdp_Session(Sdp), Flows, Count, NULL, &Size);
    char* Text = Result == DTNMOS_E_BUFFER_TOO_SMALL ? malloc(Size) : NULL;
    if (Text != NULL)
    {
        Result = DtNmosSdp_Write(DtNmosSdp_Session(Sdp), Flows, Count, Text, &Size);
    }
    free(Flows);
    if (Result != DTNMOS_OK)
    {
        free(Text);
        return Example_Failed("DtNmosSdp_Write", Result);
    }
    printf("\n%s", Text);
    free(Text);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    if (!Example_CheckArguments(
            Argc, Argv, "Reads the SDP of an ST 2110 sender and prints its flows.",
            Options, (int)(sizeof(Options) / sizeof(Options[0]))))
    {
        return EXAMPLE_FAILED;
    }
    const char* Path = Example_Value(Argc, Argv, "--file");
    if (Path == NULL)
    {
        printf("Give the SDP file with --file\n");
        return EXAMPLE_FAILED;
    }
    char* Text = NULL;
    size_t Length = 0;
    if (!Example_ReadFile(Path, &Text, &Length))
    {
        return EXAMPLE_FAILED;
    }
    DtNmosSdp* Sdp = NULL;
    const DtNmosResult Result = DtNmosSdp_Parse(Text, Length, &Sdp);
    free(Text);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosSdp_Parse", Result);
    }

    const DtNmosSession* Session = DtNmosSdp_Session(Sdp);
    printf("session \"%s\" from %s\n", Session->Name == NULL ? "" : Session->Name,
           Session->OriginIp);
    for (size_t i = 0; i < DtNmosSdp_FlowCount(Sdp); i++)
    {
        const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, i);
        printf("flow %zu: %s %s:%u from %s, PT %u, ", i, DtNmosMedia_Name(Flow->Media),
               Flow->DestinationIp, (unsigned)Flow->DestinationPort,
               Flow->SourceIp[0] != '\0' ? Flow->SourceIp : "any source",
               (unsigned)Flow->PayloadType);
        PrintFormat(Flow);
        printf("%s\n", Flow->Leg == 1 ? " (leg 1)" : "");
    }
    const int Exit = Example_HasFlag(Argc, Argv, "--write") ? WriteBack(Sdp) : EXAMPLE_OK;
    DtNmosSdp_Free(Sdp);
    return Exit;
}
