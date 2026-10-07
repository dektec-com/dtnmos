// #*#*#*#*#*#*#*#*#*#*#*#*#* ValueOfError.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Reads the value of an Expected that holds an error, which ends the program
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The program behind the test dtnmos.CppValueOfError. Built without exceptions, value()
// of an Expected that holds an error calls std::terminate(); built with them, it throws,
// and the exception that nothing catches calls std::terminate() as well. The program's
// own terminate handler then ends it with 0, which the test expects. The program returns
// 1 when value() returned.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "dtnmos_expected.hpp"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Terminated -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Ends the program with 0 when std::terminate() is called, as the test expects. The
// default handler calls abort(), which CTest counts as a failure.
//
[[noreturn]] static void Terminated()
{
    std::puts("terminated, as expected");
    std::fflush(stdout);
    std::_Exit(0);
}

int main()
{
    std::set_terminate(Terminated);
    const DtNmos::Detail::Expected<int, int> Refused = DtNmos::Detail::Unexpected<int>(1);
    std::printf("reading the value of an Expected that holds an error\n");
    std::fflush(stdout);
    const int Value = Refused.value();
    std::printf("value() returned %d, and nothing stopped it\n", Value);
    return 1;
}
