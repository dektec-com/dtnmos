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
#include "NmosTest.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- JsonReadsValuesAndEscapes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(JsonReadsValuesAndEscapes)
{
    const char* Text =
        "{\"id\": \"abc\", \"n\": -12.5e1, \"list\": [true, false, null, [], {}], "
        "\"text\": \"a\\\"b\\\\c\\/d\\n\\u00e9\\ud83d\\ude00\"}";
    NmosJson* Json = NULL;
    NMOS_ASSERT(NmosJson_Parse(Text, strlen(Text), &Json) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Json->Type, DTNMOS_JSON_OBJECT);
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "id"), "abc");
    const NmosJson* n = NmosJson_Member(Json, "n");
    NMOS_ASSERT(n != NULL);
    NMOS_ASSERT(n->Type == DTNMOS_JSON_NUMBER && n->Number == -125.0);
    const NmosJson* List = NmosJson_Member(Json, "list");
    NMOS_ASSERT(List != NULL && List->Type == DTNMOS_JSON_ARRAY);
    NMOS_ASSERT_EQ(List->Count, 5);
    NMOS_ASSERT_EQ(List->Items[0].Type, DTNMOS_JSON_TRUE);
    NMOS_ASSERT_EQ(List->Items[2].Type, DTNMOS_JSON_NULL);
    NMOS_ASSERT_EQ(List->Items[4].Type, DTNMOS_JSON_OBJECT);
    // Escapes, a character of two bytes in UTF-8, and one of four from a surrogate pair.
    NMOS_ASSERT_STR(NmosJson_MemberText(Json, "text"),
                    "a\"b\\c/d\n\xc3\xa9\xf0\x9f\x98\x80");
    NMOS_ASSERT(NmosJson_Member(Json, "missing") == NULL);
    NMOS_ASSERT(NmosJson_MemberText(Json, "n") == NULL);
    NmosJson_Free(Json);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- JsonRefusesWhatIsMalformed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(JsonRefusesWhatIsMalformed)
{
    const char* const Texts[] = {"",    "{",     "[1,]",    "{\"a\" 1}",   "\"open",
                                 "tru", "[1] 2", "\"\\x\"", "\"\\ud83d\"", "{1: 2}"};
    for (size_t i = 0; i < sizeof(Texts) / sizeof(Texts[0]); ++i)
    {
        NmosJson* Json = (NmosJson*)&Json;
        const DtNmosResult Result = NmosJson_Parse(Texts[i], strlen(Texts[i]), &Json);
        if (Result != DTNMOS_E_PARSE)
        {
            printf("  '%s' parsed\n", Texts[i]);
        }
        NMOS_ASSERT(Result == DTNMOS_E_PARSE);
        NMOS_ASSERT(Json == NULL);
        NMOS_ASSERT(strstr(DtNmos_GetLastError(), "JSON at offset") != NULL);
    }
    // Nesting deeper than the limit fails rather than overflowing the stack.
    char Deep[200];
    memset(Deep, '[', sizeof(Deep) - 1);
    Deep[sizeof(Deep) - 1] = '\0';
    NmosJson* Json = NULL;
    NMOS_ASSERT(NmosJson_Parse(Deep, strlen(Deep), &Json) == DTNMOS_E_PARSE);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- JsonWritesEscapedStrings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(JsonWritesEscapedStrings)
{
    NmosBuffer Buffer;
    memset(&Buffer, 0, sizeof(Buffer));
    NmosJson_WriteString(&Buffer, "a\"b\\c\n\x01");
    NMOS_ASSERT(!Buffer.Failed);
    NMOS_ASSERT_STR(Buffer.Data, "\"a\\\"b\\\\c\\n\\u0001\"");
    NmosBuffer_Free(&Buffer);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- HttpResponseOwnsWhatItHolds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(HttpResponseOwnsWhatItHolds)
{
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    NMOS_ASSERT(Response != NULL);
    NMOS_ASSERT_STR(DtNmosHttpResponse_Body(Response, NULL), "");
    DtNmosHttpResponse_SetStatus(Response, 200);
    char Body[] = "{\"a\": 1}";
    NMOS_ASSERT(DtNmosHttpResponse_SetBody(Response, "application/json", Body,
                                           strlen(Body)) == DTNMOS_OK);
    Body[0] = 'X';
    NMOS_ASSERT(DtNmosHttpResponse_AppendBody(Response, " ", 1) == DTNMOS_OK);
    size_t Length = 0;
    NMOS_ASSERT_STR(DtNmosHttpResponse_Body(Response, &Length), "{\"a\": 1} ");
    NMOS_ASSERT_EQ(Length, 9);
    NMOS_ASSERT(DtNmosHttpResponse_AddHeader(Response, "Link", "<x>; rel=\"next\"") ==
                DTNMOS_OK);
    NMOS_ASSERT_STR(DtNmosHttpResponse_FindHeader(Response, "link"), "<x>; rel=\"next\"");
    NMOS_ASSERT(DtNmosHttpResponse_FindHeader(Response, "Location") == NULL);
    NMOS_ASSERT_EQ(DtNmosHttpResponse_HeaderCount(Response), 1);
    NMOS_ASSERT_STR(DtNmosHttpResponse_Header(Response, 0).Name, "Link");
    NMOS_ASSERT_STR(DtNmosHttpResponse_ContentType(Response), "application/json");
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response), 200);
    DtNmosHttpResponse_Free(Response);
}

