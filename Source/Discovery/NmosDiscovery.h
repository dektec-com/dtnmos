// #*#*#*#*#*#*#*#*#*#*#*#*#*# NmosDiscovery.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The lists of registries, and the search of an application, within the library
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

// Sets *To to a copy of From, null being an empty list, with strings of its own.
DtNmosResult NmosRegistryList_Copy(const DtNmosRegistryList* From,
                                   DtNmosRegistryList** To);

// Sets *To to a list of the Count registries of Service at the base URLs Urls, the most
// preferred first, as a search that is fed holds them. Fails with
// DTNMOS_E_INVALID_ARGUMENT for a URL that is not one of http or https.
DtNmosResult NmosRegistryList_FromUrls(DtNmosService Service, const char* const* Urls,
                                       size_t Count, DtNmosRegistryList** To);

// Tells the search that a node of it has no registry, so that it searches soon, and
// again after a wait that doubles from a second to 8 for as long as nodes say so.
void NmosRegistrySearch_Hurry(DtNmosRegistrySearch* Search);
