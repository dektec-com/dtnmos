// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# main.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Runs the dtnmos tests
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <string.h>

#include "check.h"
#include "tests.h"

int CheckFailures = 0;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckReport -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void CheckReport(const char* File, int Line, const char* Expression)
{
    printf("  %s:%d: failed: %s\n", File, Line, Expression);
    ++CheckFailures;
}

typedef struct NmosTest
{
    const char* name;
    void (*Run)(void);
} NmosTest;

static const NmosTest Tests[] = {
#define DTNMOS_TEST(name) {#name, name},
    DTNMOS_TESTS
#undef DTNMOS_TEST
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    const char* Only = Argc > 1 ? Argv[1] : NULL;
    int Failed = 0;
    int Ran = 0;
    for (size_t i = 0; i < sizeof(Tests) / sizeof(Tests[0]); ++i)
    {
        if (Only != NULL && strcmp(Only, Tests[i].name) != 0)
        {
            continue;
        }
        CheckFailures = 0;
        Tests[i].Run();
        ++Ran;
        printf("%s %s\n", CheckFailures == 0 ? "[  OK  ]" : "[FAILED]", Tests[i].name);
        Failed += CheckFailures != 0;
    }
    if (Ran == 0)
    {
        printf("No test is named %s.\n", Only == NULL ? "" : Only);
        return 1;
    }
    printf("%d of %d tests passed\n", Ran - Failed, Ran);
    return Failed == 0 ? 0 : 1;
}
