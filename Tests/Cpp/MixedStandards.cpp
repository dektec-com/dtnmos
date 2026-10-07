// #*#*#*#*#*#*#*#*#*#*#*#*# MixedStandards.cpp *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Includes dtnmos.hpp under C++23, beside a source that includes it under C++20
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The program behind the test dtnmos.CppMixedStandards. This source is compiled as
// C++23, with std::expected, and MixedStandards20.cpp as C++20, with Detail::OwnExpected.
// Detail::Check() returns a Status, which is a different type in each, so each must
// have its own copy of the function, which the tag of dtnmos_expected.hpp gives it. The
// program returns 0 when the two copies have different addresses, and 1 when the linker
// kept one for both. Where this compiler has no std::expected under C++23 either, both
// sources have the same Expected, and there is nothing to check.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdint>
#include <cstdio>

#include "dtnmos.hpp"

// Returns the address of Detail::Check() under C++20, and whether that source has
// std::expected. Both are in MixedStandards20.cpp.
std::uintptr_t CheckUnderCpp20();
bool StdExpectedUnderCpp20();

int main()
{
#if defined(DTNMOS_DETAIL_STD_EXPECTED)
    if (StdExpectedUnderCpp20())
    {
        std::puts("std::expected under C++20 as well: nothing to check");
        return 0;
    }
    const auto Here = reinterpret_cast<std::uintptr_t>(&DtNmos::Detail::Check);
    if (Here == CheckUnderCpp20())
    {
        std::puts(
            "C++20 and C++23 share one Detail::Check(), as if Status were one type");
        return 1;
    }
    std::puts("C++20 and C++23 each have their own Detail::Check()");
    return 0;
#else
    std::puts("no std::expected under C++23: nothing to check");
    return 0;
#endif
}
