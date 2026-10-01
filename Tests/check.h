// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# check.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The checks of the dtnmos tests
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <string.h>

// The number of checks that failed in the running test.
extern int CheckFailures;

void CheckReport(const char* File, int Line, const char* Expression);

#define CHECK(condition)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if (!(condition))                                                                \
        {                                                                                \
            CheckReport(__FILE__, __LINE__, #condition);                                 \
        }                                                                                \
    } while (0)

// Checks condition and ends the test when it fails, for a check that later ones build on.
#define REQUIRE(condition)                                                               \
    do                                                                                   \
    {                                                                                    \
        if (!(condition))                                                                \
        {                                                                                \
            CheckReport(__FILE__, __LINE__, #condition);                                 \
            return;                                                                      \
        }                                                                                \
    } while (0)

#define CHECK_EQ(actual, expected)                                                       \
    do                                                                                   \
    {                                                                                    \
        const unsigned long long check_actual = (unsigned long long)(actual);            \
        const unsigned long long check_expected = (unsigned long long)(expected);        \
        if (check_actual != check_expected)                                              \
        {                                                                                \
            printf("  %s is %llu, expected %llu\n", #actual, check_actual,               \
                   check_expected);                                                      \
            CheckReport(__FILE__, __LINE__, #actual " == " #expected);                   \
        }                                                                                \
    } while (0)

#define CHECK_STR(actual, expected)                                                      \
    do                                                                                   \
    {                                                                                    \
        const char* check_actual = (actual);                                             \
        const char* check_expected = (expected);                                         \
        if (strcmp(check_actual, check_expected) != 0)                                   \
        {                                                                                \
            printf("  %s is \"%s\", expected \"%s\"\n", #actual, check_actual,           \
                   check_expected);                                                      \
            CheckReport(__FILE__, __LINE__, #actual " == " #expected);                   \
        }                                                                                \
    } while (0)
