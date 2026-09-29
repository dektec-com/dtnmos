// SPDX-License-Identifier: BSD-3-Clause
//
// Tests of the string type, name-based IDs and the names of results and media.

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dtnmos/sdp.h"
#include "tests.h"

void string_keeps_short_and_long_texts(void)
{
    dtnmos_string string = {0};
    CHECK_STR(dtnmos_string_get(&string), "");
    CHECK_EQ(dtnmos_string_length(&string), 0);
    CHECK_STR(dtnmos_string_get(NULL), "");

    // 23 bytes fit the struct, 24 need the heap.
    const char* fits = "0123456789abcdefghijklm";
    const char* heap = "0123456789abcdefghijklmn";
    REQUIRE(dtnmos_string_set_text(&string, fits) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&string), fits);
    CHECK(string.heap == NULL);
    REQUIRE(dtnmos_string_set_text(&string, heap) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&string), heap);
    CHECK(string.heap != NULL);
    CHECK_EQ(dtnmos_string_length(&string), 24);

    // A text of a given length need not end in a null character, and may hold one.
    REQUIRE(dtnmos_string_set(&string, "abcdef", 3) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&string), "abc");
    CHECK(string.heap == NULL);
    dtnmos_string_clear(&string);
    CHECK_STR(dtnmos_string_get(&string), "");
}

void string_sets_from_its_own_text(void)
{
    dtnmos_string string = {0};
    REQUIRE(dtnmos_string_set_text(&string, "a text long enough for the heap") ==
            DTNMOS_OK);
    // From the heap it holds, into itself and into the struct.
    REQUIRE(dtnmos_string_set(&string, dtnmos_string_get(&string) + 2, 20) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&string), "text long enough for");
    REQUIRE(dtnmos_string_set(&string, dtnmos_string_get(&string), 4) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&string), "text");
    REQUIRE(dtnmos_string_copy(&string, &string) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&string), "text");
    dtnmos_string_clear(&string);
}

void string_copies_and_clears(void)
{
    dtnmos_string source = {0};
    dtnmos_string target = {0};
    REQUIRE(dtnmos_string_set_text(&source, "a text long enough for the heap") ==
            DTNMOS_OK);
    REQUIRE(dtnmos_string_copy(&target, &source) == DTNMOS_OK);
    CHECK(target.heap != source.heap);
    dtnmos_string_clear(&source);
    CHECK_STR(dtnmos_string_get(&target), "a text long enough for the heap");
    REQUIRE(dtnmos_string_set_text(&target, NULL) == DTNMOS_OK);
    CHECK_STR(dtnmos_string_get(&target), "");
    CHECK(dtnmos_string_set(NULL, "x", 1) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(dtnmos_string_set(&target, NULL, 1) == DTNMOS_E_INVALID_ARGUMENT);
    dtnmos_string_clear(&target);
    dtnmos_string_clear(NULL);
}

void id_is_the_uuid_of_version_5(void)
{
    // The example of the uuid module of Python: uuid5(NAMESPACE_DNS, "python.org").
    const dtnmos_id dns = {"6ba7b810-9dad-11d1-80b4-00c04fd430c8"};
    dtnmos_id id;
    dtnmos_error error = {DTNMOS_OK, ""};
    REQUIRE(dtnmos_id_from_name(&dns, "python.org", &id, &error) == DTNMOS_OK);
    CHECK_STR(id.text, "886313e1-3b8a-5372-9b90-0c9aee199e5d");
    // A name longer than a block of SHA-1 hashes over more than one block.
    REQUIRE(dtnmos_id_from_name(
                &dns,
                "a name that is a good deal longer than one block of sixty-four "
                "bytes, which SHA-1 hashes at a time",
                &id, &error) == DTNMOS_OK);
    CHECK_EQ(strlen(id.text), 36);
    CHECK(id.text[14] == '5');
    CHECK(strchr("89ab", id.text[19]) != NULL);
}

void id_refuses_a_namespace_of_no_uuid(void)
{
    const dtnmos_id wrong = {"6ba7b810-9dad-11d1-80b4-00c04fd430cX"};
    dtnmos_id id;
    dtnmos_error error = {DTNMOS_OK, ""};
    CHECK(dtnmos_id_from_name(&wrong, "x", &id, &error) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(error.code == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(error.message, "no UUID") != NULL);
    CHECK(dtnmos_id_from_name(NULL, "x", &id, NULL) == DTNMOS_E_INVALID_ARGUMENT);
}

void names_results_and_media(void)
{
    CHECK_STR(dtnmos_result_name(DTNMOS_E_AMBIGUOUS), "DTNMOS_E_AMBIGUOUS");
    CHECK_STR(dtnmos_media_name(DTNMOS_MEDIA_ANC), "ancillary data");
    int major = -1;
    dtnmos_version(&major, NULL, NULL);
    CHECK_EQ(major, DTNMOS_VERSION_MAJOR);
}
