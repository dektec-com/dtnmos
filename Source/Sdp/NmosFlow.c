// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosFlow.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Copying a flow, with the strings and arrays it points to, into an owner
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "NmosFlow.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *Target to a copy of text owned by store, or to null for null; returns 0 when the
// memory ran out.
//
static int CopyText(const char** Target, NmosStore* Store, const char* Text)
{
    if (Text == NULL)
    {
        *Target = NULL;
        return 1;
    }
    *Target = NmosStore_Text(Store, Text, strlen(Text));
    return *Target != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosFlow_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosFlow_Copy(DtNmosFlow* Target, NmosStore* Store, const DtNmosFlow* Source)
{
    DtNmosFlow Copy = *Source;
    int Copied = CopyText(&Copy.RefClock.Text, Store, Source->RefClock.Text);
    switch (Source->Media)
    {
    case DTNMOS_MEDIA_AUDIO:
        Copied = Copied && CopyText(&Copy.Format.Audio.ChannelOrder, Store,
                                    Source->Format.Audio.ChannelOrder);
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