// A registry that the test answers for: each route an URL and what it gives.
typedef struct NmosRoute
{
    const char* Url;
    int Status;
    const char* Body;
    const char* Link; // the Link header, or null
} NmosRoute;

typedef struct NmosFakeRegistry
{
    const NmosRoute* Routes;
    size_t Count;
    int Requests;
    bool Unreachable; // every request fails as if the registry did not answer
} NmosFakeRegistry;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult FakeHttp(void* User, const DtNmosHttpRequest* Request,
                             DtNmosHttpResponse* Response)
{
    NmosFakeRegistry* Registry = User;
    ++Registry->Requests;
    if (Registry->Unreachable)
    {
        return DtNmos_SetLastError(DTNMOS_E_HTTP, "connection refused");
    }
    NMOS_EXPECT(strcmp(Request->Method, "GET") == 0);
    NMOS_EXPECT(Request->TimeoutMs == 2000);
    for (size_t i = 0; i < Registry->Count; ++i)
    {
        if (strcmp(Registry->Routes[i].Url, Request->Url) == 0)
        {
            DtNmosHttpResponse_SetStatus(Response, Registry->Routes[i].Status);
            DtNmosHttpResponse_SetBody(Response, "application/json",
                                       Registry->Routes[i].Body,
                                       strlen(Registry->Routes[i].Body));
            if (Registry->Routes[i].Link != NULL)
            {
                DtNmosHttpResponse_AddHeader(Response, "Link", Registry->Routes[i].Link);
            }
            return DTNMOS_OK;
        }
    }
    printf("  no route for %s\n", Request->Url);
    DtNmosHttpResponse_SetStatus(Response, 404);
    return DTNMOS_OK;
}

#define VIDEO_ID "11111111-1111-4111-8111-111111111111"
#define AUDIO_ID "22222222-2222-4222-8222-222222222222"
#define TWIN_ID "33333333-3333-4333-8333-333333333333"
#define VIDEO_FLOW "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
#define AUDIO_FLOW "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
#define BASE "http://registry.test/x-nmos/query/v1.3/"

static const char* const VideoSender =
    "{\"id\": \"" VIDEO_ID "\", \"label\": \"camera 1\", \"description\": \"studio\", "
    "\"flow_id\": \"" VIDEO_FLOW "\", \"device_id\": \"" TWIN_ID "\", "
    "\"transport\": \"urn:x-nmos:transport:rtp.mcast\", "
    "\"manifest_href\": \"http://camera.test/video.sdp\"}";
static const char* const AudioSender =
    "{\"id\": \"" AUDIO_ID "\", \"label\": \"mic\", \"flow_id\": \"" AUDIO_FLOW "\", "
    "\"manifest_href\": null}";
static const char* const TwinSender = "{\"id\": \"" TWIN_ID "\", \"label\": \"mic\"}";

