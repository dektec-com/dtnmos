// #*#*#*#*#*#*#*#*#*#*#*#*#*# NmosDiscovery.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Registry lists and the registry search, as the rest of the library uses them
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

// Copies a list of registries, with its own copies of the strings. A null From gives an
// empty list.
DtNmosResult NmosRegistryList_Copy(const DtNmosRegistryList* From,
                                   DtNmosRegistryList** To);

// Makes a list of registries from Count base URLs, the most preferred first. This is the
// list a search holds when the program gives it the URLs itself. A URL that does not
// start with http or https fails with DTNMOS_E_INVALID_ARGUMENT.
DtNmosResult NmosRegistryList_FromUrls(DtNmosService Service, const char* const* Urls,
                                       size_t Count, DtNmosRegistryList** To);

// Asks the search to look again soon, because a node has no registry. While nodes keep
// asking, the search repeats after a wait that doubles from 1 to 8 seconds.
void NmosRegistrySearch_Hurry(DtNmosRegistrySearch* Search);
