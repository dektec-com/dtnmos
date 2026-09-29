// SPDX-License-Identifier: BSD-3-Clause
//
// Runs the dtnmos tests: all of them, or the one named on the command line, as ctest runs
// each of them.

#include <stdio.h>
#include <string.h>

#include "check.h"
#include "tests.h"

int check_failures = 0;

void check_report(const char* file, int line, const char* expression)
{
    printf("  %s:%d: failed: %s\n", file, line, expression);
    ++check_failures;
}

typedef struct test
{
    const char* name;
    void (*run)(void);
} test;

static const test tests[] = {
#define DTNMOS_TEST(name) {#name, name},
    DTNMOS_TESTS
#undef DTNMOS_TEST
};

int main(int argc, char** argv)
{
    const char* only = argc > 1 ? argv[1] : NULL;
    int failed = 0;
    int ran = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
    {
        if (only != NULL && strcmp(only, tests[i].name) != 0)
        {
            continue;
        }
        check_failures = 0;
        tests[i].run();
        ++ran;
        printf("%s %s\n", check_failures == 0 ? "[  OK  ]" : "[FAILED]", tests[i].name);
        failed += check_failures != 0;
    }
    if (ran == 0)
    {
        printf("No test is named %s.\n", only == NULL ? "" : only);
        return 1;
    }
    printf("%d of %d tests passed\n", ran - failed, ran);
    return failed == 0 ? 0 : 1;
}
