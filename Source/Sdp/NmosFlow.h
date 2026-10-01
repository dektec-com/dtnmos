// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosFlow.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - What the node and the parser share of flows and their SDP
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "NmosInternal.h"
#include "dtnmos_sdp.h"

// Makes target a copy of source whose strings and arrays without a fixed size,
// ChannelOrder, Fmtp, DidSdid and RefClock.Text, are copies owned by store. Leaves target
// as it was when the memory runs out.
DtNmosResult dtnmos_flow_copy(DtNmosFlow* target, dtnmos_store* store,
                              const DtNmosFlow* source);

// Writes the SDP of session with the count flows of flows into text, as
// DtNmosSdp_Write() does into the caller's buffer.
DtNmosResult dtnmos_sdp_write(const DtNmosSession* session, const DtNmosFlow* flows,
                              size_t count, dtnmos_buffer* text);
