// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# NmosCurl.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The HTTP transport on libcurl, when the library is built with DTNMOS_WITH_CURL
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "NmosInternal.h"
#include "dtnmos_http.h"

#ifdef DTNMOS_WITH_CURL

    #include <curl/curl.h>
    #include <stdio.h>
    #include <string.h>

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasCurl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtNmos_HasCurl(void)
{
    return 1;
}

// What the callbacks of libcurl fill.
typedef struct NmosTransfer
{
    DtNmosHttpResponse* Response;
    int Failed;
} NmosTransfer;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiveBody -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static size_t ReceiveBody(char* Data, size_t Size, size_t Count, void* User)
{
    NmosTransfer* t = User;
    const size_t Length = Size * Count;
    if (DtNmosHttpResponse_AppendBody(t->Response, Data, Length) != DTNMOS_OK)
    {
        t->Failed = 1;
        return 0;
    }
    return Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiveHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static size_t ReceiveHeader(char* Data, size_t Size, size_t Count, void* User)
{
    NmosTransfer* t = User;
    const size_t Length = Size * Count;
    NmosSpan Line = {Data, Length};
    while (Line.Length > 0 &&
           (Line.Data[Line.Length - 1] == '\n' || Line.Data[Line.Length - 1] == '\r'))
    {
        --Line.Length;
    }
    // A new status line starts the headers of the next response, after a redirect.
    if (NmosSpan_StartsWith(Line, "HTTP/"))
    {
        return Length;
    }
    NmosSpan Name;
    const NmosSpan Value = NmosSpan_Split(Line, ':', &Name);
    if (Value.Data == NULL || Name.Length == 0 || Name.Length > 255 ||
        Value.Length > 8191)
    {
        return Length;
    }
    char NameText[256];
    char ValueText[8192];
    const NmosSpan Trimmed = NmosSpan_Trim(Value);
    memcpy(NameText, Name.Data, Name.Length);
    NameText[Name.Length] = '\0';
    memcpy(ValueText, Trimmed.Data, Trimmed.Length);
    ValueText[Trimmed.Length] = '\0';
    if (DtNmosHttpResponse_AddHeader(t->Response, NameText, ValueText) != DTNMOS_OK)
    {
        t->Failed = 1;
        return 0;
    }
    return Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_CurlHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_CurlHttp(void* User, const DtNmosHttpRequest* Request,
                             DtNmosHttpResponse* Response)
{
    (void)User;
    if (Request == NULL || Request->Url == NULL || Request->Method == NULL ||
        Response == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmos_CurlHttp() needs a request with a method and a URL.");
    }
    const DtNmosResult Sized =
        DTNMOS_CHECK_SIZE(Request, DtNmosHttpRequest, sizeof(DtNmosHttpRequest));
    if (Sized != DTNMOS_OK)
    {
        return Sized;
    }
    CURL* Curl = curl_easy_init();
    if (Curl == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INTERNAL, "libcurl could not create a handle.");
    }
    NmosTransfer t = {Response, 0};
    struct curl_slist* Headers = NULL;
    char ContentType[256];
    if (Request->ContentType != NULL)
    {
        snprintf(ContentType, sizeof(ContentType), "Content-Type: %s",
                 Request->ContentType);
        Headers = curl_slist_append(Headers, ContentType);
    }
    Headers =
        curl_slist_append(Headers, "Accept: application/json, application/sdp, */*");
    curl_easy_setopt(Curl, CURLOPT_URL, Request->Url);
    curl_easy_setopt(Curl, CURLOPT_CUSTOMREQUEST, Request->Method);
    curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, Headers);
    curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(Curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(Curl, CURLOPT_TIMEOUT_MS,
                     (long)(Request->TimeoutMs == 0 ? 5000 : Request->TimeoutMs));
    curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, ReceiveBody);
    curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(Curl, CURLOPT_HEADERFUNCTION, ReceiveHeader);
    curl_easy_setopt(Curl, CURLOPT_HEADERDATA, &t);
    if (Request->Body != NULL)
    {
        curl_easy_setopt(Curl, CURLOPT_POSTFIELDS, Request->Body);
        curl_easy_setopt(Curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         (curl_off_t)Request->BodyLength);
    }
    else if (strcmp(Request->Method, "GET") != 0 &&
             strcmp(Request->Method, "DELETE") != 0)
    {
        curl_easy_setopt(Curl, CURLOPT_POSTFIELDS, "");
    }
    const CURLcode Code = curl_easy_perform(Curl);
    long Status = 0;
    curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &Status);
    curl_slist_free_all(Headers);
    curl_easy_cleanup(Curl);
    if (t.Failed)
    {
        return NmosError_FailMemory();
    }
    if (Code == CURLE_OPERATION_TIMEDOUT)
    {
        return NmosError_Fail(
            DTNMOS_E_TIMEOUT, "%s %s got no answer within %u ms.", Request->Method,
            Request->Url,
            (unsigned)(Request->TimeoutMs == 0 ? 5000 : Request->TimeoutMs));
    }
    if (Code != CURLE_OK)
    {
        return NmosError_Fail(DTNMOS_E_HTTP, "%s %s failed: %s.", Request->Method,
                              Request->Url, curl_easy_strerror(Code));
    }
    DtNmosHttpResponse_SetStatus(Response, (int)Status);
    // An answer without body still has one, so that its text ends in a null character.
    DtNmosHttpResponse_AppendBody(Response, "", 0);
    return DTNMOS_OK;
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_HasCurl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtNmos_HasCurl(void)
{
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_CurlHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_CurlHttp(void* User, const DtNmosHttpRequest* Request,
                             DtNmosHttpResponse* Response)
{
    (void)User;
    (void)Request;
    (void)Response;
    return NmosError_Fail(
        DTNMOS_E_STATE,
        "dtnmos was built without libcurl, so it has no HTTP transport of its "
        "own; pass one.");
}

#endif
