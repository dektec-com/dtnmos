// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestCommon.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of name-based IDs and of the names of results and media
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dtnmos_sdp.h"
#include "tests.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- id_is_the_uuid_of_version_5 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void id_is_the_uuid_of_version_5(void)
{
    // The example of the uuid module of Python: uuid5(NAMESPACE_DNS, "python.org").
    const DtNmosId Dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    DtNmosId Id;
    REQUIRE(DtNmosId_FromName(&Dns, "python.org", &Id) == DTNMOS_OK);
    CHECK_STR(Id.Text, "886313e1-3b8a-5372-9b90-0c9aee199e5d");
    // A name longer than a block of SHA-1 hashes over more than one block.
    REQUIRE(DtNmosId_FromName(
                &Dns,
                "a name that is a good deal longer than one block of sixty-four "
                "bytes, which SHA-1 hashes at a time",
                &Id) == DTNMOS_OK);
    CHECK_EQ(strlen(Id.Text), 36);
    CHECK(Id.Text[14] == '5');
    CHECK(strchr("89ab", Id.Text[19]) != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- id_refuses_a_namespace_of_no_uuid -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void id_refuses_a_namespace_of_no_uuid(void)
{
    const DtNmosId Wrong = {"6ba7b810-9dad-11d1-80b4-00c04fd430cX"};
    DtNmosId Id;
    CHECK(DtNmosId_FromName(&Wrong, "x", &Id) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "no UUID") != NULL);
    CHECK(DtNmosId_FromName(NULL, "x", &Id) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- names_results_and_media -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void names_results_and_media(void)
{
    CHECK_STR(DtNmosResult_Name(DTNMOS_E_AMBIGUOUS), "DTNMOS_E_AMBIGUOUS");
    CHECK_STR(DtNmosMedia_Name(DTNMOS_MEDIA_ANC), "ancillary data");
    int Major = -1;
    int Minor = -1;
    int Patch = -1;
    DtNmos_Version(&Major, &Minor, &Patch);
    CHECK_EQ(Major, DTNMOS_VERSION_MAJOR);
    CHECK_EQ(Minor, DTNMOS_VERSION_MINOR);
    CHECK_EQ(Patch, DTNMOS_VERSION_PATCH);
    char Text[32];
    snprintf(Text, sizeof(Text), "%d.%d.%d", Major, Minor, Patch);
    CHECK_STR(Text, DTNMOS_VERSION);
}

// .-.-.-.-.-.-.-.-.-.-.-.- keeps_the_last_error_of_the_thread -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtNmos_SetLastError() returns the result it is given and keeps the message; a call
// that fails replaces it, and one that succeeds leaves it.
void keeps_the_last_error_of_the_thread(void)
{
    CHECK(DtNmos_SetLastError(DTNMOS_E_HTTP, "the card refused it") == DTNMOS_E_HTTP);
    CHECK_STR(DtNmos_GetLastError(), "the card refused it");
    DtNmosId Id;
    CHECK(DtNmosId_FromName(NULL, "x", &Id) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "the card refused it") == NULL);
    const char* Failure = DtNmos_GetLastError();
    char Kept[512];
    snprintf(Kept, sizeof(Kept), "%s", Failure);
    const DtNmosId Dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    CHECK(DtNmosId_FromName(&Dns, "python.org", &Id) == DTNMOS_OK);
    CHECK_STR(DtNmos_GetLastError(), Kept);
    CHECK(DtNmos_SetLastError(DTNMOS_OK, NULL) == DTNMOS_OK);
    CHECK_STR(DtNmos_GetLastError(), "");
}
