// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosFlow.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Copying a flow, with the strings and arrays it points to, into an owner
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdlib.h>
#include <string.h>

#include "NmosFlow.h"

// A flow DtNmosFlow_Copy() made, with the store of what it points to. The flow comes
// first, so that the copy's address is the flow's, which DtNmosFlow_Free() is given.
typedef struct NmosOwnedFlow
{
    DtNmosFlow Flow;
    NmosStore Store;
} NmosOwnedFlow;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *Target to a copy of text owned by store, or to null for null; returns false when
// the memory ran out.
//
static bool CopyText(const char** Target, NmosStore* Store, const char* Text)
{
    if (Text == NULL)
    {
        *Target = NULL;
        return true;
    }
    *Target = NmosStore_Text(Store, Text, strlen(Text));
    return *Target != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosFlow_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosFlow_Copy(DtNmosFlow* Target, NmosStore* Store, const DtNmosFlow* Source)
{
    DtNmosFlow Copy = *Source;
    bool Copied = CopyText(&Copy.RefClock.Text, Store, Source->RefClock.Text);
    switch (Source->Media)
    {
    case DTNMOS_MEDIA_AUDIO:
        Copied = Copied &&
                 CopyText(&Copy.Format.Audio.ChannelOrder, Store,
                          Source->Format.Audio.ChannelOrder) &&
                 CopyText(&Copy.Format.Audio.OtherParameters, Store,
                          Source->Format.Audio.OtherParameters);
        break;
    case DTNMOS_MEDIA_VIDEO:
        Copied = Copied && CopyText(&Copy.Format.Video.OtherParameters, Store,
                                    Source->Format.Video.OtherParameters);
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        Copied = Copied && CopyText(&Copy.Format.CompressedVideo.OtherParameters, Store,
                                    Source->Format.CompressedVideo.OtherParameters);
        break;
    case DTNMOS_MEDIA_ANC:
        if (Source->Format.Anc.DidSdidCount > 0)
        {
            Copy.Format.Anc.DidSdid = NmosStore_Copy(
                Store, Source->Format.Anc.DidSdid,
                Source->Format.Anc.DidSdidCount * sizeof(*Source->Format.Anc.DidSdid));
            Copied = Copied && Copy.Format.Anc.DidSdid != NULL;
        }
        else
        {
            Copy.Format.Anc.DidSdid = NULL;
        }
        break;
    case DTNMOS_MEDIA_OTHER:
        Copied =
            Copied && CopyText(&Copy.Format.Other.Fmtp, Store, Source->Format.Other.Fmtp);
        break;
    default:
        break;
    }
    if (!Copied)
    {
        return NmosError_FailMemory();
    }
    *Target = Copy;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosFlow_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosFlow_Copy(const DtNmosFlow* Flow, DtNmosFlow** Copy)
{
    if (Copy != NULL)
    {
        *Copy = NULL;
    }
    if (Copy == NULL || Flow == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosFlow_Copy() needs a flow and a place for its copy.");
    }
    const DtNmosResult Sized = DTNMOS_CHECK_SIZE(Flow, DtNmosFlow, sizeof(DtNmosFlow));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    NmosOwnedFlow* Owned = (NmosOwnedFlow*)calloc(1, sizeof(*Owned));
    if (Owned == NULL)
    {
        return NmosError_FailMemory();
    }
    const DtNmosResult Result = NmosFlow_Copy(&Owned->Flow, &Owned->Store, Flow);
    if (Result != DTNMOS_OK)
    {
        NmosStore_Free(&Owned->Store);
        free(Owned);
        return Result;
    }
    Owned->Flow.Size = sizeof(Owned->Flow);
    *Copy = &Owned->Flow;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosFlow_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosFlow_Free(DtNmosFlow* Flow)
{
    if (Flow == NULL)
    {
        return;
    }
    NmosOwnedFlow* Owned = (NmosOwnedFlow*)(void*)Flow;
    NmosStore_Free(&Owned->Store);
    free(Owned);
}
