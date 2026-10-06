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

using ResponsePtr = std::unique_ptr<DtNmosHttpResponse, dtnmos::detail::HttpResponseFree>;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppHttpConverts -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A request with a body that holds a null converts to the C API and back to itself, and
// one without a body gives the C API no body and no content type; a response with two
// headers and a body fills a C response that converts back to itself.
//
NMOS_TEST(CppHttpConverts)
{
    dtnmos::HttpRequest Request;
    Request.Method = "PATCH";
    Request.Url = "/x-nmos/connection/v1.1/single/senders/";
    Request.ContentType = "application/json";
    Request.Body = std::string("{\"a\": 1}\0tail", 13);
    Request.TimeoutMs = 2500;
    const DtNmosHttpRequest Native = dtnmos::detail::ToNative(Request);
    NMOS_ASSERT_EQ(Native.BodyLength, 13);
    NMOS_ASSERT(dtnmos::detail::FromNative(Native) == Request);

    const dtnmos::HttpRequest Get{"GET", "http://registry.test/", "", "", 0};
    const DtNmosHttpRequest NativeGet = dtnmos::detail::ToNative(Get);
    NMOS_ASSERT(NativeGet.Body == nullptr);
    NMOS_ASSERT(NativeGet.ContentType == nullptr);

    dtnmos::HttpResponse Response;
    Response.Status = 201;
    Response.Headers = {{"Location", "/x-nmos/registration/v1.3/resource/nodes/1"},
                        {"X-Other", "2"}};
    Response.ContentType = "application/json";
    Response.Body = "{}";
    const ResponsePtr Filled(DtNmosHttpResponse_Alloc());
    NMOS_ASSERT(Filled != nullptr);
    NMOS_ASSERT(dtnmos::detail::ToNative(Response, Filled.get()).has_value());
    const dtnmos::HttpResponse Back = dtnmos::detail::FromNative(Filled.get());
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
// Calls Http as the C library calls it, with a GET of the registry, into Response.
//
static DtNmosResult CallTrampoline(const dtnmos::HttpFunction& Http,
                                   DtNmosHttpResponse* Response)
{
    const dtnmos::HttpRequest Get{"GET", "http://registry.test/x-nmos", "", "", 1000};
    const DtNmosHttpRequest Native = dtnmos::detail::ToNative(Get);
    const dtnmos::detail::NativeHttp C = dtnmos::detail::ToNative(Http);
    return C.Function(C.User, &Native, Response);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppHttpTrampoline -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C library's call of an HttpFunction gives it the request, and fills the C
// response with its answer. An Error it returns is the call's result and message; one
// with Result::Ok, no failure, becomes Result::Http. With exceptions, one it throws is
// Result::Http with its what().
//
NMOS_TEST(CppHttpTrampoline)
{
    std::string Asked;
    const dtnmos::HttpFunction Answers =
        [&](const dtnmos::HttpRequest& Request) -> dtnmos::Expected<dtnmos::HttpResponse>
    {
        Asked = Request.Method + " " + Request.Url;
        return dtnmos::HttpResponse{200, {}, "application/json", "[]"};
    };
    const ResponsePtr Response(DtNmosHttpResponse_Alloc());
    NMOS_ASSERT_EQ(CallTrampoline(Answers, Response.get()), DTNMOS_OK);
    NMOS_ASSERT(Asked == "GET http://registry.test/x-nmos");
    NMOS_ASSERT_EQ(DtNmosHttpResponse_Status(Response.get()), 200);
    NMOS_ASSERT_STR(DtNmosHttpResponse_Body(Response.get(), nullptr), "[]");

    const dtnmos::HttpFunction TimesOut =
        [](const dtnmos::HttpRequest&) -> dtnmos::Expected<dtnmos::HttpResponse>
    { return std::unexpected(dtnmos::Error{dtnmos::Result::Timeout, "no answer"}); };
    const ResponsePtr Empty(DtNmosHttpResponse_Alloc());
    NMOS_ASSERT_EQ(CallTrampoline(TimesOut, Empty.get()), DTNMOS_E_TIMEOUT);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), "no answer");

    const dtnmos::HttpFunction FailsWithOk =
        [](const dtnmos::HttpRequest&) -> dtnmos::Expected<dtnmos::HttpResponse>
    { return std::unexpected(dtnmos::Error{dtnmos::Result::Ok, "not a failure"}); };
    NMOS_ASSERT_EQ(CallTrampoline(FailsWithOk, Empty.get()), DTNMOS_E_HTTP);
#if defined(__cpp_exceptions)
    const dtnmos::HttpFunction Throws =
        [](const dtnmos::HttpRequest&) -> dtnmos::Expected<dtnmos::HttpResponse>
    { throw std::runtime_error("the socket broke"); };
    NMOS_ASSERT_EQ(CallTrampoline(Throws, Empty.get()), DTNMOS_E_HTTP);
    NMOS_ASSERT_STR(DtNmos_GetLastError(), "the socket broke");
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CppHttpCurl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C API is given libcurl's own function for CurlHttp, the trampoline for another
// function, and none for an empty one. CurlHttp fails for a port nobody listens on, and
// in a library without libcurl with Result::State.
//
NMOS_TEST(CppHttpCurl)
{
    NMOS_ASSERT(
        dtnmos::detail::ToNative(dtnmos::HttpFunction(dtnmos::CurlHttp)).Function ==
        DtNmos_CurlHttp);
    const dtnmos::HttpFunction Other =
        [](const dtnmos::HttpRequest&) -> dtnmos::Expected<dtnmos::HttpResponse>
    { return dtnmos::HttpResponse{}; };
    NMOS_ASSERT(dtnmos::detail::ToNative(Other).Function ==
                dtnmos::detail::HttpTrampoline);
    NMOS_ASSERT(dtnmos::detail::ToNative(dtnmos::HttpFunction()).Function == nullptr);

    const dtnmos::Expected<dtnmos::HttpResponse> Answer =
        dtnmos::CurlHttp(dtnmos::HttpRequest{"GET", "http://127.0.0.1:9/", "", "", 1000});
    NMOS_ASSERT(!Answer.has_value());
    if (!dtnmos::HasCurl())
    {
        NMOS_ASSERT(Answer.error().Code == dtnmos::Result::State);
    }
}

NMOS_TEST_MAIN("CppHttp", NMOS_RUN(CppHttpConverts), NMOS_RUN(CppHttpTrampoline),
               NMOS_RUN(CppHttpCurl))
