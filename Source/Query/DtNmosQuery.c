// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosQuery.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The Query API of an NMOS registry (IS-04 v1.3)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosQuery.h"

// The most pages a list follows, which bounds a registry whose paging never ends.
#define DTNMOS_MAX_PAGES 1000

// The size of a page the query asks for.
#define DTNMOS_PAGE_LIMIT 100

// A query is allocated empty and closed; DtNmosQuery_Open() fills it in, and
// DtNmosQuery_Close() empties it again.
struct DtNmosQuery
{
    int Open;
    char* Base; // registry URL followed by /x-nmos/query/v1.3/
    DtNmosHttpFunc Http;
    void* HttpUser;
    uint32_t TimeoutMs;
    DtNmosLogFunc Log;
    void* LogUser;
};

// A list owns its strings in its store; a list of one, as finding a sender returns, is
// made with room for that one.
struct DtNmosSenderList
{
    DtNmosSenderInfo* Senders;
    size_t Count;
    NmosStore Store;
};

struct DtNmosReceiverList
{
    DtNmosReceiverInfo* Receivers;
    size_t Count;
    NmosStore Store;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosSenderList_Count(const DtNmosSenderList* List)
{
    return List == NULL ? 0 : List->Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosSenderInfo* DtNmosSenderList_At(const DtNmosSenderList* List, size_t Index)
{
    return List == NULL || Index >= List->Count ? NULL : &List->Senders[Index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosSenderList_Free(DtNmosSenderList* List)
{
    if (List == NULL)
    {
        return;
    }
    NmosStore_Free(&List->Store);
    free(List->Senders);
    free(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static char* CopyText(const char* Text, size_t Length)
{
    char* Copy = malloc(Length + 1);
    if (Copy != NULL)
    {
        memcpy(Copy, Text, Length);
        Copy[Length] = '\0';
    }
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_Open(DtNmosQuery* Query, const DtNmosQueryConfig* Config)
{
    if (Query == NULL || Config == NULL || Config->RegistryUrl == NULL ||
        Config->RegistryUrl[0] == '\0' || Config->Http == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "A query needs the URL of a registry and an HTTP function.");
    }
    if (Query->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "The query is open already; close it first.");
    }
    const DtNmosResult Sized =
        DTNMOS_CHECK_SIZE(Config, DtNmosQueryConfig, sizeof(DtNmosQueryConfig));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    if (Config->ApiVersion != NULL && strcmp(Config->ApiVersion, "v1.3") != 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The Query API %s is not supported; dtnmos speaks v1.3.",
                              Config->ApiVersion);
    }
    DtNmosQuery* Result = Query;
    size_t Length = strlen(Config->RegistryUrl);
    while (Length > 0 && Config->RegistryUrl[Length - 1] == '/')
    {
        --Length;
    }
    NmosBuffer Base;
    memset(&Base, 0, sizeof(Base));
    NmosBuffer_Append(&Base, Config->RegistryUrl, Length);
    DTNMOS_APPEND_LITERAL(&Base, "/x-nmos/query/v1.3/");
    if (Base.Failed)
    {
        NmosBuffer_Free(&Base);
        return NmosError_FailMemory();
    }
    Result->Base = Base.Data;
    Result->Http = Config->Http;
    Result->HttpUser = Config->HttpUser;
    Result->TimeoutMs = Config->TimeoutMs == 0 ? 5000 : Config->TimeoutMs;
    Result->Log = Config->Log;
    Result->LogUser = Config->LogUser;
    Result->Open = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosQuery* DtNmosQuery_Alloc(void)
{
    return calloc(1, sizeof(DtNmosQuery));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Close(DtNmosQuery* Query)
{
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_Close");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    free(Query->Base);
    memset(Query, 0, sizeof(*Query));
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosQuery_Free(DtNmosQuery* Query)
{
    if (Query == NULL)
    {
        return;
    }
    if (Query->Open)
    {
        DtNmosQuery_Close(Query);
    }
    free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosQuery_Freep(DtNmosQuery** Query)
{
    if (Query != NULL)
    {
        DtNmosQuery_Free(*Query);
        *Query = NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosQuery_CheckOpen(const DtNmosQuery* Query, const char* Function)
{
    if (Query == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "%s() needs a query.", Function);
    }
    if (!Query->Open)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "%s() needs an open query.", Function);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LogMessage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void LogMessage(DtNmosQuery* Query, DtNmosLogLevel Level, const char* Message)
{
    if (Query->Log != NULL)
    {
        Query->Log(Query->LogUser, Level, Message);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Base -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* NmosQuery_Base(const DtNmosQuery* Query)
{
    return Query->Base;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Timeout -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t NmosQuery_Timeout(const DtNmosQuery* Query)
{
    return Query->TimeoutMs;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Request -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosQuery_Request(DtNmosQuery* Query, const char* Method, const char* Url,
                               const char* ContentType, const char* Body,
                               size_t BodyLength, DtNmosHttpResponse* Response)
{
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = Method;
    Request.Url = Url;
    Request.ContentType = Body == NULL ? NULL : ContentType;
    Request.Body = Body;
    Request.BodyLength = Body == NULL ? 0 : BodyLength;
    Request.TimeoutMs = Query->TimeoutMs;
    char Message[600];
    snprintf(Message, sizeof(Message), "%s %s", Method, Url);
    LogMessage(Query, DTNMOS_LOG_DEBUG, Message);
    NmosError_Clear();
    DtNmosResult Result = Query->Http(Query->HttpUser, &Request, Response);
    if (Result != DTNMOS_OK && DtNmos_GetLastError()[0] == '\0')
    {
        NmosError_Fail(Result, "%s %s failed.", Method, Url);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Get -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Performs a GET of url into response; fails unless the answer is 200, with
// DTNMOS_E_NOT_FOUND for 404.
//
static DtNmosResult Get(DtNmosQuery* Query, const char* Url, DtNmosHttpResponse* Response)
{
    DtNmosResult Result = NmosQuery_Request(Query, "GET", Url, NULL, NULL, 0, Response);
    if (Result != DTNMOS_OK)
    {
        return Result;
    }
    const int Status = DtNmosHttpResponse_Status(Response);
    if (Status == 404)
    {
        return NmosError_Fail(DTNMOS_E_NOT_FOUND, "The registry has no %s (404).", Url);
    }
    if (Status != 200)
    {
        return NmosError_Fail(DTNMOS_E_HTTP, "GET %s was answered with %d.", Url, Status);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetJson -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Performs a GET of url and parses its JSON; the caller frees json and, when next is not
// null, *Next, the URL of the next page from the Link header, or null.
//
static DtNmosResult GetJson(DtNmosQuery* Query, const char* Url, NmosJson** Json,
                            char** Next)
{
    *Json = NULL;
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    if (Response == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult Result = Get(Query, Url, Response);
    if (Result == DTNMOS_OK)
    {
        size_t Length = 0;
        const char* Body = DtNmosHttpResponse_Body(Response, &Length);
        Result = NmosJson_Parse(Body, Length, Json);
        if (Result != DTNMOS_OK)
        {
            char Reason[512];
            snprintf(Reason, sizeof(Reason), "%s", DtNmos_GetLastError());
            Result = NmosError_Fail(DTNMOS_E_PARSE, "The answer to GET %s is no JSON: %s",
                                    Url, Reason);
        }
    }
    if (Result == DTNMOS_OK && Next != NULL)
    {
        *Next = NULL;
        // Link: <url>; rel="next", <url>; rel="prev", ...
        const char* Link = DtNmosHttpResponse_FindHeader(Response, "Link");
        NmosSpan Rest = NmosSpan_Of(Link);
        while (Rest.Data != NULL && Rest.Length > 0)
        {
            NmosSpan Entry;
            Rest = NmosSpan_Split(Rest, ',', &Entry);
            Entry = NmosSpan_Trim(Entry);
            if (Entry.Length < 2 || Entry.Data[0] != '<')
            {
                continue;
            }
            const char* Close = memchr(Entry.Data, '>', Entry.Length);
            if (Close == NULL)
            {
                continue;
            }
            const NmosSpan After = {Close + 1,
                                    Entry.Length - (size_t)(Close + 1 - Entry.Data)};
            const char* Rel = NULL;
            for (size_t i = 0; i + 10 <= After.Length; ++i)
            {
                if (memcmp(After.Data + i, "rel=\"next\"", 10) == 0)
                {
                    Rel = After.Data + i;
                    break;
                }
            }
            if (Rel != NULL)
            {
                *Next = CopyText(Entry.Data + 1, (size_t)(Close - Entry.Data - 1));
                if (*Next == NULL)
                {
                    Result = NmosError_FailMemory();
                }
                break;
            }
        }
    }
    if (Result != DTNMOS_OK && *Json != NULL)
    {
        NmosJson_Free(*Json);
        *Json = NULL;
    }
    DtNmosHttpResponse_Free(Response);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_GetJson -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosQuery_GetJson(DtNmosQuery* Query, const char* Url, NmosJson** Json)
{
    return GetJson(Query, Url, Json, NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Builds base + path, and a query of the paging limit.
//
static char* MakeUrl(const DtNmosQuery* Query, const char* Path, const char* Parameters)
{
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    NmosBuffer_Printf(&Url, "%s%s?%s%spaging.limit=%d", Query->Base, Path,
                      Parameters == NULL ? "" : Parameters, Parameters == NULL ? "" : "&",
                      DTNMOS_PAGE_LIMIT);
    if (Url.Failed)
    {
        NmosBuffer_Free(&Url);
        return NULL;
    }
    return Url.Data;
}

// A growing array of JSON values of one or more pages, which it owns.
typedef struct NmosPages
{
    NmosJson** Values;
    size_t Count;
    size_t Capacity;
} NmosPages;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreePages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FreePages(NmosPages* p)
{
    for (size_t i = 0; i < p->Count; ++i)
    {
        NmosJson_Free(p->Values[i]);
    }
    free(p->Values);
    memset(p, 0, sizeof(*p));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetPages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fetches every page of the list at url, each an array, into p.
//
static DtNmosResult GetPages(DtNmosQuery* Query, char* Url, NmosPages* p)
{
    for (int Page = 0; Url != NULL; ++Page)
    {
        if (Page == DTNMOS_MAX_PAGES)
        {
            free(Url);
            return NmosError_Fail(DTNMOS_E_HTTP, "The registry pages on beyond %d pages.",
                                  DTNMOS_MAX_PAGES);
        }
        NmosJson* Json = NULL;
        char* Next = NULL;
        DtNmosResult Result = GetJson(Query, Url, &Json, &Next);
        if (Result == DTNMOS_OK && Json->Type != DTNMOS_JSON_ARRAY)
        {
            NmosJson_Free(Json);
            Result =
                NmosError_Fail(DTNMOS_E_PARSE, "The answer to GET %s is no array.", Url);
        }
        if (Result != DTNMOS_OK)
        {
            free(Url);
            free(Next);
            return Result;
        }
        // An empty page ends the list; a page that points back at itself does too.
        const int End = Json->Count == 0 || (Next != NULL && strcmp(Next, Url) == 0);
        if (p->Count == p->Capacity)
        {
            const size_t Capacity = p->Capacity == 0 ? 4 : p->Capacity * 2;
            NmosJson** Values = realloc(p->Values, Capacity * sizeof(*Values));
            if (Values == NULL)
            {
                NmosJson_Free(Json);
                free(Url);
                free(Next);
                return NmosError_FailMemory();
            }
            p->Values = Values;
            p->Capacity = Capacity;
        }
        p->Values[p->Count++] = Json;
        free(Url);
        Url = End ? NULL : Next;
        if (End)
        {
            free(Next);
        }
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MediaOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the media of a format of IS-04 and a media type, which may be null.
//
static DtNmosMedia MediaOf(const char* Format, const char* MediaType)
{
    if (Format == NULL)
    {
        return DTNMOS_MEDIA_OTHER;
    }
    if (strcmp(Format, "urn:x-nmos:format:video") == 0)
    {
        return MediaType != NULL && strcmp(MediaType, "video/raw") != 0
                   ? DTNMOS_MEDIA_COMPRESSED_VIDEO
                   : DTNMOS_MEDIA_VIDEO;
    }
    if (strcmp(Format, "urn:x-nmos:format:audio") == 0)
    {
        return DTNMOS_MEDIA_AUDIO;
    }
    if (strcmp(Format, "urn:x-nmos:format:data") == 0 && MediaType != NULL &&
        strcmp(MediaType, "video/smpte291") == 0)
    {
        return DTNMOS_MEDIA_ANC;
    }
    return DTNMOS_MEDIA_OTHER;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MediaOfFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the media of a flow resource of IS-04, from its format and media type.
//
static DtNmosMedia MediaOfFlow(const NmosJson* Flow)
{
    return MediaOf(NmosJson_MemberText(Flow, "format"),
                   NmosJson_MemberText(Flow, "media_type"));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void CopyId(DtNmosId* Id, const char* Text)
{
    memset(Id, 0, sizeof(*Id));
    if (Text != NULL && strlen(Text) < sizeof(Id->Text))
    {
        strcpy(Id->Text, Text);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StoreMember -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns a copy of the text of member key of resource, owned by store, or "" when it
// has none; sets *Failed when the memory ran out.
//
static const char* StoreMember(NmosStore* Store, const NmosJson* Resource,
                               const char* Key, int* Failed)
{
    const char* Text = NmosJson_MemberText(Resource, Key);
    if (Text == NULL || Text[0] == '\0')
    {
        return "";
    }
    const char* Copy = NmosStore_Text(Store, Text, strlen(Text));
    if (Copy == NULL)
    {
        *Failed = 1;
        return "";
    }
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills sender from a sender resource of IS-04, its strings owned by store; its media
// stays for the caller.
//
static DtNmosResult ReadSender(const NmosJson* Resource, NmosStore* Store, void* Info)
{
    DtNmosSenderInfo* Sender = Info;
    memset(Sender, 0, sizeof(*Sender));
    Sender->Media = DTNMOS_MEDIA_OTHER;
    CopyId(&Sender->Id, NmosJson_MemberText(Resource, "id"));
    CopyId(&Sender->FlowId, NmosJson_MemberText(Resource, "flow_id"));
    CopyId(&Sender->DeviceId, NmosJson_MemberText(Resource, "device_id"));
    int Failed = 0;
    Sender->Label = StoreMember(Store, Resource, "label", &Failed);
    Sender->Description = StoreMember(Store, Resource, "description", &Failed);
    Sender->Transport = StoreMember(Store, Resource, "transport", &Failed);
    Sender->ManifestHref = StoreMember(Store, Resource, "manifest_href", &Failed);
    return Failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NewSenderList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns an empty list with room for capacity senders, or null when the memory ran out.
//
static DtNmosSenderList* NewSenderList(size_t Capacity)
{
    DtNmosSenderList* List = calloc(1, sizeof(*List));
    if (List != NULL && Capacity > 0 &&
        (List->Senders = calloc(Capacity, sizeof(*List->Senders))) == NULL)
    {
        free(List);
        return NULL;
    }
    return List;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Senders -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Senders(DtNmosQuery* Query, DtNmosSenderList** List)
{
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_Senders");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || List == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Senders() needs a query and a place for the list.");
    }
    *List = NULL;
    NmosPages Senders;
    NmosPages Flows;
    memset(&Senders, 0, sizeof(Senders));
    memset(&Flows, 0, sizeof(Flows));
    char* Url = MakeUrl(Query, "senders", NULL);
    if (Url == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult Result = GetPages(Query, Url, &Senders);
    if (Result == DTNMOS_OK)
    {
        Url = MakeUrl(Query, "flows", NULL);
        Result = Url == NULL ? NmosError_FailMemory() : GetPages(Query, Url, &Flows);
    }
    DtNmosSenderList* Built = NULL;
    if (Result == DTNMOS_OK)
    {
        size_t Total = 0;
        for (size_t p = 0; p < Senders.Count; ++p)
        {
            Total += Senders.Values[p]->Count;
        }
        Built = NewSenderList(Total);
        if (Built == NULL)
        {
            Result = NmosError_FailMemory();
        }
    }
    for (size_t p = 0; Result == DTNMOS_OK && p < Senders.Count; ++p)
    {
        for (size_t i = 0; Result == DTNMOS_OK && i < Senders.Values[p]->Count; ++i)
        {
            const NmosJson* Resource = &Senders.Values[p]->Items[i];
            DtNmosSenderInfo* Sender = &Built->Senders[Built->Count];
            if (ReadSender(Resource, &Built->Store, Sender) != DTNMOS_OK)
            {
                Result = NmosError_FailMemory();
                break;
            }
            ++Built->Count;
            // The media comes from the flow of the sender, which the list of flows holds.
            for (size_t f = 0; f < Flows.Count; ++f)
            {
                for (size_t j = 0; j < Flows.Values[f]->Count; ++j)
                {
                    const char* Id =
                        NmosJson_MemberText(&Flows.Values[f]->Items[j], "id");
                    if (Id != NULL && strcmp(Id, Sender->FlowId.Text) == 0)
                    {
                        Sender->Media = MediaOfFlow(&Flows.Values[f]->Items[j]);
                    }
                }
            }
        }
    }
    FreePages(&Senders);
    FreePages(&Flows);
    if (Result != DTNMOS_OK)
    {
        DtNmosSenderList_Free(Built);
        return Result;
    }
    *List = Built;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsUuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether text has the form of a UUID.
//
static int IsUuid(const char* Text)
{
    if (strlen(Text) != 36)
    {
        return 0;
    }
    for (size_t i = 0; i < 36; ++i)
    {
        const char c = Text[i];
        const int Dash = i == 8 || i == 13 || i == 18 || i == 23;
        const int Hex =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (Dash ? c != '-' : !Hex)
        {
            return 0;
        }
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AppendEncoded -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Appends text to a URL, encoded as RFC 3986 asks of a query value.
//
static void AppendEncoded(NmosBuffer* Url, const char* Text)
{
    for (const unsigned char* c = (const unsigned char*)Text; *c != '\0'; ++c)
    {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
            (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.' || *c == '~')
        {
            NmosBuffer_Append(Url, (const char*)c, 1);
        }
        else
        {
            NmosBuffer_Printf(Url, "%%%02X", (unsigned)*c);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadMedia -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the media of sender from its flow, which it fetches; a flow that cannot be fetched
// leaves DTNMOS_MEDIA_OTHER.
//
static void ReadMedia(DtNmosQuery* Query, DtNmosSenderInfo* Sender)
{
    if (Sender->FlowId.Text[0] == '\0')
    {
        return;
    }
    NmosBuffer Url;
    memset(&Url, 0, sizeof(Url));
    NmosBuffer_Printf(&Url, "%sflows/%s", Query->Base, Sender->FlowId.Text);
    NmosJson* Flow = NULL;
    if (!Url.Failed && GetJson(Query, Url.Data, &Flow, NULL) == DTNMOS_OK)
    {
        Sender->Media = MediaOfFlow(Flow);
        NmosJson_Free(Flow);
    }
    NmosBuffer_Free(&Url);
}

// A kind of resource that a search by ID or label finds: its path in the Query API, what
// a message calls one, and how one is read into what the caller gave, its strings owned
// by a store.
typedef struct NmosResourceKind
{
    const char* Path; // e.g. "senders"
    const char* Noun; // e.g. "sender"
    DtNmosResult (*Read)(const NmosJson* Resource, NmosStore* Store, void* Info);
} NmosResourceKind;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindByIdOrLabel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds the resource of kind whose ID is id_or_label when it is a UUID, or else whose
// label it is, and reads it into info, its strings owned by store.
//
static DtNmosResult FindByIdOrLabel(DtNmosQuery* Query, const NmosResourceKind* Kind,
                                    const char* IdOrLabel, NmosStore* Store, void* Info)
{
    if (IsUuid(IdOrLabel))
    {
        NmosBuffer Url;
        memset(&Url, 0, sizeof(Url));
        NmosBuffer_Printf(&Url, "%s%s/%s", Query->Base, Kind->Path, IdOrLabel);
        if (Url.Failed)
        {
            NmosBuffer_Free(&Url);
            return NmosError_FailMemory();
        }
        NmosJson* Resource = NULL;
        DtNmosResult Result = GetJson(Query, Url.Data, &Resource, NULL);
        NmosBuffer_Free(&Url);
        if (Result == DTNMOS_E_NOT_FOUND)
        {
            return NmosError_Fail(DTNMOS_E_NOT_FOUND, "The registry has no %s %s.",
                                  Kind->Noun, IdOrLabel);
        }
        if (Result != DTNMOS_OK)
        {
            return Result;
        }
        Result = Kind->Read(Resource, Store, Info);
        NmosJson_Free(Resource);
        return Result == DTNMOS_OK ? DTNMOS_OK : NmosError_FailMemory();
    }

    // A label is matched by the registry, and again here, as the Query API compares
    // values of a basic query in ways of its own.
    NmosBuffer Parameters;
    memset(&Parameters, 0, sizeof(Parameters));
    DTNMOS_APPEND_LITERAL(&Parameters, "label=");
    AppendEncoded(&Parameters, IdOrLabel);
    char* Url = Parameters.Failed ? NULL : MakeUrl(Query, Kind->Path, Parameters.Data);
    NmosBuffer_Free(&Parameters);
    if (Url == NULL)
    {
        return NmosError_FailMemory();
    }
    NmosPages Found;
    memset(&Found, 0, sizeof(Found));
    DtNmosResult Result = GetPages(Query, Url, &Found);
    const NmosJson* Match = NULL;
    size_t Matches = 0;
    NmosBuffer Ids;
    memset(&Ids, 0, sizeof(Ids));
    for (size_t p = 0; Result == DTNMOS_OK && p < Found.Count; ++p)
    {
        for (size_t i = 0; i < Found.Values[p]->Count; ++i)
        {
            const NmosJson* Resource = &Found.Values[p]->Items[i];
            const char* Label = NmosJson_MemberText(Resource, "label");
            if (Label != NULL && strcmp(Label, IdOrLabel) == 0)
            {
                const char* Id = NmosJson_MemberText(Resource, "id");
                NmosBuffer_Printf(&Ids, "%s%s", Matches == 0 ? "" : ", ",
                                  Id == NULL ? "?" : Id);
                Match = Resource;
                ++Matches;
            }
        }
    }
    if (Result == DTNMOS_OK && Matches == 0)
    {
        Result =
            NmosError_Fail(DTNMOS_E_NOT_FOUND, "The registry has no %s labelled '%s'.",
                           Kind->Noun, IdOrLabel);
    }
    else if (Result == DTNMOS_OK && Matches > 1)
    {
        Result = NmosError_Fail(DTNMOS_E_AMBIGUOUS,
                                "%zu %ss of the registry are labelled '%s': %s.", Matches,
                                Kind->Noun, IdOrLabel, Ids.Data == NULL ? "" : Ids.Data);
    }
    else if (Result == DTNMOS_OK && Kind->Read(Match, Store, Info) != DTNMOS_OK)
    {
        Result = NmosError_FailMemory();
    }
    NmosBuffer_Free(&Ids);
    FreePages(&Found);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_FindSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_FindSender(DtNmosQuery* Query, const char* IdOrLabel,
                                    DtNmosSenderList** Found)
{
    if (Found != NULL)
    {
        *Found = NULL;
    }
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_FindSender");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || IdOrLabel == NULL || IdOrLabel[0] == '\0' || Found == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_FindSender() needs an ID or a label and a place "
            "for the sender.");
    }
    DtNmosSenderList* List = NewSenderList(1);
    if (List == NULL)
    {
        return NmosError_FailMemory();
    }
    static const NmosResourceKind Senders = {"senders", "sender", ReadSender};
    const DtNmosResult Result =
        FindByIdOrLabel(Query, &Senders, IdOrLabel, &List->Store, &List->Senders[0]);
    if (Result != DTNMOS_OK)
    {
        DtNmosSenderList_Free(List);
        return Result;
    }
    List->Count = 1;
    ReadMedia(Query, &List->Senders[0]);
    *Found = List;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverList_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtNmosReceiverList_Count(const DtNmosReceiverList* List)
{
    return List == NULL ? 0 : List->Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverList_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtNmosReceiverInfo* DtNmosReceiverList_At(const DtNmosReceiverList* List,
                                                size_t Index)
{
    return List == NULL || Index >= List->Count ? NULL : &List->Receivers[Index];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosReceiverList_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosReceiverList_Free(DtNmosReceiverList* List)
{
    if (List == NULL)
    {
        return;
    }
    NmosStore_Free(&List->Store);
    free(List->Receivers);
    free(List);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NewReceiverList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns an empty list with room for capacity receivers, or null when the memory ran
// out.
//
static DtNmosReceiverList* NewReceiverList(size_t Capacity)
{
    DtNmosReceiverList* List = calloc(1, sizeof(*List));
    if (List != NULL && Capacity > 0 &&
        (List->Receivers = calloc(Capacity, sizeof(*List->Receivers))) == NULL)
    {
        free(List);
        return NULL;
    }
    return List;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills receiver from a receiver resource of IS-04, its strings owned by store: its
// media from its format and the first media type of its caps, and its subscription.
//
static DtNmosResult ReadReceiver(const NmosJson* Resource, NmosStore* Store, void* Info)
{
    DtNmosReceiverInfo* Receiver = Info;
    memset(Receiver, 0, sizeof(*Receiver));
    CopyId(&Receiver->Id, NmosJson_MemberText(Resource, "id"));
    CopyId(&Receiver->DeviceId, NmosJson_MemberText(Resource, "device_id"));
    const NmosJson* Types =
        NmosJson_Member(NmosJson_Member(Resource, "caps"), "media_types");
    const char* MediaType =
        Types != NULL && Types->Type == DTNMOS_JSON_ARRAY && Types->Count > 0
            ? NmosJson_Text(&Types->Items[0])
            : NULL;
    Receiver->Media = MediaOf(NmosJson_MemberText(Resource, "format"), MediaType);
    const NmosJson* Subscription = NmosJson_Member(Resource, "subscription");
    CopyId(&Receiver->SenderId, NmosJson_MemberText(Subscription, "sender_id"));
    const NmosJson* Active = NmosJson_Member(Subscription, "active");
    Receiver->Active = Active != NULL && Active->Type == DTNMOS_JSON_TRUE;
    int Failed = 0;
    Receiver->Label = StoreMember(Store, Resource, "label", &Failed);
    Receiver->Description = StoreMember(Store, Resource, "description", &Failed);
    Receiver->Transport = StoreMember(Store, Resource, "transport", &Failed);
    return Failed ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_Receivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_Receivers(DtNmosQuery* Query, DtNmosReceiverList** List)
{
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_Receivers");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || List == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_Receivers() needs a query and a place for the list.");
    }
    *List = NULL;
    NmosPages Receivers;
    memset(&Receivers, 0, sizeof(Receivers));
    char* Url = MakeUrl(Query, "receivers", NULL);
    if (Url == NULL)
    {
        return NmosError_FailMemory();
    }
    DtNmosResult Result = GetPages(Query, Url, &Receivers);
    DtNmosReceiverList* Built = NULL;
    if (Result == DTNMOS_OK)
    {
        size_t Total = 0;
        for (size_t p = 0; p < Receivers.Count; ++p)
        {
            Total += Receivers.Values[p]->Count;
        }
        Built = NewReceiverList(Total);
        if (Built == NULL)
        {
            Result = NmosError_FailMemory();
        }
    }
    for (size_t p = 0; Result == DTNMOS_OK && p < Receivers.Count; ++p)
    {
        for (size_t i = 0; i < Receivers.Values[p]->Count; ++i)
        {
            if (ReadReceiver(&Receivers.Values[p]->Items[i], &Built->Store,
                             &Built->Receivers[Built->Count]) != DTNMOS_OK)
            {
                Result = NmosError_FailMemory();
                break;
            }
            ++Built->Count;
        }
    }
    FreePages(&Receivers);
    if (Result != DTNMOS_OK)
    {
        DtNmosReceiverList_Free(Built);
        return Result;
    }
    *List = Built;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_FindReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_FindReceiver(DtNmosQuery* Query, const char* IdOrLabel,
                                      DtNmosReceiverList** Found)
{
    if (Found != NULL)
    {
        *Found = NULL;
    }
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_FindReceiver");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || IdOrLabel == NULL || IdOrLabel[0] == '\0' || Found == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_FindReceiver() needs an ID or a label and a place "
            "for the receiver.");
    }
    DtNmosReceiverList* List = NewReceiverList(1);
    if (List == NULL)
    {
        return NmosError_FailMemory();
    }
    static const NmosResourceKind Receivers = {"receivers", "receiver", ReadReceiver};
    const DtNmosResult Result =
        FindByIdOrLabel(Query, &Receivers, IdOrLabel, &List->Store, &List->Receivers[0]);
    if (Result != DTNMOS_OK)
    {
        DtNmosReceiverList_Free(List);
        return Result;
    }
    List->Count = 1;
    *Found = List;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosQuery_Manifest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosQuery_Manifest(DtNmosQuery* Query, const DtNmosSenderInfo* Sender,
                                DtNmosHttpResponse** Response)
{
    *Response = NULL;
    if (Sender->ManifestHref == NULL || Sender->ManifestHref[0] == '\0')
    {
        return NmosError_Fail(
            DTNMOS_E_NOT_FOUND,
            "Sender %s ('%s') has no manifest_href, so it gives no SDP.", Sender->Id.Text,
            Sender->Label == NULL ? "" : Sender->Label);
    }
    DtNmosHttpResponse* Fetched = DtNmosHttpResponse_Alloc();
    if (Fetched == NULL)
    {
        return NmosError_FailMemory();
    }
    const DtNmosResult Result = Get(Query, Sender->ManifestHref, Fetched);
    if (Result != DTNMOS_OK)
    {
        DtNmosHttpResponse_Free(Fetched);
        return Result;
    }
    *Response = Fetched;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_SenderManifest -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosQuery_SenderManifest(DtNmosQuery* Query,
                                        const DtNmosSenderInfo* Sender, char* Buffer,
                                        size_t* Size)
{
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_SenderManifest");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Query == NULL || Sender == NULL || Size == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosQuery_SenderManifest() needs a query, a sender and a size.");
    }
    DtNmosHttpResponse* Response = NULL;
    DtNmosResult Result = NmosQuery_Manifest(Query, Sender, &Response);
    if (Result == DTNMOS_OK)
    {
        size_t Length = 0;
        const char* Body = DtNmosHttpResponse_Body(Response, &Length);
        Result = NmosText_CopyText(Buffer, Size, Body, Length);
    }
    DtNmosHttpResponse_Free(Response);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosQuery_SenderSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosQuery_SenderSdp(DtNmosQuery* Query, const DtNmosSenderInfo* Sender,
                                   DtNmosSdp** Sdp)
{
    const DtNmosResult Open = NmosQuery_CheckOpen(Query, "DtNmosQuery_SenderSdp");
    if (Open != DTNMOS_OK)
    {
        return Open;
    }
    if (Sdp == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosQuery_SenderSdp() needs a place for the SDP.");
    }
    *Sdp = NULL;
    if (Query == NULL || Sender == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosQuery_SenderSdp() needs a query and a sender.");
    }
    DtNmosHttpResponse* Response = NULL;
    DtNmosResult Result = NmosQuery_Manifest(Query, Sender, &Response);
    if (Result == DTNMOS_OK)
    {
        size_t Length = 0;
        const char* Body = DtNmosHttpResponse_Body(Response, &Length);
        Result = DtNmosSdp_Parse(Body, Length, Sdp);
    }
    DtNmosHttpResponse_Free(Response);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSenderInfo_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosSenderInfo_Parse(const char* Json, size_t Length,
                                    DtNmosSenderList** Sender)
{
    if (Sender != NULL)
    {
        *Sender = NULL;
    }
    if (Json == NULL || Sender == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosSenderInfo_Parse() needs JSON and a place for the "
                              "sender.");
    }
    NmosJson* Resource = NULL;
    if (NmosJson_Parse(Json, Length, &Resource) != DTNMOS_OK ||
        Resource->Type != DTNMOS_JSON_OBJECT)
    {
        NmosJson_Free(Resource);
        return NmosError_Fail(DTNMOS_E_PARSE, "The JSON of a sender is no object.");
    }
    DtNmosSenderList* List = NewSenderList(1);
    if (List == NULL ||
        ReadSender(Resource, &List->Store, &List->Senders[0]) != DTNMOS_OK)
    {
        DtNmosSenderList_Free(List);
        NmosJson_Free(Resource);
        return NmosError_FailMemory();
    }
    NmosJson_Free(Resource);
    List->Count = 1;
    *Sender = List;
    return DTNMOS_OK;
}
