// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosTest.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The assertions and the runner of every test suite
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
    #include <crtdbg.h>
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test framework +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// This is CDTAPI's DtTest.h under the names of dtnmos. dtnmos has no dependencies, and a
// unit-test framework is not worth making an exception for. A suite is one source file,
// built into a program of its own: it declares its cases with NMOS_TEST and lists them
// in NMOS_TEST_MAIN, one line each, as no constructor attribute works on every compiler.
//
// A failing assertion reports where it is, counts the failure and returns from the
// function it is in, so that one broken case does not hide the ones after it. The
// program exits with zero only when every case passed, which is what CTest reads.
//
// The failures are counted for the whole file rather than passed to each case, as
// DtTest.h passes them, because the tests of dtnmos assert in their helpers and in the
// callbacks the library calls as well. A callback that must return a value checks with
// NMOS_EXPECT, which counts a failure without returning.
//
// Usage:
//
//     NMOS_TEST(VersionIsKnown)
//     {
//         int Major = -1;
//         DtNmos_Version(&Major, NULL, NULL);
//         NMOS_ASSERT(Major >= 0);
//     }
//
//     NMOS_TEST_MAIN("Common", NMOS_RUN(VersionIsKnown))
//
// A case that opens something registers, with NmosTest_SetCleanup, a function that frees
// it, which NMOS_FAIL calls before it returns, so that what the case opened is not left
// open by a failure. The case's own end unregisters the function first, and frees as it
// always does.
//

// Declares one test case. The body follows this macro.
#define NMOS_TEST(Name) static void Name(void)

// The failures of the running case.
static int NmosTest_Failures;

// Frees what the running case opened. The function runs at most once: NMOS_FAIL
// unregisters it before calling it, so a failure while it runs does not run it again.
typedef void (*NmosTestCleanupFunc)(void* Context);

static NmosTestCleanupFunc NmosTest_CleanupFunc;
static void* NmosTest_CleanupContext;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosTest_Cleanup -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static inline void NmosTest_Cleanup(void)
{
    NmosTestCleanupFunc Func = NmosTest_CleanupFunc;
    NmosTest_CleanupFunc = NULL;
    if (Func != NULL)
    {
        Func(NmosTest_CleanupContext);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosTest_SetCleanup -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static inline void NmosTest_SetCleanup(NmosTestCleanupFunc Func, void* Context)
{
    NmosTest_CleanupFunc = Func;
    NmosTest_CleanupContext = Context;
}

// Reports a failure where it is and counts it, without returning.
#define NMOS_REPORT(...)                                                                 \
    do                                                                                   \
    {                                                                                    \
        printf("    FAIL %s:%d: ", __FILE__, __LINE__);                                  \
        printf(__VA_ARGS__);                                                             \
        printf("\n");                                                                    \
        NmosTest_Failures++;                                                             \
    } while (0)

#define NMOS_FAIL(...)                                                                   \
    do                                                                                   \
    {                                                                                    \
        NMOS_REPORT(__VA_ARGS__);                                                        \
        NmosTest_Cleanup();                                                              \
        return;                                                                          \
    } while (0)

#define NMOS_ASSERT(Cond)                                                                \
    do                                                                                   \
    {                                                                                    \
        if (!(Cond))                                                                     \
            NMOS_FAIL("expected %s", #Cond);                                             \
    } while (0)

#define NMOS_ASSERT_EQ(Actual, Expected)                                                 \
    do                                                                                   \
    {                                                                                    \
        int64_t NmosA = (int64_t)(Actual);                                               \
        int64_t NmosE = (int64_t)(Expected);                                             \
        if (NmosA != NmosE)                                                              \
            NMOS_FAIL("%s: expected %" PRId64 ", got %" PRId64, #Actual, NmosE, NmosA);  \
    } while (0)

#define NMOS_ASSERT_STR(Actual, Expected)                                                \
    do                                                                                   \
    {                                                                                    \
        const char* NmosA = (Actual);                                                    \
        const char* NmosE = (Expected);                                                  \
        if (NmosA == NULL || strcmp(NmosA, NmosE) != 0)                                  \
            NMOS_FAIL("%s: expected \"%s\", got \"%s\"", #Actual, NmosE,                 \
                      NmosA == NULL ? "(null)" : NmosA);                                 \
    } while (0)

// Checks Cond in a function that cannot return at once, such as a callback that returns
// a value: a failure is counted, and the function goes on.
#define NMOS_EXPECT(Cond)                                                                \
    do                                                                                   \
    {                                                                                    \
        if (!(Cond))                                                                     \
            NMOS_REPORT("expected %s", #Cond);                                           \
    } while (0)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Test runner -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct NmosTestCase
{
    const char* Name;
    void (*Func)(void);
} NmosTestCase;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosTest_SilenceDialogs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends failed CRT assertions and abort() to stderr instead of to a message box: a run
// that CTest or the CI starts has nobody to click OK, and would wait until it timed out.
//
static void NmosTest_SilenceDialogs(void)
{
#if defined(_MSC_VER)
    // _CRT_WARN, _CRT_ERROR and _CRT_ASSERT.
    for (int Report = 0; Report < 3; Report++)
    {
        _CrtSetReportMode(Report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(Report, _CRTDBG_FILE_STDERR);
    }
    // Still report the fault to an attached debugger, but show no dialog.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

#define NMOS_RUN(Name) {#Name, Name}

#define NMOS_TEST_MAIN(SuiteName, ...)                                                   \
    int main(void)                                                                       \
    {                                                                                    \
        static const NmosTestCase Cases[] = {__VA_ARGS__};                               \
        const int NumCases = (int)(sizeof(Cases) / sizeof(Cases[0]));                    \
        int TotalFailures = 0;                                                           \
        NmosTest_SilenceDialogs();                                                       \
        printf("== %s: %d case(s)\n", SuiteName, NumCases);                              \
        for (int i = 0; i < NumCases; i++)                                               \
        {                                                                                \
            NmosTest_Failures = 0;                                                       \
            Cases[i].Func();                                                             \
            printf("  %-4s %s\n", NmosTest_Failures == 0 ? "ok" : "FAIL",                \
                   Cases[i].Name);                                                       \
            TotalFailures += NmosTest_Failures;                                          \
        }                                                                                \
        printf("== %s: %s\n", SuiteName, TotalFailures == 0 ? "PASSED" : "FAILED");      \
        return TotalFailures == 0 ? 0 : 1;                                               \
    }
