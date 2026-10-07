// #*#*#*#*#*#*#*#*#*#*#*#* MixedStandards20.cpp *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Includes dtnmos.hpp under C++20, for the test dtnmos.CppMixedStandards
//
// SPDX-License-Identifier: BSD-3-Clause
//
// MixedStandards.cpp says what the test checks.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdint>

#include "dtnmos.hpp"

// Returns the address of Detail::Check() under this standard.
std::uintptr_t CheckUnderCpp20()
{
    return reinterpret_cast<std::uintptr_t>(&DtNmos::Detail::Check);
}

// Returns true when Expected is std::expected under this standard as well.
bool StdExpectedUnderCpp20()
{
#if defined(DTNMOS_DETAIL_STD_EXPECTED)
    return true;
#else
    return false;
#endif
}
