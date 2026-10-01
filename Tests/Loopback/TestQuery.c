// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestQuery.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of JSON, the HTTP response, and the query of a registry
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "check.h"
#include "tests.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- json_reads_values_and_escapes -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void json_reads_values_and_escapes(void)
{
    const char* text =
        "{\"id\": \"abc\", \"n\": -12.5e1, \"list\": [true, false, null, [], {}], "
        "\"text\": \"a\\\"b\\\\c\\/d\\n\\u00e9\\ud83d\\ude00\"}";
    dtnmos_json* json = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    REQUIRE(dtnmos_json_parse(text, strlen(text), &json, &error) == DTNMOS_OK);
    CHECK_EQ(json->type, DTNMOS_JSON_OBJECT);
    CHECK_STR(dtnmos_json_member_text(json, "id"), "abc");
    const dtnmos_json* n = dtnmos_json_member(json, "n");
    REQUIRE(n != NULL);
    CHECK(n->type == DTNMOS_JSON_NUMBER && n->number == -125.0);
    const dtnmos_json* list = dtnmos_json_member(json, "list");
    REQUIRE(list != NULL && list->type == DTNMOS_JSON_ARRAY);
    CHECK_EQ(list->count, 5);
    CHECK_EQ(list->items[0].type, DTNMOS_JSON_TRUE);
    CHECK_EQ(list->items[2].type, DTNMOS_JSON_NULL);
    CHECK_EQ(list->items[4].type, DTNMOS_JSON_OBJECT);
    // Escapes, a character of two bytes in UTF-8, and one of four from a surrogate pair.
    CHECK_STR(dtnmos_json_member_text(json, "text"),
              "a\"b\\c/d\n\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(dtnmos_json_member(json, "missing") == NULL);
    CHECK(dtnmos_json_member_text(json, "n") == NULL);
    dtnmos_json_free(json);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- json_refuses_what_is_malformed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void json_refuses_what_is_malformed(void)
{
    const char* const texts[] = {"",    "{",     "[1,]",    "{\"a\" 1}",   "\"open",
                                 "tru", "[1] 2", "\"\\x\"", "\"\\ud83d\"", "{1: 2}"};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i)
    {
        dtnmos_json* json = (dtnmos_json*)&json;
        dtnmos_error error = {DTNMOS_OK, ""};
        const dtnmos_result result =
            dtnmos_json_parse(texts[i], strlen(texts[i]), &json, &error);
        if (result != DTNMOS_E_PARSE)
        {
            printf("  '%s' parsed\n", texts[i]);
        }
        CHECK(result == DTNMOS_E_PARSE);
        CHECK(json == NULL);
        CHECK(strstr(error.message, "JSON at offset") != NULL);
    }
    // Nesting deeper than the limit fails rather than overflowing the stack.
    char deep[200];
    memset(deep, '[', sizeof(deep) - 1);
    deep[sizeof(deep) - 1] = '\0';
    dtnmos_json* json = NULL;
    CHECK(dtnmos_json_parse(deep, strlen(deep), &json, NULL) == DTNMOS_E_PARSE);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- json_writes_escaped_strings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void json_writes_escaped_strings(void)
{
    dtnmos_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    dtnmos_json_write_string(&buffer, "a\"b\\c\n\x01");
    REQUIRE(!buffer.failed);
    CHECK_STR(buffer.data, "\"a\\\"b\\\\c\\n\\u0001\"");
    dtnmos_buffer_free(&buffer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- http_response_owns_what_it_holds -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void http_response_owns_what_it_holds(void)
{
    dtnmos_http_response* response = dtnmos_http_response_create();
    REQUIRE(response != NULL);
    CHECK_STR(dtnmos_http_response_body(response, NULL), "");
    dtnmos_http_response_set_status(response, 200);
    char body[] = "{\"a\": 1}";
    REQUIRE(dtnmos_http_response_set_body(response, "application/json", body,
                                          strlen(body)) == DTNMOS_OK);
    body[0] = 'X';
    REQUIRE(dtnmos_http_response_append_body(response, " ", 1) == DTNMOS_OK);
    size_t length = 0;
    CHECK_STR(dtnmos_http_response_body(response, &length), "{\"a\": 1} ");
    CHECK_EQ(length, 9);
    REQUIRE(dtnmos_http_response_add_header(response, "Link", "<x>; rel=\"next\"") ==
            DTNMOS_OK);
    CHECK_STR(dtnmos_http_response_find_header(response, "link"), "<x>; rel=\"next\"");
    CHECK(dtnmos_http_response_find_header(response, "Location") == NULL);
    CHECK_EQ(dtnmos_http_response_header_count(response), 1);
    CHECK_STR(dtnmos_http_response_header(response, 0).name, "Link");
    CHECK_STR(dtnmos_http_response_content_type(response), "application/json");
    CHECK_EQ(dtnmos_http_response_status(response), 200);
    dtnmos_http_response_free(response);
}

// A registry that the test answers for: each route an URL and what it gives.
typedef struct route
{
    const char* url;
    int status;
    const char* body;
    const char* link; // the Link header, or null
} route;

typedef struct fake_registry
{
    const route* routes;
    size_t count;
    int requests;
    int unreachable; // every request fails as if the registry did not answer
} fake_registry;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_http -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static dtnmos_result fake_http(void* user, const dtnmos_http_request* request,
                               dtnmos_http_response* response, dtnmos_error* error)
{
    fake_registry* registry = user;
    ++registry->requests;
    if (registry->unreachable)
    {
        snprintf(error->message, sizeof(error->message), "connection refused");
        error->code = DTNMOS_E_HTTP;
        return DTNMOS_E_HTTP;
    }
    CHECK_STR(request->method, "GET");
    CHECK_EQ(request->timeout_ms, 2000);
    for (size_t i = 0; i < registry->count; ++i)
    {
        if (strcmp(registry->routes[i].url, request->url) == 0)
        {
            dtnmos_http_response_set_status(response, registry->routes[i].status);
            dtnmos_http_response_set_body(response, "application/json",
                                          registry->routes[i].body,
                                          strlen(registry->routes[i].body));
            if (registry->routes[i].link != NULL)
            {
                dtnmos_http_response_add_header(response, "Link",
                                                registry->routes[i].link);
            }
            return DTNMOS_OK;
        }
    }
    printf("  no route for %s\n", request->url);
    dtnmos_http_response_set_status(response, 404);
    return DTNMOS_OK;
}

#define VIDEO_ID "11111111-1111-4111-8111-111111111111"
#define AUDIO_ID "22222222-2222-4222-8222-222222222222"
#define TWIN_ID "33333333-3333-4333-8333-333333333333"
#define VIDEO_FLOW "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
#define AUDIO_FLOW "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
#define BASE "http://registry.test/x-nmos/query/v1.3/"

static const char* const video_sender =
    "{\"id\": \"" VIDEO_ID "\", \"label\": \"camera 1\", \"description\": \"studio\", "
    "\"flow_id\": \"" VIDEO_FLOW "\", \"device_id\": \"" TWIN_ID "\", "
    "\"transport\": \"urn:x-nmos:transport:rtp.mcast\", "
    "\"manifest_href\": \"http://camera.test/video.sdp\"}";
static const char* const audio_sender =
    "{\"id\": \"" AUDIO_ID "\", \"label\": \"mic\", \"flow_id\": \"" AUDIO_FLOW "\", "
    "\"manifest_href\": null}";
static const char* const twin_sender = "{\"id\": \"" TWIN_ID "\", \"label\": \"mic\"}";

static const route routes[] = {
    {BASE "senders?paging.limit=100", 200, "[]", NULL},
    {BASE "flows?paging.limit=100", 200,
     "[{\"id\": \"" VIDEO_FLOW "\", \"format\": \"urn:x-nmos:format:video\", "
     "\"media_type\": \"video/raw\"}, {\"id\": \"" AUDIO_FLOW "\", "
     "\"format\": \"urn:x-nmos:format:audio\", \"media_type\": \"audio/L24\"}]",
     NULL},
    {BASE "flows/" VIDEO_FLOW, 200,
     "{\"id\": \"" VIDEO_FLOW "\", \"format\": \"urn:x-nmos:format:video\", "
     "\"media_type\": \"video/raw\"}",
     NULL},
    {BASE "senders/" VIDEO_ID, 200, NULL, NULL},
    {BASE "senders?label=camera%201&paging.limit=100", 200, NULL, NULL},
    {BASE "senders?label=mic&paging.limit=100", 200, NULL, NULL},
    {"http://camera.test/video.sdp", 200,
     "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=camera 1\r\nt=0 0\r\n"
     "m=video 5004 RTP/AVP 96\r\nc=IN IP4 239.0.0.1/64\r\na=rtpmap:96 raw/90000\r\n",
     NULL},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- registry_routes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Builds the routes of a registry with two pages of senders, the second reached through
// the Link header of the first, as the paging of IS-04 does.
//
static void registry_routes(route* table, size_t* count, char* page_one,
                            size_t page_one_size, char* single, size_t single_size,
                            char* by_label, size_t by_label_size, char* twins,
                            size_t twins_size)
{
    memcpy(table, routes, sizeof(routes));
    *count = sizeof(routes) / sizeof(routes[0]);
    snprintf(page_one, page_one_size, "[%s, %s]", video_sender, audio_sender);
    snprintf(single, single_size, "%s", video_sender);
    snprintf(by_label, by_label_size, "[%s]", video_sender);
    snprintf(twins, twins_size, "[%s, %s]", audio_sender, twin_sender);
    table[0].body = page_one;
    table[0].link = "<" BASE "senders?paging.limit=100&paging.until=2:0>; rel=\"next\", "
                    "<" BASE "senders?paging.limit=100>; rel=\"first\"";
    table[3].body = single;
    table[4].body = by_label;
    table[5].body = twins;
    // The second page, empty, ends the list.
    table[*count].url = BASE "senders?paging.limit=100&paging.until=2:0";
    table[*count].status = 200;
    table[*count].body = "[]";
    table[*count].link = NULL;
    ++*count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- make_query -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static dtnmos_query* make_query(fake_registry* registry)
{
    dtnmos_query_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.registry_url = "http://registry.test/";
    config.http = fake_http;
    config.http_user = registry;
    config.timeout_ms = 2000;
    dtnmos_query* query = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    if (dtnmos_query_create(&config, &query, &error) != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    return query;
}

// .-.-.-.-.-.-.-.-.-.-.-.- query_lists_the_senders_of_every_page -.-.-.-.-.-.-.-.-.-.-.-.
//
void query_lists_the_senders_of_every_page(void)
{
    route table[16];
    size_t count = 0;
    char page_one[1024];
    char single[512];
    char by_label[512];
    char twins[512];
    registry_routes(table, &count, page_one, sizeof(page_one), single, sizeof(single),
                    by_label, sizeof(by_label), twins, sizeof(twins));
    fake_registry registry = {table, count, 0, 0};
    dtnmos_query* query = make_query(&registry);
    REQUIRE(query != NULL);
    dtnmos_sender_list* list = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    const dtnmos_result result = dtnmos_query_senders(query, &list, &error);
    if (result != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    REQUIRE(result == DTNMOS_OK);
    REQUIRE(dtnmos_sender_list_count(list) == 2);
    const dtnmos_sender_info* video = dtnmos_sender_list_at(list, 0);
    CHECK_STR(video->id.text, VIDEO_ID);
    CHECK_STR(dtnmos_string_get(&video->label), "camera 1");
    CHECK_STR(dtnmos_string_get(&video->description), "studio");
    CHECK_EQ(video->media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(dtnmos_string_get(&video->manifest_href), "http://camera.test/video.sdp");
    CHECK_STR(video->device_id.text, TWIN_ID);
    const dtnmos_sender_info* audio = dtnmos_sender_list_at(list, 1);
    CHECK_EQ(audio->media, DTNMOS_MEDIA_AUDIO);
    CHECK_STR(dtnmos_string_get(&audio->manifest_href), "");
    CHECK(dtnmos_sender_list_at(list, 2) == NULL);
    // Two pages of senders and one of flows.
    CHECK_EQ(registry.requests, 3);
    dtnmos_sender_list_free(list);
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- query_finds_a_sender_and_its_sdp -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void query_finds_a_sender_and_its_sdp(void)
{
    route table[16];
    size_t count = 0;
    char page_one[1024];
    char single[512];
    char by_label[512];
    char twins[512];
    registry_routes(table, &count, page_one, sizeof(page_one), single, sizeof(single),
                    by_label, sizeof(by_label), twins, sizeof(twins));
    fake_registry registry = {table, count, 0, 0};
    dtnmos_query* query = make_query(&registry);
    REQUIRE(query != NULL);
    dtnmos_error error = {DTNMOS_OK, ""};
    const char* const keys[] = {VIDEO_ID, "camera 1"};
    for (size_t k = 0; k < 2; ++k)
    {
        dtnmos_sender_info sender = {0};
        const dtnmos_result result =
            dtnmos_query_find_sender(query, keys[k], &sender, &error);
        if (result != DTNMOS_OK)
        {
            printf("  %s: %s\n", keys[k], error.message);
        }
        REQUIRE(result == DTNMOS_OK);
        CHECK_STR(sender.id.text, VIDEO_ID);
        CHECK_EQ(sender.media, DTNMOS_MEDIA_VIDEO);
        dtnmos_sdp* sdp = NULL;
        REQUIRE(dtnmos_query_sender_sdp(query, &sender, &sdp, &error) == DTNMOS_OK);
        CHECK_STR(dtnmos_string_get(&dtnmos_sdp_session(sdp)->name), "camera 1");
        CHECK_EQ(dtnmos_sdp_flow(sdp, 0)->destination_port, 5004);
        dtnmos_sdp_free(sdp);
        dtnmos_sender_info copy = {0};
        REQUIRE(dtnmos_sender_info_copy(&copy, &sender) == DTNMOS_OK);
        dtnmos_sender_info_clear(&sender);
        CHECK_STR(dtnmos_string_get(&copy.label), "camera 1");
        dtnmos_sender_info_clear(&copy);
    }
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- query_names_what_went_wrong -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void query_names_what_went_wrong(void)
{
    route table[16];
    size_t count = 0;
    char page_one[1024];
    char single[512];
    char by_label[512];
    char twins[512];
    registry_routes(table, &count, page_one, sizeof(page_one), single, sizeof(single),
                    by_label, sizeof(by_label), twins, sizeof(twins));
    fake_registry registry = {table, count, 0, 0};
    dtnmos_query* query = make_query(&registry);
    REQUIRE(query != NULL);
    dtnmos_error error = {DTNMOS_OK, ""};
    dtnmos_sender_info sender = {0};

    // Two senders share a label.
    CHECK(dtnmos_query_find_sender(query, "mic", &sender, &error) == DTNMOS_E_AMBIGUOUS);
    CHECK(strstr(error.message, AUDIO_ID) != NULL &&
          strstr(error.message, TWIN_ID) != NULL);
    // No sender has the ID or the label.
    CHECK(dtnmos_query_find_sender(query, "44444444-4444-4444-8444-444444444444", &sender,
                                   &error) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "no sender 44444444") != NULL);
    table[4].body = "[]";
    CHECK(dtnmos_query_find_sender(query, "camera 1", &sender, &error) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "no sender labelled 'camera 1'") != NULL);
    // A sender without manifest gives no SDP.
    dtnmos_sender_info audio = {0};
    audio.id = (dtnmos_id){AUDIO_ID};
    dtnmos_string_set_text(&audio.label, "mic");
    dtnmos_string text = {0};
    CHECK(dtnmos_query_sender_manifest(query, &audio, &text, &error) ==
          DTNMOS_E_NOT_FOUND);
    CHECK(strstr(error.message, "no manifest_href") != NULL);
    dtnmos_sender_info_clear(&audio);
    // An answer that is no JSON, and an error status.
    table[3].body = "<html>";
    CHECK(dtnmos_query_find_sender(query, VIDEO_ID, &sender, &error) == DTNMOS_E_PARSE);
    CHECK(strstr(error.message, "is no JSON") != NULL);
    table[3].status = 500;
    CHECK(dtnmos_query_find_sender(query, VIDEO_ID, &sender, &error) == DTNMOS_E_HTTP);
    CHECK(strstr(error.message, "answered with 500") != NULL);
    // A registry that does not answer.
    registry.unreachable = 1;
    dtnmos_sender_list* list = NULL;
    CHECK(dtnmos_query_senders(query, &list, &error) == DTNMOS_E_HTTP);
    CHECK(strstr(error.message, "connection refused") != NULL);
    CHECK(list == NULL);
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.- query_refuses_an_incomplete_config -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void query_refuses_an_incomplete_config(void)
{
    dtnmos_query_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    dtnmos_query* query = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    CHECK(dtnmos_query_create(&config, &query, &error) == DTNMOS_E_INVALID_ARGUMENT);
    config.registry_url = "http://registry.test";
    config.http = fake_http;
    config.api_version = "v1.2";
    CHECK(dtnmos_query_create(&config, &query, &error) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(error.message, "v1.2") != NULL);
    CHECK(query == NULL);
    dtnmos_http_response* response = dtnmos_http_response_create();
    dtnmos_http_request request = {sizeof(request), "GET", "http://x", NULL, NULL, 0, 0};
    if (!dtnmos_has_curl())
    {
        CHECK(dtnmos_curl_http(NULL, &request, response, &error) == DTNMOS_E_STATE);
    }
    dtnmos_http_response_free(response);
}
