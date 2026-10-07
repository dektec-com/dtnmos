// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestCppHttp.cpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the C++ API of the HTTP client
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <memory>
#include <stdexcept>
#include <string>

#include "NmosTest.h"
#include "dtnmos_http.hpp"

using ResponsePtr = std::unique_ptr<DtNmosHttpResponse, DtNmos::Detail::HttpResponseFree>;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppHttpConverts -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A request with a body that holds a null converts to the C API and back unchanged. A
// request without a body gives the C API no body and no content type. A response with
// two headers and a body fills a C response, which converts back unchanged; its headers
// are found by name in any case.
//
NMOS_TEST(CppHttpConverts)
{
    DtNmos::HttpRequest Request;
    Request.Method = "PATCH";
    Request.Url = "/x-nmos/connection/v1.1/single/senders/";
    Request.ContentType = "application/json";
    Request.Body = std::string("{\"a\": 1}\0tail", 13);
    Request.TimeoutMs = 2500;
    const DtNmosHttpRequest Native = DtNmos::Detail::ToNative(Request);
    NMOS_ASSERT_EQ(Native.BodyLength, 13);
    NMOS_ASSERT(DtNmos::Detail::FromNative(Native) == Request);

    const DtNmos::HttpRequest Get{"GET", "http://registry.test/", "", "", 0};
    const DtNmosHttpRequest NativeGet = DtNmos::Detail::ToNative(Get);
    NMOS_ASSERT(NativeGet.Body == nullptr);
    NMOS_ASSERT(NativeGet.ContentType == nullptr);

    DtNmos::HttpResponse Response;
    Response.Status = 201;
    Response.Headers = {{"Location", "/x-nmos/registration/v1.3/resource/nodes/1"},
                        {"X-Other", "2"}};
    Response.ContentType = "application/json";
    Response.Body = "{}";
    const ResponsePtr Filled(DtNmosHttpResponse_Alloc());
    NMOS_ASSERT(Filled != nullptr);
    NMOS_ASSERT(DtNmos::Detail::ToNative(Response, Filled.get()).has_value());
    const DtNmos::HttpResponse Back = DtNmos::Detail::FromNative(Filled.get());
    NMOS_ASSERT_EQ(Back.Status, 201);
    NMOS_ASSERT(Back.ContentType == "application/json");
    NMOS_ASSERT(Back.Body == "{}");
    NMOS_ASSERT(Back.FindHeader("location") != nullptr);
    NMOS_ASSERT(*Back.FindHeader("LOCATION") ==
                "/x-nmos/registration/v1.3/resource/nodes/1");
    NMOS_ASSERT(Back.FindHeader("X-Missing") == nullptr);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CallTrampoline -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Calls Http as the C library calls it: with a GET of the registry, which fills Response.
//
static DtNmosResult CallTrampoline(const DtNmos::HttpFunction& Http,
                                   DtNmosHttpResponse* Response)
{
    const DtNmos::HttpRequest Get{"GET", "http://registry.test/x-nmos", "", "", 1000};
    const DtNmosHttpRequest Native = DtNmos::Detail::ToNative(Get);
    const DtNmos::Detail::NativeHttp C = DtNmos::Detail::ToNative(Http);
    return C.Function(C.User, &Native, Response);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppHttpTrampoline -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C library calls an HttpFunction through its trampoline. The function gets the
// request, and its answer fills the C response. An Error it returns is the result and
// message of the call. An Error with Result::Ok is no failure, and the call fails with
// Result::Http. With exceptions, an exception it throws fails the call with Result::Http
// and its what().
//
NMOS_TEST(CppHttpTrampoline)
{
    std::string Asked;
    const DtNmos::HttpFunction Answers =
        [&](const DtNmos::HttpRequest& Request) -> DtNmos::Expected<DtNmos::HttpResponse>
    {
        Asked = Request.Method + " " + Request.Url;
        return DtNmos::HttpResponse{200, {}, "application/json", "[]"};
    };
    const ResponsePtr Response(DtNmosHttpResponse_Alloc());
    NMOS_ASSERT_EQ(CallTrampoline(Answers, Response.get()), DTNMOS_OK);
    NMOS_ASSERT(Asked == "GET http://registry.test/x-nmos");
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response.get()), 200);
    NMOS_ASSERT_STR(DtNmosHttpResponse_Body(Response.get(), nullptr), "[]");

    const DtNmos::HttpFunction TimesOut =
        [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    { return DtNmos::Unexpected(DtNmos::Error{DtNmos::Result::Timeout, "no answer"}); };
    const ResponsePtr Empty(DtNmosHttpResponse_Alloc());
    NMOS_ASSERT_EQ(CallTrampoline(TimesOut, Empty.get()), DTNMOS_E_TIMEOUT);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), "no answer");

    const DtNmos::HttpFunction FailsWithOk =
        [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    { return DtNmos::Unexpected(DtNmos::Error{DtNmos::Result::Ok, "not a failure"}); };
    NMOS_ASSERT_EQ(CallTrampoline(FailsWithOk, Empty.get()), DTNMOS_E_HTTP);
#if defined(__cpp_exceptions)
    const DtNmos::HttpFunction Throws =
        [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    { throw std::runtime_error("the socket broke"); };
    NMOS_ASSERT_EQ(CallTrampoline(Throws, Empty.get()), DTNMOS_E_HTTP);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), "the socket broke");
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppHttpCurl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C API gets libcurl's own function for CurlHttp, the trampoline for any other
// function, and no function for an empty one. CurlHttp fails for a port where nobody
// listens. In a library without libcurl it fails with Result::State.
//
NMOS_TEST(CppHttpCurl)
{
    NMOS_ASSERT(
        DtNmos::Detail::ToNative(DtNmos::HttpFunction(DtNmos::CurlHttp)).Function ==
        DtNmos_CurlHttp);
    const DtNmos::HttpFunction Other =
        [](const DtNmos::HttpRequest&) -> DtNmos::Expected<DtNmos::HttpResponse>
    { return DtNmos::HttpResponse{}; };
    NMOS_ASSERT(DtNmos::Detail::ToNative(Other).Function ==
                DtNmos::Detail::HttpTrampoline);
    NMOS_ASSERT(DtNmos::Detail::ToNative(DtNmos::HttpFunction()).Function == nullptr);

    const DtNmos::Expected<DtNmos::HttpResponse> Answer =
        DtNmos::CurlHttp(DtNmos::HttpRequest{"GET", "http://127.0.0.1:9/", "", "", 1000});
    NMOS_ASSERT(!Answer.has_value());
    if (!DtNmos::HasCurl())
    {
        NMOS_ASSERT(Answer.error().Code == DtNmos::Result::State);
    }
}

NMOS_TEST_MAIN("CppHttp", NMOS_RUN(CppHttpConverts), NMOS_RUN(CppHttpTrampoline),
               NMOS_RUN(CppHttpCurl))
