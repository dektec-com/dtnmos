// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* main.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Makes an ID with the C++ API of an installed dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <dtnmos.hpp>

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main()
{
    const auto Namespace = DtNmos::Id::FromText("bbbbbbbb-0000-4000-8000-000000000001");
    const auto Made = Namespace ? DtNmos::Id::FromName(*Namespace, "device") : Namespace;
    return Made && !Made->IsEmpty() ? 0 : 1;
}
