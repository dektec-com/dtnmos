// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosFlow.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Copying a flow, and writing SDP into a buffer that grows
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "NmosInternal.h"
#include "dtnmos_sdp.h"

// Copies a flow into memory of its own, so that the copy does not point into the
// caller's memory. Its strings and its DidSdid array are copied into Store, which owns
// them. When memory runs out, Target is left unchanged.
DtNmosResult NmosFlow_Copy(DtNmosFlow* Target, NmosStore* Store,
                           const DtNmosFlow* Source);

// Writes the SDP text of a session and its Count flows. Does what DtNmosSdp_Write() does,
// but appends to a buffer that grows rather than to a buffer of fixed size.
DtNmosResult NmosSdp_Write(const DtNmosSession* Session, const DtNmosFlow* Flows,
                           size_t Count, NmosBuffer* Text);
