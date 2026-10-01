// #*#*#*#*#*#*#*#*#*#*#*#*#*#* controller.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - A controller that connects and disconnects receivers (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdint.h>

#include "dtnmos/dtnmos.h"
#include "dtnmos/query.h"

#ifdef __cplusplus
extern "C"
{
#endif

// What dtnmos_connect() connected: the receiver and the sender as the registry lists
// them, and the SDP of the sender that the receiver was given. Set to zero, it is empty;
// dtnmos_connection_clear() frees what it holds.
typedef struct dtnmos_connection
{
    dtnmos_receiver_info receiver;
    dtnmos_sender_info sender;
    dtnmos_string sdp;
} dtnmos_connection;

DTNMOS_API void dtnmos_connection_clear(dtnmos_connection* connection);

// Connects the receiver of the registry of query to a sender, each given by its ID or
// its label, as a controller of IS-05 does: it finds the Connection API of the receiver
// through the control urn:x-nmos:control:sr-ctrl/v1.1 of its device, and activates at
// once its staged parameters with the sender, master_enable true and the SDP of the
// sender as its transport file. The requests to the node go through the HTTP function
// of the query, with its timeout. connection, when not null, is cleared first and
// receives what was connected.
//
// Fails as dtnmos_query_find_receiver() and dtnmos_query_find_sender() do; with
// DTNMOS_E_INVALID_ARGUMENT when the flow of the sender is of another kind than the
// format of the receiver, video, audio or data; with DTNMOS_E_NOT_FOUND when the
// device has no such control or the sender no SDP; and with DTNMOS_E_HTTP when the node
// answers with another status than 200, the message holding the error it gave.
DTNMOS_API dtnmos_result dtnmos_connect(dtnmos_query* query, const char* receiver,
                                        const char* sender, dtnmos_connection* connection,
                                        dtnmos_error* error);

// Disconnects the receiver of the registry of query, given by its ID or label: activates
// at once its staged parameters with master_enable false and no sender. disconnected,
// when not null, is cleared first and receives the receiver as the registry lists it.
// Fails as dtnmos_connect() does.
DTNMOS_API dtnmos_result dtnmos_disconnect(dtnmos_query* query, const char* receiver,
                                           dtnmos_receiver_info* disconnected,
                                           dtnmos_error* error);

// Moves a sender of the registry of query, given by its ID or label, to destination_ip
// and destination_port, as a controller of IS-05 does: through the Connection API of the
// sender, found as dtnmos_connect() finds that of a receiver, it activates at once the
// staged parameters of its leg with the new destination. moved, when not null, is
// cleared first and receives the sender as the registry lists it. Fails as
// dtnmos_connect() does, and with DTNMOS_E_INVALID_ARGUMENT for a sender of another
// transport than RTP.
DTNMOS_API dtnmos_result dtnmos_move_sender(dtnmos_query* query, const char* sender,
                                            const char* destination_ip,
                                            uint16_t destination_port,
                                            dtnmos_sender_info* moved,
                                            dtnmos_error* error);

#ifdef __cplusplus
}
#endif
