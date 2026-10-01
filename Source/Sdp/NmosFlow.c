// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosFlow.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Copying a flow, with the strings and arrays it points to, into an owner
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "NmosFlow.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets *target to a copy of text owned by store, or to null for null; returns 0 when the
// memory ran out.
//
static int copy_text(const char** target, NmosStore* store, const char* text)
{
    if (text == NULL)
    {
        *target = NULL;
        return 1;
    }
    *target = NmosStore_Text(store, text, strlen(text));
    return *target != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosFlow_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosFlow_Copy(DtNmosFlow* target, NmosStore* store, const DtNmosFlow* source)
{
    DtNmosFlow copy = *source;
    int copied = copy_text(&copy.RefClock.Text, store, source->RefClock.Text);
    switch (source->Media)
    {
    case DTNMOS_MEDIA_AUDIO:
        copied = copied && copy_text(&copy.Format.Audio.ChannelOrder, store,
                                     source->Format.Audio.ChannelOrder);
        break;
    case DTNMOS_MEDIA_ANC:
        if (source->Format.Anc.DidSdidCount > 0)
        {
            copy.Format.Anc.DidSdid = NmosStore_Copy(
                store, source->Format.Anc.DidSdid,
                source->Format.Anc.DidSdidCount * sizeof(*source->Format.Anc.DidSdid));
            copied = copied && copy.Format.Anc.DidSdid != NULL;
        }
        else
        {
            copy.Format.Anc.DidSdid = NULL;
        }
        break;
    case DTNMOS_MEDIA_OTHER:
        copied = copied &&
                 copy_text(&copy.Format.Other.Fmtp, store, source->Format.Other.Fmtp);
        break;
    default:
        break;
    }
    if (!copied)
    {
        return NmosError_FailMemory();
    }
    *target = copy;
    return DTNMOS_OK;
}
