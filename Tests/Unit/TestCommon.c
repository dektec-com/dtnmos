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
    const DtNmosId dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    DtNmosId id;
    REQUIRE(DtNmosId_FromName(&dns, "python.org", &id) == DTNMOS_OK);
    CHECK_STR(id.Text, "886313e1-3b8a-5372-9b90-0c9aee199e5d");
    // A name longer than a block of SHA-1 hashes over more than one block.
    REQUIRE(DtNmosId_FromName(
                &dns,
                "a name that is a good deal longer than one block of sixty-four "
                "bytes, which SHA-1 hashes at a time",
                &id) == DTNMOS_OK);
    CHECK_EQ(strlen(id.Text), 36);
    CHECK(id.Text[14] == '5');
    CHECK(strchr("89ab", id.Text[19]) != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- id_refuses_a_namespace_of_no_uuid -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void id_refuses_a_namespace_of_no_uuid(void)
{
    const DtNmosId wrong = {"6ba7b810-9dad-11d1-80b4-00c04fd430cX"};
    DtNmosId id;
    CHECK(DtNmosId_FromName(&wrong, "x", &id) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "no UUID") != NULL);
    CHECK(DtNmosId_FromName(NULL, "x", &id) == DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- names_results_and_media -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void names_results_and_media(void)
{
    CHECK_STR(DtNmosResult_Name(DTNMOS_E_AMBIGUOUS), "DTNMOS_E_AMBIGUOUS");
    CHECK_STR(DtNmosMedia_Name(DTNMOS_MEDIA_ANC), "ancillary data");
    int major = -1;
    int minor = -1;
    int patch = -1;
    DtNmos_Version(&major, &minor, &patch);
    CHECK_EQ(major, DTNMOS_VERSION_MAJOR);
    CHECK_EQ(minor, DTNMOS_VERSION_MINOR);
    CHECK_EQ(patch, DTNMOS_VERSION_PATCH);
    char text[32];
    snprintf(text, sizeof(text), "%d.%d.%d", major, minor, patch);
    CHECK_STR(text, DTNMOS_VERSION);
}

// .-.-.-.-.-.-.-.-.-.-.-.- keeps_the_last_error_of_the_thread -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtNmos_SetLastError() returns the result it is given and keeps the message; a call
// that fails replaces it, and one that succeeds leaves it.
void keeps_the_last_error_of_the_thread(void)
{
    CHECK(DtNmos_SetLastError(DTNMOS_E_HTTP, "the card refused it") == DTNMOS_E_HTTP);
    CHECK_STR(DtNmos_GetLastError(), "the card refused it");
    DtNmosId id;
    CHECK(DtNmosId_FromName(NULL, "x", &id) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "the card refused it") == NULL);
    const char* failure = DtNmos_GetLastError();
    char kept[512];
    snprintf(kept, sizeof(kept), "%s", failure);
    const DtNmosId dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    CHECK(DtNmosId_FromName(&dns, "python.org", &id) == DTNMOS_OK);
    CHECK_STR(DtNmos_GetLastError(), kept);
    CHECK(DtNmos_SetLastError(DTNMOS_OK, NULL) == DTNMOS_OK);
    CHECK_STR(DtNmos_GetLastError(), "");
}
