// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestCommon.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of name-based IDs and of the names of results and media
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosTest.h"
#include "dtnmos_sdp.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IdIsTheUuidOfVersion5 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(IdIsTheUuidOfVersion5)
{
    // The example of the uuid module of Python: uuid5(NAMESPACE_DNS, "python.org").
    const DtNmosId Dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    DtNmosId Id;
    NMOS_ASSERT(DtNmosId_FromName(&Dns, "python.org", &Id) == DTNMOS_OK);
    NMOS_ASSERT_STR(Id.Text, "886313e1-3b8a-5372-9b90-0c9aee199e5d");
    // A name longer than a block of SHA-1 hashes over more than one block.
    NMOS_ASSERT(DtNmosId_FromName(
                    &Dns,
                    "a name that is a good deal longer than one block of sixty-four "
                    "bytes, which SHA-1 hashes at a time",
                    &Id) == DTNMOS_OK);
    NMOS_ASSERT_EQ(strlen(Id.Text), 36);
    NMOS_ASSERT(Id.Text[14] == '5');
    NMOS_ASSERT(strchr("89ab", Id.Text[19]) != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- IdRefusesANamespaceOfNoUuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(IdRefusesANamespaceOfNoUuid)
{
    const DtNmosId Wrong = {"6ba7b810-9dad-11d1-80b4-00c04fd430cX"};
    DtNmosId Id;
    NMOS_ASSERT(DtNmosId_FromName(&Wrong, "x", &Id) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "no UUID") != NULL);
    NMOS_ASSERT(DtNmosId_FromName(NULL, "x", &Id) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NamesResultsAndMedia -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(NamesResultsAndMedia)
{
    NMOS_ASSERT_STR(DtNmosResult_Name(DTNMOS_E_AMBIGUOUS), "DTNMOS_E_AMBIGUOUS");
    NMOS_ASSERT_STR(DtNmosMedia_Name(DTNMOS_MEDIA_ANC), "ancillary data");
    int Major = -1;
    int Minor = -1;
    int Patch = -1;
    DtNmos_Version(&Major, &Minor, &Patch);
    NMOS_ASSERT_EQ(Major, DTNMOS_VERSION_MAJOR);
    NMOS_ASSERT_EQ(Minor, DTNMOS_VERSION_MINOR);
    NMOS_ASSERT_EQ(Patch, DTNMOS_VERSION_PATCH);
    char Text[32];
    snprintf(Text, sizeof(Text), "%d.%d.%d", Major, Minor, Patch);
    NMOS_ASSERT_STR(Text, DTNMOS_VERSION);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- KeepsTheLastErrorOfTheThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtNmos_SetLastError() returns the result it is given and keeps the message; a call
// that fails replaces it, and one that succeeds leaves it.
NMOS_TEST(KeepsTheLastErrorOfTheThread)
{
    NMOS_ASSERT(DtNmos_SetLastError(DTNMOS_E_HTTP, "the card refused it") ==
                DTNMOS_E_HTTP);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), "the card refused it");
    DtNmosId Id;
    NMOS_ASSERT(DtNmosId_FromName(NULL, "x", &Id) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "the card refused it") == NULL);
    const char* Failure = DtNmos_GetLastError();
    char Kept[512];
    snprintf(Kept, sizeof(Kept), "%s", Failure);
    const DtNmosId Dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    NMOS_ASSERT(DtNmosId_FromName(&Dns, "python.org", &Id) == DTNMOS_OK);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), Kept);
    NMOS_ASSERT(DtNmos_SetLastError(DTNMOS_OK, NULL) == DTNMOS_OK);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), "");
}

NMOS_TEST_MAIN("Common", NMOS_RUN(IdIsTheUuidOfVersion5),
               NMOS_RUN(IdRefusesANamespaceOfNoUuid), NMOS_RUN(NamesResultsAndMedia),
               NMOS_RUN(KeepsTheLastErrorOfTheThread))
