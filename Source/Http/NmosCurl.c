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
typedef struct transfer
{
    DtNmosHttpResponse* response;
    int failed;
} transfer;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- receive_body -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t receive_body(char* data, size_t size, size_t count, void* user)
{
    transfer* t = user;
    const size_t length = size * count;
    if (DtNmosHttpResponse_AppendBody(t->response, data, length) != DTNMOS_OK)
    {
        t->failed = 1;
        return 0;
    }
    return length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- receive_header -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t receive_header(char* data, size_t size, size_t count, void* user)
{
    transfer* t = user;
    const size_t length = size * count;
    dtnmos_span line = {data, length};
    while (line.length > 0 &&
           (line.data[line.length - 1] == '\n' || line.data[line.length - 1] == '\r'))
    {
        --line.length;
    }
    // A new status line starts the headers of the next response, after a redirect.
    if (dtnmos_span_starts_with(line, "HTTP/"))
    {
        return length;
    }
    dtnmos_span name;
    const dtnmos_span value = dtnmos_span_split(line, ':', &name);
    if (value.data == NULL || name.length == 0 || name.length > 255 ||
        value.length > 8191)
    {
        return length;
    }
    char name_text[256];
    char value_text[8192];
    const dtnmos_span trimmed = dtnmos_span_trim(value);
    memcpy(name_text, name.data, name.length);
    name_text[name.length] = '\0';
    memcpy(value_text, trimmed.data, trimmed.length);
    value_text[trimmed.length] = '\0';
    if (DtNmosHttpResponse_AddHeader(t->response, name_text, value_text) != DTNMOS_OK)
    {
        t->failed = 1;
        return 0;
    }
    return length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmos_CurlHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmos_CurlHttp(void* user, const DtNmosHttpRequest* request,
                             DtNmosHttpResponse* response, DtNmosError* error)
{
    (void)user;
    if (request == NULL || request->Url == NULL || request->Method == NULL ||
        response == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmos_CurlHttp() needs a request with a method and a URL.");
    }
    CURL* curl = curl_easy_init();
    if (curl == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INTERNAL,
                           "libcurl could not create a handle.");
    }
    transfer t = {response, 0};
    struct curl_slist* headers = NULL;
    char content_type[256];
    if (request->ContentType != NULL)
    {
        snprintf(content_type, sizeof(content_type), "Content-Type: %s",
                 request->ContentType);
        headers = curl_slist_append(headers, content_type);
    }
    headers =
        curl_slist_append(headers, "Accept: application/json, application/sdp, */*");
    curl_easy_setopt(curl, CURLOPT_URL, request->Url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request->Method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                     (long)(request->TimeoutMs == 0 ? 5000 : request->TimeoutMs));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &t);
    if (request->Body != NULL)
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->Body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         (curl_off_t)request->BodyLength);
    }
    else if (strcmp(request->Method, "GET") != 0 &&
             strcmp(request->Method, "DELETE") != 0)
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    }
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (t.failed)
    {
        return dtnmos_fail_memory(error);
    }
    if (code == CURLE_OPERATION_TIMEDOUT)
    {
        return dtnmos_fail(
            error, DTNMOS_E_TIMEOUT, "%s %s got no answer within %u ms.", request->Method,
            request->Url,
            (unsigned)(request->TimeoutMs == 0 ? 5000 : request->TimeoutMs));
    }
    if (code != CURLE_OK)
    {
        return dtnmos_fail(error, DTNMOS_E_HTTP, "%s %s failed: %s.", request->Method,
                           request->Url, curl_easy_strerror(code));
    }
    DtNmosHttpResponse_SetStatus(response, (int)status);
    // An answer without body still has one, so that its text ends in a null character.
    DtNmosHttpResponse_AppendBody(response, "", 0);
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
DtNmosResult DtNmos_CurlHttp(void* user, const DtNmosHttpRequest* request,
                             DtNmosHttpResponse* response, DtNmosError* error)
{
    (void)user;
    (void)request;
    (void)response;
    return dtnmos_fail(
        error, DTNMOS_E_STATE,
        "dtnmos was built without libcurl, so it has no HTTP transport of its "
        "own; pass one.");
}

#endif
