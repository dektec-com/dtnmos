// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestCommon.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the string type, name-based IDs and the names of results and media
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dtnmos_sdp.h"
#include "tests.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.- string_keeps_short_and_long_texts -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void string_keeps_short_and_long_texts(void)
{
    DtNmosString string = {0};
    CHECK_STR(DtNmosString_Get(&string), "");
    CHECK_EQ(DtNmosString_Length(&string), 0);
    CHECK_STR(DtNmosString_Get(NULL), "");

    // 23 bytes fit the struct, 24 need the heap.
    const char* fits = "0123456789abcdefghijklm";
    const char* heap = "0123456789abcdefghijklmn";
    REQUIRE(DtNmosString_SetText(&string, fits) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&string), fits);
    CHECK(string.Heap == NULL);
    REQUIRE(DtNmosString_SetText(&string, heap) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&string), heap);
    CHECK(string.Heap != NULL);
    CHECK_EQ(DtNmosString_Length(&string), 24);

    // A text of a given length need not end in a null character, and may hold one.
    REQUIRE(DtNmosString_Set(&string, "abcdef", 3) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&string), "abc");
    CHECK(string.Heap == NULL);
    DtNmosString_Clear(&string);
    CHECK_STR(DtNmosString_Get(&string), "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- string_sets_from_its_own_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void string_sets_from_its_own_text(void)
{
    DtNmosString string = {0};
    REQUIRE(DtNmosString_SetText(&string, "a text long enough for the heap") ==
            DTNMOS_OK);
    // From the heap it holds, into itself and into the struct.
    REQUIRE(DtNmosString_Set(&string, DtNmosString_Get(&string) + 2, 20) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&string), "text long enough for");
    REQUIRE(DtNmosString_Set(&string, DtNmosString_Get(&string), 4) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&string), "text");
    REQUIRE(DtNmosString_Copy(&string, &string) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&string), "text");
    DtNmosString_Clear(&string);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- string_copies_and_clears -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void string_copies_and_clears(void)
{
    DtNmosString source = {0};
    DtNmosString target = {0};
    REQUIRE(DtNmosString_SetText(&source, "a text long enough for the heap") ==
            DTNMOS_OK);
    REQUIRE(DtNmosString_Copy(&target, &source) == DTNMOS_OK);
    CHECK(target.Heap != source.Heap);
    DtNmosString_Clear(&source);
    CHECK_STR(DtNmosString_Get(&target), "a text long enough for the heap");
    REQUIRE(DtNmosString_SetText(&target, NULL) == DTNMOS_OK);
    CHECK_STR(DtNmosString_Get(&target), "");
    CHECK(DtNmosString_Set(NULL, "x", 1) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(DtNmosString_Set(&target, NULL, 1) == DTNMOS_E_INVALID_ARGUMENT);
    DtNmosString_Clear(&target);
    DtNmosString_Clear(NULL);
}

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