static const NmosRoute Routes[] = {
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegistryRoutes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Builds the routes of a registry with two pages of senders, the second reached through
// the Link header of the first, as the paging of IS-04 does.
//
static void RegistryRoutes(NmosRoute* Table, size_t* Count, char* PageOne,
                           size_t PageOneSize, char* Single, size_t SingleSize,
                           char* ByLabel, size_t ByLabelSize, char* Twins,
                           size_t TwinsSize)
{
    memcpy(Table, Routes, sizeof(Routes));
    *Count = sizeof(Routes) / sizeof(Routes[0]);
    snprintf(PageOne, PageOneSize, "[%s, %s]", VideoSender, AudioSender);
    snprintf(Single, SingleSize, "%s", VideoSender);
    snprintf(ByLabel, ByLabelSize, "[%s]", VideoSender);
    snprintf(Twins, TwinsSize, "[%s, %s]", AudioSender, TwinSender);
    Table[0].Body = PageOne;
    Table[0].Link = "<" BASE "senders?paging.limit=100&paging.until=2:0>; rel=\"next\", "
                    "<" BASE "senders?paging.limit=100>; rel=\"first\"";
    Table[3].Body = Single;
    Table[4].Body = ByLabel;
    Table[5].Body = Twins;
    // The second page, empty, ends the list.
    Table[*Count].Url = BASE "senders?paging.limit=100&paging.until=2:0";
    Table[*Count].Status = 200;
    Table[*Count].Body = "[]";
    Table[*Count].Link = NULL;
    ++*Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeQuery -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosQuery* MakeQuery(NmosFakeRegistry* Registry)
{
    DtNmosQueryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.RegistryUrl = "http://registry.test/";
    Config.Http = FakeHttp;
    Config.HttpUser = Registry;
    Config.TimeoutMs = 2000;
    DtNmosQuery* Query = DtNmosQuery_Alloc();
    if (Query == NULL || DtNmosQuery_Open(Query, &Config) != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
        DtNmosQuery_Freep(&Query);
    }
    return Query;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- QueryListsTheSendersOfEveryPage -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(QueryListsTheSendersOfEveryPage)
{
    NmosRoute Table[16];
    size_t Count = 0;
    char PageOne[1024];
    char Single[512];
    char ByLabel[512];
    char Twins[512];
    RegistryRoutes(Table, &Count, PageOne, sizeof(PageOne), Single, sizeof(Single),
                   ByLabel, sizeof(ByLabel), Twins, sizeof(Twins));
    NmosFakeRegistry Registry = {Table, Count, 0, 0};
    DtNmosQuery* Query = MakeQuery(&Registry);
    NMOS_ASSERT(Query != NULL);
    DtNmosSenderList* List = NULL;
    const DtNmosResult Result = DtNmosQuery_Senders(Query, &List);
    if (Result != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    NMOS_ASSERT(Result == DTNMOS_OK);
    NMOS_ASSERT(DtNmosSenderList_Count(List) == 2);
    const DtNmosSenderInfo* Video = DtNmosSenderList_At(List, 0);
    NMOS_ASSERT_STR(Video->Id.Text, VIDEO_ID);
    NMOS_ASSERT_STR(Video->Label, "camera 1");
    NMOS_ASSERT_STR(Video->Description, "studio");
    NMOS_ASSERT_EQ(Video->Media, DTNMOS_MEDIA_VIDEO);
    NMOS_ASSERT_STR(Video->ManifestHref, "http://camera.test/video.sdp");
    NMOS_ASSERT_STR(Video->DeviceId.Text, TWIN_ID);
    const DtNmosSenderInfo* Audio = DtNmosSenderList_At(List, 1);
    NMOS_ASSERT_EQ(Audio->Media, DTNMOS_MEDIA_AUDIO);
    // A string the registry leaves out is empty, not null.
    NMOS_ASSERT(Audio->ManifestHref != NULL);
    NMOS_ASSERT_STR(Audio->ManifestHref, "");
    NMOS_ASSERT(DtNmosSenderList_At(List, 2) == NULL);
    // Two pages of senders and one of flows.
    NMOS_ASSERT_EQ(Registry.Requests, 3);
    DtNmosSenderList_Free(List);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- QueryFindsASenderAndItsSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(QueryFindsASenderAndItsSdp)
{
    NmosRoute Table[16];
    size_t Count = 0;
    char PageOne[1024];
    char Single[512];
    char ByLabel[512];
    char Twins[512];
    RegistryRoutes(Table, &Count, PageOne, sizeof(PageOne), Single, sizeof(Single),
                   ByLabel, sizeof(ByLabel), Twins, sizeof(Twins));
    NmosFakeRegistry Registry = {Table, Count, 0, 0};
    DtNmosQuery* Query = MakeQuery(&Registry);
    NMOS_ASSERT(Query != NULL);
    const char* const Keys[] = {VIDEO_ID, "camera 1"};
    for (size_t k = 0; k < 2; ++k)
    {
        DtNmosSenderList* Found = NULL;
        const DtNmosResult Result = DtNmosQuery_FindSender(Query, Keys[k], &Found);
        if (Result != DTNMOS_OK)
        {
            printf("  %s: %s\n", Keys[k], DtNmos_GetLastError());
        }
        NMOS_ASSERT(Result == DTNMOS_OK);
        NMOS_ASSERT(DtNmosSenderList_Count(Found) == 1);
        const DtNmosSenderInfo* Sender = DtNmosSenderList_At(Found, 0);
        NMOS_ASSERT_STR(Sender->Id.Text, VIDEO_ID);
        NMOS_ASSERT_STR(Sender->Label, "camera 1");
        NMOS_ASSERT_EQ(Sender->Media, DTNMOS_MEDIA_VIDEO);
        DtNmosSdp* Sdp = NULL;
        NMOS_ASSERT(DtNmosQuery_SenderSdp(Query, Sender, &Sdp) == DTNMOS_OK);
        NMOS_ASSERT_STR(DtNmosSdp_Session(Sdp)->Name, "camera 1");
        NMOS_ASSERT_EQ(DtNmosSdp_Flow(Sdp, 0)->DestinationPort, 5004);
        DtNmosSdp_Free(Sdp);
        DtNmosSenderList_Free(Found);
    }
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.- QueryWritesAManifestIntoTheCallersBuffer -.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(QueryWritesAManifestIntoTheCallersBuffer)
{
    NmosRoute Table[16];
    size_t Count = 0;
    char PageOne[1024];
    char Single[512];
    char ByLabel[512];
    char Twins[512];
    RegistryRoutes(Table, &Count, PageOne, sizeof(PageOne), Single, sizeof(Single),
                   ByLabel, sizeof(ByLabel), Twins, sizeof(Twins));
    NmosFakeRegistry Registry = {Table, Count, 0, 0};
    DtNmosQuery* Query = MakeQuery(&Registry);
    NMOS_ASSERT(Query != NULL);
    DtNmosSenderList* Found = NULL;
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, VIDEO_ID, &Found) == DTNMOS_OK);
    const DtNmosSenderInfo* Sender = DtNmosSenderList_At(Found, 0);

    // Without a buffer, it says how many bytes the SDP needs.
    size_t Size = 0;
    NMOS_ASSERT(DtNmosQuery_SenderManifest(Query, Sender, NULL, &Size) ==
                DTNMOS_E_BUFFER_TOO_SMALL);
    NMOS_ASSERT(Size > 1);
    const size_t Needed = Size;
    char Small[8];
    Size = sizeof(Small);
    NMOS_ASSERT(DtNmosQuery_SenderManifest(Query, Sender, Small, &Size) ==
                DTNMOS_E_BUFFER_TOO_SMALL);
    NMOS_ASSERT_EQ(Size, Needed);
    // A buffer of that size takes it, and the size is then its length.
    char Text[4096];
    NMOS_ASSERT(Needed <= sizeof(Text));
    Size = Needed;
    NMOS_ASSERT(DtNmosQuery_SenderManifest(Query, Sender, Text, &Size) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Size, Needed - 1);
    NMOS_ASSERT_EQ(strlen(Text), Needed - 1);
    NMOS_ASSERT(strstr(Text, "s=camera 1") != NULL);
    DtNmosSenderList_Free(Found);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- QueryNamesWhatWentWrong -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(QueryNamesWhatWentWrong)
{
    NmosRoute Table[16];
    size_t Count = 0;
    char PageOne[1024];
    char Single[512];
    char ByLabel[512];
    char Twins[512];
    RegistryRoutes(Table, &Count, PageOne, sizeof(PageOne), Single, sizeof(Single),
                   ByLabel, sizeof(ByLabel), Twins, sizeof(Twins));
    NmosFakeRegistry Registry = {Table, Count, 0, 0};
    DtNmosQuery* Query = MakeQuery(&Registry);
    NMOS_ASSERT(Query != NULL);
    DtNmosSenderList* Sender = NULL;

    // Two senders share a label.
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, "mic", &Sender) == DTNMOS_E_AMBIGUOUS);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), AUDIO_ID) != NULL &&
                strstr(DtNmos_GetLastError(), TWIN_ID) != NULL);
    NMOS_ASSERT(Sender == NULL);
    // No sender has the ID or the label.
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, "44444444-4444-4444-8444-444444444444",
                                       &Sender) == DTNMOS_E_NOT_FOUND);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "no sender 44444444") != NULL);
    Table[4].Body = "[]";
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, "camera 1", &Sender) == DTNMOS_E_NOT_FOUND);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "no sender labelled 'camera 1'") != NULL);
    // A sender without manifest gives no SDP.
    DtNmosSenderInfo Audio = {0};
    Audio.Id = (DtNmosId){AUDIO_ID};
    Audio.Label = "mic";
    Audio.ManifestHref = "";
    char Text[64];
    size_t Size = sizeof(Text);
    NMOS_ASSERT(DtNmosQuery_SenderManifest(Query, &Audio, Text, &Size) ==
                DTNMOS_E_NOT_FOUND);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "no manifest_href") != NULL);
    // An answer that is no JSON, and an error status.
    Table[3].Body = "<html>";
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, VIDEO_ID, &Sender) == DTNMOS_E_PARSE);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "is no JSON") != NULL);
    Table[3].Status = 500;
    NMOS_ASSERT(DtNmosQuery_FindSender(Query, VIDEO_ID, &Sender) == DTNMOS_E_HTTP);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "answered with 500") != NULL);
    // A registry that does not answer.
    Registry.Unreachable = true;
    DtNmosSenderList* List = NULL;
    NMOS_ASSERT(DtNmosQuery_Senders(Query, &List) == DTNMOS_E_HTTP);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "connection refused") != NULL);
    NMOS_ASSERT(List == NULL);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- QueryRefusesAnIncompleteConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(QueryRefusesAnIncompleteConfig)
{
    DtNmosQueryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    DtNmosQuery* Query = DtNmosQuery_Alloc();
    NMOS_ASSERT(Query != NULL);
    NMOS_ASSERT(DtNmosQuery_Open(Query, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    Config.RegistryUrl = "http://registry.test";
    Config.Http = FakeHttp;
    Config.ApiVersion = "v1.2";
    NMOS_ASSERT(DtNmosQuery_Open(Query, &Config) == DTNMOS_E_INVALID_ARGUMENT);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "v1.2") != NULL);
    // A query that did not open is closed: it asks nothing.
    DtNmosSenderList* List = NULL;
    NMOS_ASSERT(DtNmosQuery_Senders(Query, &List) == DTNMOS_E_STATE);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "needs an open query") != NULL);
    NMOS_ASSERT(DtNmosQuery_Close(Query) == DTNMOS_E_STATE);
    DtNmosQuery_Freep(&Query);
    NMOS_ASSERT(Query == NULL);
    DtNmosQuery_Freep(&Query);
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    DtNmosHttpRequest Request = {sizeof(Request), "GET", "http://x", NULL, NULL, 0, 0};
    if (!DtNmos_HasCurl())
    {
        NMOS_ASSERT(DtNmos_CurlHttp(NULL, &Request, Response) == DTNMOS_E_STATE);
    }
    DtNmosHttpResponse_Free(Response);
}

NMOS_TEST_MAIN("Query", NMOS_RUN(JsonReadsValuesAndEscapes),
               NMOS_RUN(JsonRefusesWhatIsMalformed), NMOS_RUN(JsonWritesEscapedStrings),
               NMOS_RUN(HttpResponseOwnsWhatItHolds),
               NMOS_RUN(QueryListsTheSendersOfEveryPage),
               NMOS_RUN(QueryFindsASenderAndItsSdp), NMOS_RUN(QueryNamesWhatWentWrong),
               NMOS_RUN(QueryWritesAManifestIntoTheCallersBuffer),
               NMOS_RUN(QueryRefusesAnIncompleteConfig))
