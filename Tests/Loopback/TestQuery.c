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
    REQUIRE(dtnmos_json_parse(text, strlen(text), &json) == DTNMOS_OK);
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
        const DtNmosResult result = dtnmos_json_parse(texts[i], strlen(texts[i]), &json);
        if (result != DTNMOS_E_PARSE)
        {
            printf("  '%s' parsed\n", texts[i]);
        }
        CHECK(result == DTNMOS_E_PARSE);
        CHECK(json == NULL);
        CHECK(strstr(DtNmos_GetLastError(), "JSON at offset") != NULL);
    }
    // Nesting deeper than the limit fails rather than overflowing the stack.
    char deep[200];
    memset(deep, '[', sizeof(deep) - 1);
    deep[sizeof(deep) - 1] = '\0';
    dtnmos_json* json = NULL;
    CHECK(dtnmos_json_parse(deep, strlen(deep), &json) == DTNMOS_E_PARSE);
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
    DtNmosHttpResponse* response = DtNmosHttpResponse_Alloc();
    REQUIRE(response != NULL);
    CHECK_STR(DtNmosHttpResponse_Body(response, NULL), "");
    DtNmosHttpResponse_SetStatus(response, 200);
    char body[] = "{\"a\": 1}";
    REQUIRE(DtNmosHttpResponse_SetBody(response, "application/json", body,
                                       strlen(body)) == DTNMOS_OK);
    body[0] = 'X';
    REQUIRE(DtNmosHttpResponse_AppendBody(response, " ", 1) == DTNMOS_OK);
    size_t length = 0;
    CHECK_STR(DtNmosHttpResponse_Body(response, &length), "{\"a\": 1} ");
    CHECK_EQ(length, 9);
    REQUIRE(DtNmosHttpResponse_AddHeader(response, "Link", "<x>; rel=\"next\"") ==
            DTNMOS_OK);
    CHECK_STR(DtNmosHttpResponse_FindHeader(response, "link"), "<x>; rel=\"next\"");
    CHECK(DtNmosHttpResponse_FindHeader(response, "Location") == NULL);
    CHECK_EQ(DtNmosHttpResponse_HeaderCount(response), 1);
    CHECK_STR(DtNmosHttpResponse_Header(response, 0).Name, "Link");
    CHECK_STR(DtNmosHttpResponse_ContentType(response), "application/json");
    CHECK_EQ(DtNmosHttpResponse_Status(response), 200);
    DtNmosHttpResponse_Free(response);
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
static DtNmosResult fake_http(void* user, const DtNmosHttpRequest* request,
                              DtNmosHttpResponse* response)
{
    fake_registry* registry = user;
    ++registry->requests;
    if (registry->unreachable)
    {
        return DtNmos_SetLastError(DTNMOS_E_HTTP, "connection refused");
    }
    CHECK_STR(request->Method, "GET");
    CHECK_EQ(request->TimeoutMs, 2000);
    for (size_t i = 0; i < registry->count; ++i)
    {
        if (strcmp(registry->routes[i].url, request->Url) == 0)
        {
            DtNmosHttpResponse_SetStatus(response, registry->routes[i].status);
            DtNmosHttpResponse_SetBody(response, "application/json",
                                       registry->routes[i].body,
                                       strlen(registry->routes[i].body));
            if (registry->routes[i].link != NULL)
            {
                DtNmosHttpResponse_AddHeader(response, "Link", registry->routes[i].link);
            }
            return DTNMOS_OK;
        }
    }
    printf("  no route for %s\n", request->Url);
    DtNmosHttpResponse_SetStatus(response, 404);
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
static DtNmosQuery* make_query(fake_registry* registry)
{
    DtNmosQueryConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.RegistryUrl = "http://registry.test/";
    config.Http = fake_http;
    config.HttpUser = registry;
    config.TimeoutMs = 2000;
    DtNmosQuery* query = DtNmosQuery_Alloc();
    if (query == NULL || DtNmosQuery_Open(query, &config) != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
        DtNmosQuery_Freep(&query);
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
    DtNmosQuery* query = make_query(&registry);
    REQUIRE(query != NULL);
    DtNmosSenderList* list = NULL;
    const DtNmosResult result = DtNmosQuery_Senders(query, &list);
    if (result != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(result == DTNMOS_OK);
    REQUIRE(DtNmosSenderList_Count(list) == 2);
    const DtNmosSenderInfo* video = DtNmosSenderList_At(list, 0);
    CHECK_STR(video->Id.Text, VIDEO_ID);
    CHECK_STR(video->Label, "camera 1");
    CHECK_STR(video->Description, "studio");
    CHECK_EQ(video->Media, DTNMOS_MEDIA_VIDEO);
    CHECK_STR(video->ManifestHref, "http://camera.test/video.sdp");
    CHECK_STR(video->DeviceId.Text, TWIN_ID);
    const DtNmosSenderInfo* audio = DtNmosSenderList_At(list, 1);
    CHECK_EQ(audio->Media, DTNMOS_MEDIA_AUDIO);
    // A string the registry leaves out is empty, not null.
    REQUIRE(audio->ManifestHref != NULL);
    CHECK_STR(audio->ManifestHref, "");
    CHECK(DtNmosSenderList_At(list, 2) == NULL);
    // Two pages of senders and one of flows.
    CHECK_EQ(registry.requests, 3);
    DtNmosSenderList_Free(list);
    DtNmosQuery_Free(query);
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
    DtNmosQuery* query = make_query(&registry);
    REQUIRE(query != NULL);
    const char* const keys[] = {VIDEO_ID, "camera 1"};
    for (size_t k = 0; k < 2; ++k)
    {
        DtNmosSenderList* found = NULL;
        const DtNmosResult result = DtNmosQuery_FindSender(query, keys[k], &found);
        if (result != DTNMOS_OK)
        {
            printf("  %s: %s\n", keys[k], DtNmos_GetLastError());
        }
        REQUIRE(result == DTNMOS_OK);
        REQUIRE(DtNmosSenderList_Count(found) == 1);
        const DtNmosSenderInfo* sender = DtNmosSenderList_At(found, 0);
        CHECK_STR(sender->Id.Text, VIDEO_ID);
        CHECK_STR(sender->Label, "camera 1");
        CHECK_EQ(sender->Media, DTNMOS_MEDIA_VIDEO);
        DtNmosSdp* sdp = NULL;
        REQUIRE(DtNmosQuery_SenderSdp(query, sender, &sdp) == DTNMOS_OK);
        CHECK_STR(DtNmosSdp_Session(sdp)->Name, "camera 1");
        CHECK_EQ(DtNmosSdp_Flow(sdp, 0)->DestinationPort, 5004);
        DtNmosSdp_Free(sdp);
        DtNmosSenderList_Free(found);
    }
    DtNmosQuery_Free(query);
}

// .-.-.-.-.-.-.-.-.- query_writes_a_manifest_into_the_callers_buffer -.-.-.-.-.-.-.-.-.-.
//
void query_writes_a_manifest_into_the_callers_buffer(void)
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
    DtNmosQuery* query = make_query(&registry);
    REQUIRE(query != NULL);
    DtNmosSenderList* found = NULL;
    REQUIRE(DtNmosQuery_FindSender(query, VIDEO_ID, &found) == DTNMOS_OK);
    const DtNmosSenderInfo* sender = DtNmosSenderList_At(found, 0);

    // Without a buffer, it says how many bytes the SDP needs.
    size_t size = 0;
    CHECK(DtNmosQuery_SenderManifest(query, sender, NULL, &size) ==
          DTNMOS_E_BUFFER_TOO_SMALL);
    REQUIRE(size > 1);
    const size_t needed = size;
    char small[8];
    size = sizeof(small);
    CHECK(DtNmosQuery_SenderManifest(query, sender, small, &size) ==
          DTNMOS_E_BUFFER_TOO_SMALL);
    CHECK_EQ(size, needed);
    // A buffer of that size takes it, and the size is then its length.
    char text[4096];
    REQUIRE(needed <= sizeof(text));
    size = needed;
    REQUIRE(DtNmosQuery_SenderManifest(query, sender, text, &size) == DTNMOS_OK);
    CHECK_EQ(size, needed - 1);
    CHECK_EQ(strlen(text), needed - 1);
    CHECK(strstr(text, "s=camera 1") != NULL);
    DtNmosSenderList_Free(found);
    DtNmosQuery_Free(query);
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
    DtNmosQuery* query = make_query(&registry);
    REQUIRE(query != NULL);
    DtNmosSenderList* sender = NULL;

    // Two senders share a label.
    CHECK(DtNmosQuery_FindSender(query, "mic", &sender) == DTNMOS_E_AMBIGUOUS);
    CHECK(strstr(DtNmos_GetLastError(), AUDIO_ID) != NULL &&
          strstr(DtNmos_GetLastError(), TWIN_ID) != NULL);
    CHECK(sender == NULL);
    // No sender has the ID or the label.
    CHECK(DtNmosQuery_FindSender(query, "44444444-4444-4444-8444-444444444444",
                                 &sender) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no sender 44444444") != NULL);
    table[4].body = "[]";
    CHECK(DtNmosQuery_FindSender(query, "camera 1", &sender) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no sender labelled 'camera 1'") != NULL);
    // A sender without manifest gives no SDP.
    DtNmosSenderInfo audio = {0};
    audio.Id = (DtNmosId){AUDIO_ID};
    audio.Label = "mic";
    audio.ManifestHref = "";
    char text[64];
    size_t size = sizeof(text);
    CHECK(DtNmosQuery_SenderManifest(query, &audio, text, &size) == DTNMOS_E_NOT_FOUND);
    CHECK(strstr(DtNmos_GetLastError(), "no manifest_href") != NULL);
    // An answer that is no JSON, and an error status.
    table[3].body = "<html>";
    CHECK(DtNmosQuery_FindSender(query, VIDEO_ID, &sender) == DTNMOS_E_PARSE);
    CHECK(strstr(DtNmos_GetLastError(), "is no JSON") != NULL);
    table[3].status = 500;
    CHECK(DtNmosQuery_FindSender(query, VIDEO_ID, &sender) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "answered with 500") != NULL);
    // A registry that does not answer.
    registry.unreachable = 1;
    DtNmosSenderList* list = NULL;
    CHECK(DtNmosQuery_Senders(query, &list) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(), "connection refused") != NULL);
    CHECK(list == NULL);
    DtNmosQuery_Free(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.- query_refuses_an_incomplete_config -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void query_refuses_an_incomplete_config(void)
{
    DtNmosQueryConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    DtNmosQuery* query = DtNmosQuery_Alloc();
    REQUIRE(query != NULL);
    CHECK(DtNmosQuery_Open(query, &config) == DTNMOS_E_INVALID_ARGUMENT);
    config.RegistryUrl = "http://registry.test";
    config.Http = fake_http;
    config.ApiVersion = "v1.2";
    CHECK(DtNmosQuery_Open(query, &config) == DTNMOS_E_INVALID_ARGUMENT);
    CHECK(strstr(DtNmos_GetLastError(), "v1.2") != NULL);
    // A query that did not open is closed: it asks nothing.
    DtNmosSenderList* list = NULL;
    CHECK(DtNmosQuery_Senders(query, &list) == DTNMOS_E_STATE);
    CHECK(strstr(DtNmos_GetLastError(), "needs an open query") != NULL);
    CHECK(DtNmosQuery_Close(query) == DTNMOS_E_STATE);
    DtNmosQuery_Freep(&query);
    CHECK(query == NULL);
    DtNmosQuery_Freep(&query);
    DtNmosHttpResponse* response = DtNmosHttpResponse_Alloc();
    DtNmosHttpRequest request = {sizeof(request), "GET", "http://x", NULL, NULL, 0, 0};
    if (!DtNmos_HasCurl())
    {
        CHECK(DtNmos_CurlHttp(NULL, &request, response) == DTNMOS_E_STATE);
    }
    DtNmosHttpResponse_Free(response);
}
