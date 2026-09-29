// #*#*#*#*#*#*#*#*#*#*#*#*# subscription-test.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Tests of the subscriptions of a registry, and of the WebSocket on libcurl
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos/subscription.h"

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "internal.h"
#include "json.h"
#include "platform.h"
#include "tests.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
typedef SOCKET test_socket;
    #define TEST_NO_SOCKET INVALID_SOCKET
    #define test_close_socket closesocket
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
typedef int test_socket;
    #define TEST_NO_SOCKET (-1)
    #define test_close_socket close
#endif

#define BASE "http://registry.test/x-nmos/query/v1.3/"
#define WS_HREF "ws://registry.test/x-nmos/query/v1.3/subscriptions/1/ws"
#define CAMERA_ID "11111111-1111-4111-8111-111111111111"
#define MIC_ID "22222222-2222-4222-8222-222222222222"
#define CAMERA                                                                           \
    "{\"id\": \"" CAMERA_ID "\", \"label\": \"camera 1\", \"version\": \"1:0\"}"
#define CAMERA_2                                                                         \
    "{\"id\": \"" CAMERA_ID "\", \"label\": \"camera 1\", \"version\": \"2:0\"}"
#define MIC "{\"id\": \"" MIC_ID "\", \"label\": \"mic\", \"flow_id\": null}"

// A grain of the Query API with the changes in data.
#define GRAIN(data)                                                                      \
    "{\"grain_type\": \"event\", \"source_id\": \"x\", \"flow_id\": \"y\", "             \
    "\"grain\": {\"type\": \"urn:x-nmos:format:data.event\", \"topic\": \"/senders/\", " \
    "\"data\": [" data "]}}"

// A registry and a WebSocket that the test answers for.
typedef struct fake_subscription
{
    int status;          // what the registry answers the POST
    const char* answer;  // and with what
    char body[512];      // of the POST
    char url[256];       // of the POST
    char connected[256]; // the URL the WebSocket connected to
    int connect_fails;
    const char* const* messages; // what the WebSocket gives, then it closes
    size_t count;
    size_t next;
    int closed;
} fake_subscription;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_http -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static dtnmos_result fake_http(void* user, const dtnmos_http_request* request,
                               dtnmos_http_response* response, dtnmos_error* error)
{
    (void)error;
    fake_subscription* fake = user;
    CHECK_STR(request->method, "POST");
    CHECK_STR(request->content_type, "application/json");
    snprintf(fake->url, sizeof(fake->url), "%s", request->url);
    snprintf(fake->body, sizeof(fake->body), "%.*s", (int)request->body_length,
             request->body);
    dtnmos_http_response_set_status(response, fake->status);
    dtnmos_http_response_set_body(response, "application/json", fake->answer,
                                  strlen(fake->answer));
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static dtnmos_result fake_connect(void* user, const char* url, uint32_t timeout_ms,
                                  void** connection, dtnmos_error* error)
{
    fake_subscription* fake = user;
    CHECK_EQ(timeout_ms, 2000);
    snprintf(fake->connected, sizeof(fake->connected), "%s", url);
    if (fake->connect_fails)
    {
        return dtnmos_fail(error, DTNMOS_E_NETWORK, "connection refused");
    }
    *connection = fake;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static dtnmos_result fake_receive(void* user, void* connection, uint32_t timeout_ms,
                                  dtnmos_string* message, dtnmos_error* error)
{
    (void)connection;
    fake_subscription* fake = user;
    if (fake->next < fake->count)
    {
        const char* text = fake->messages[fake->next++];
        if (text == NULL)
        {
            return dtnmos_fail(error, DTNMOS_E_TIMEOUT, "No message came within %u ms.",
                               (unsigned)timeout_ms);
        }
        return dtnmos_string_set_text(message, text);
    }
    return dtnmos_fail(error, DTNMOS_E_NETWORK, "The server closed the WebSocket.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void fake_close(void* user, void* connection)
{
    (void)connection;
    ((fake_subscription*)user)->closed = 1;
}

// What the changes were, as "<kind> <id> <label before> <label after>".
typedef struct recorded_changes
{
    char lines[8][160];
    size_t count;
} recorded_changes;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- record_change -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void record_change(void* user, const dtnmos_change* change)
{
    recorded_changes* recorded = user;
    dtnmos_sender_info before = {0};
    dtnmos_sender_info after = {0};
    if (change->pre != NULL)
    {
        CHECK(dtnmos_sender_info_parse(change->pre, change->pre_length, &before, NULL) ==
              DTNMOS_OK);
    }
    if (change->post != NULL)
    {
        CHECK(dtnmos_sender_info_parse(change->post, change->post_length, &after, NULL) ==
              DTNMOS_OK);
        CHECK_STR(after.id.text, change->id);
    }
    if (recorded->count < 8)
    {
        snprintf(recorded->lines[recorded->count++], sizeof(recorded->lines[0]),
                 "%s %s %s %s", dtnmos_change_kind_name(change->kind), change->id,
                 change->pre != NULL ? dtnmos_string_get(&before.label) : "-",
                 change->post != NULL ? dtnmos_string_get(&after.label) : "-");
    }
    dtnmos_sender_info_clear(&before);
    dtnmos_sender_info_clear(&after);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- subscribe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Creates a query of the registry of fake and a subscription to its senders through
// the WebSocket of fake; returns the result of creating it.
//
static dtnmos_result subscribe(fake_subscription* fake, recorded_changes* recorded,
                               dtnmos_query** query, dtnmos_subscription** subscription,
                               dtnmos_error* error)
{
    static dtnmos_websocket_transport websocket;
    websocket.size = sizeof(websocket);
    websocket.user = fake;
    websocket.connect = fake_connect;
    websocket.receive = fake_receive;
    websocket.close = fake_close;
    dtnmos_query_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.registry_url = "http://registry.test";
    config.http = fake_http;
    config.http_user = fake;
    config.timeout_ms = 2000;
    *query = NULL;
    *subscription = NULL;
    if (dtnmos_query_create(&config, query, error) != DTNMOS_OK)
    {
        return DTNMOS_E_INTERNAL;
    }
    dtnmos_subscription_config wanted;
    memset(&wanted, 0, sizeof(wanted));
    wanted.size = sizeof(wanted);
    wanted.resource_path = "/senders";
    wanted.websocket = &websocket;
    wanted.on_change = record_change;
    wanted.on_change_user = recorded;
    return dtnmos_subscription_create(*query, &wanted, subscription, error);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- subscription_reports_what_changes -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void subscription_reports_what_changes(void)
{
    static const char* const messages[] = {
        // The first message: every sender as it is.
        GRAIN("{\"path\": \"" CAMERA_ID "\", \"pre\": " CAMERA ", \"post\": " CAMERA "}, "
              "{\"path\": \"" MIC_ID "\", \"pre\": " MIC ", \"post\": " MIC "}"),
        NULL, // a poll in which nothing came
        GRAIN("{\"path\": \"" CAMERA_ID "\", \"pre\": " CAMERA ", \"post\": " CAMERA_2
              "}"),
        GRAIN("{\"path\": \"" MIC_ID "\", \"pre\": " MIC "}, {\"path\": \"" MIC_ID
              "\", \"post\": " MIC "}"),
    };
    fake_subscription fake;
    memset(&fake, 0, sizeof(fake));
    fake.status = 201;
    fake.answer = "{\"id\": \"1\", \"ws_href\": \"" WS_HREF "\"}";
    fake.messages = messages;
    fake.count = sizeof(messages) / sizeof(messages[0]);
    recorded_changes recorded;
    memset(&recorded, 0, sizeof(recorded));
    dtnmos_query* query = NULL;
    dtnmos_subscription* subscription = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    const dtnmos_result created =
        subscribe(&fake, &recorded, &query, &subscription, &error);
    if (created != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    REQUIRE(created == DTNMOS_OK);
    CHECK_STR(fake.url, BASE "subscriptions");
    CHECK_STR(fake.connected, WS_HREF);
    CHECK_STR(dtnmos_subscription_url(subscription), WS_HREF);
    dtnmos_json* body = NULL;
    REQUIRE(dtnmos_json_parse(fake.body, strlen(fake.body), &body, NULL) == DTNMOS_OK);
    CHECK_STR(dtnmos_json_member_text(body, "resource_path"), "/senders");
    CHECK_EQ(dtnmos_json_member(body, "max_update_rate_ms")->number, 100);
    CHECK_EQ(dtnmos_json_member(body, "persist")->type, DTNMOS_JSON_FALSE);
    CHECK_EQ(dtnmos_json_member(body, "secure")->type, DTNMOS_JSON_FALSE);
    CHECK_EQ(dtnmos_json_member(body, "params")->type, DTNMOS_JSON_OBJECT);
    dtnmos_json_free(body);

    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_OK);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_E_TIMEOUT);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_OK);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_OK);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_E_NETWORK);
    CHECK(strstr(error.message, "closed") != NULL);
    const char* const expected[] = {
        "present " CAMERA_ID " camera 1 camera 1",
        "present " MIC_ID " mic mic",
        "modified " CAMERA_ID " camera 1 camera 1",
        "removed " MIC_ID " mic -",
        "added " MIC_ID " - mic",
    };
    REQUIRE(recorded.count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < recorded.count; ++i)
    {
        CHECK_STR(recorded.lines[i], expected[i]);
    }
    dtnmos_subscription_destroy(subscription);
    CHECK_EQ(fake.closed, 1);
    dtnmos_query_destroy(query);
}

// .-.-.-.-.-.-.-.-.-.-.-.- subscription_names_what_went_wrong -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void subscription_names_what_went_wrong(void)
{
    static const char* const messages[] = {"not json", "{\"grain\": {}}",
                                           GRAIN("{\"path\": \"" MIC_ID "\"}")};
    fake_subscription fake;
    memset(&fake, 0, sizeof(fake));
    recorded_changes recorded;
    memset(&recorded, 0, sizeof(recorded));
    dtnmos_query* query = NULL;
    dtnmos_subscription* subscription = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};

    // A registry that refuses, and one that names no WebSocket.
    fake.status = 400;
    fake.answer = "{\"code\": 400, \"error\": \"bad path\"}";
    CHECK(subscribe(&fake, &recorded, &query, &subscription, &error) == DTNMOS_E_HTTP);
    CHECK(strstr(error.message, "answered the subscription to /senders with 400") !=
          NULL);
    CHECK(subscription == NULL);
    dtnmos_query_destroy(query);
    fake.status = 200;
    fake.answer = "{\"id\": \"1\"}";
    CHECK(subscribe(&fake, &recorded, &query, &subscription, &error) == DTNMOS_E_PARSE);
    CHECK(strstr(error.message, "without the ws_href") != NULL);
    dtnmos_query_destroy(query);
    // A WebSocket that cannot be opened.
    fake.answer = "{\"id\": \"1\", \"ws_href\": \"" WS_HREF "\"}";
    fake.connect_fails = 1;
    CHECK(subscribe(&fake, &recorded, &query, &subscription, &error) == DTNMOS_E_NETWORK);
    CHECK(strstr(error.message, "connection refused") != NULL);
    dtnmos_query_destroy(query);

    // Messages that are no grain fail the poll and not the subscription, and an item
    // with neither pre nor post is no change.
    fake.connect_fails = 0;
    fake.messages = messages;
    fake.count = sizeof(messages) / sizeof(messages[0]);
    REQUIRE(subscribe(&fake, &recorded, &query, &subscription, &error) == DTNMOS_OK);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_E_PARSE);
    CHECK(strstr(error.message, "is no grain with data") != NULL);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_E_PARSE);
    CHECK(dtnmos_subscription_poll(subscription, 50, &error) == DTNMOS_OK);
    CHECK_EQ(recorded.count, 0);
    dtnmos_subscription_destroy(subscription);
    dtnmos_query_destroy(query);

    // A subscription needs a path and a function.
    dtnmos_subscription_config config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.resource_path = "senders";
    CHECK(dtnmos_subscription_create(NULL, &config, &subscription, &error) ==
          DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- json_writes_what_it_reads_back -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void json_writes_what_it_reads_back(void)
{
    const char* text = "{\"a\": [1, -2.5, 1e3, true, false, null, \"x\\\"y\\n\"], "
                       "\"b\": {}, \"c\": []}";
    dtnmos_json* json = NULL;
    REQUIRE(dtnmos_json_parse(text, strlen(text), &json, NULL) == DTNMOS_OK);
    dtnmos_buffer written;
    memset(&written, 0, sizeof(written));
    dtnmos_json_write(&written, json);
    REQUIRE(!written.failed);
    CHECK_STR(written.data,
              "{\"a\":[1,-2.5,1000,true,false,null,\"x\\\"y\\n\"],\"b\":{},\"c\":[]}");
    dtnmos_json_free(json);
    dtnmos_buffer_free(&written);
}

// A server of one WebSocket that the test runs on a thread: it accepts one client,
// answers its handshake, and sends what the test of the WebSocket on libcurl reads.
typedef struct test_server
{
    test_socket listener;
    uint16_t port;
    int handshake_ok;
} test_server;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- base64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void base64(const uint8_t* data, size_t length, char* text)
{
    static const char digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3)
    {
        const uint32_t group = (uint32_t)data[i] << 16 |
                               (i + 1 < length ? (uint32_t)data[i + 1] << 8 : 0) |
                               (i + 2 < length ? (uint32_t)data[i + 2] : 0);
        text[out++] = digits[group >> 18 & 63];
        text[out++] = digits[group >> 12 & 63];
        text[out++] = i + 1 < length ? digits[group >> 6 & 63] : '=';
        text[out++] = i + 2 < length ? digits[group & 63] : '=';
    }
    text[out] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- send_frame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sends a frame of a server, unmasked: opcode, whether it is the last of its message,
// and its payload.
//
static void send_frame(test_socket client, int opcode, int last, const char* payload,
                       size_t length)
{
    uint8_t header[10];
    size_t size = 2;
    header[0] = (uint8_t)((last ? 0x80 : 0) | opcode);
    if (length < 126)
    {
        header[1] = (uint8_t)length;
    }
    else if (length < 65536)
    {
        header[1] = 126;
        header[2] = (uint8_t)(length >> 8);
        header[3] = (uint8_t)length;
        size = 4;
    }
    else
    {
        header[1] = 127;
        for (int i = 0; i < 8; ++i)
        {
            header[2 + i] = (uint8_t)((uint64_t)length >> (56 - 8 * i));
        }
        size = 10;
    }
    send(client, (const char*)header, (int)size, 0);
    size_t sent = 0;
    while (sent < length)
    {
        const int now = (int)send(client, payload + sent, (int)(length - sent), 0);
        if (now <= 0)
        {
            return;
        }
        sent += (size_t)now;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- serve_client -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void serve_client(void* argument)
{
    test_server* server = argument;
    const test_socket client = accept(server->listener, NULL, NULL);
    if (client == TEST_NO_SOCKET)
    {
        return;
    }
    char request[4096];
    size_t length = 0;
    request[0] = '\0';
    while (length < sizeof(request) - 1 && strstr(request, "\r\n\r\n") == NULL)
    {
        const int now =
            (int)recv(client, request + length, (int)(sizeof(request) - 1 - length), 0);
        if (now <= 0)
        {
            break;
        }
        length += (size_t)now;
        request[length] = '\0';
    }
    request[length] = '\0';
    const char* key = strstr(request, "Sec-WebSocket-Key: ");
    if (key != NULL)
    {
        key += strlen("Sec-WebSocket-Key: ");
        const size_t key_length = strcspn(key, "\r");
        dtnmos_sha1 sha1;
        dtnmos_sha1_init(&sha1);
        dtnmos_sha1_update(&sha1, key, key_length);
        dtnmos_sha1_update(&sha1, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
        uint8_t digest[20];
        dtnmos_sha1_final(&sha1, digest);
        char accept_key[32];
        base64(digest, sizeof(digest), accept_key);
        char answer[256];
        const int answer_length =
            snprintf(answer, sizeof(answer),
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                     accept_key);
        send(client, answer, answer_length, 0);
        server->handshake_ok = 1;

        // A message in two fragments with a ping between them, one of 70000 bytes, and
        // the close.
        send_frame(client, 0x1, 0, "hello ", 6);
        send_frame(client, 0x9, 1, "", 0);
        send_frame(client, 0x0, 1, "world", 5);
        char* large = malloc(70000);
        if (large != NULL)
        {
            memset(large, 'x', 70000);
            send_frame(client, 0x1, 1, large, 70000);
            free(large);
        }
        send_frame(client, 0x8, 1, "\x03\xe8", 2);
        // Wait for the client to close, so that nothing is lost in a reset.
        char rest[256];
        while (recv(client, rest, sizeof(rest), 0) > 0)
        {
        }
    }
    test_close_socket(client);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- websocket_on_curl_reads_messages -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void websocket_on_curl_reads_messages(void)
{
    if (!dtnmos_has_curl_websocket())
    {
        printf("  skipped: no WebSocket on libcurl\n");
        return;
    }
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    test_server server;
    memset(&server, 0, sizeof(server));
    server.listener = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(server.listener != TEST_NO_SOCKET);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(server.listener, (struct sockaddr*)&address, sizeof(address)) == 0);
    REQUIRE(listen(server.listener, 1) == 0);
    socklen_t address_length = sizeof(address);
    REQUIRE(getsockname(server.listener, (struct sockaddr*)&address, &address_length) ==
            0);
    server.port = ntohs(address.sin_port);
    dtnmos_thread* thread = dtnmos_thread_start(serve_client, &server);
    REQUIRE(thread != NULL);

    char url[64];
    snprintf(url, sizeof(url), "ws://127.0.0.1:%u/ws", (unsigned)server.port);
    const dtnmos_websocket_transport* websocket = dtnmos_curl_websocket();
    void* connection = NULL;
    dtnmos_error error = {DTNMOS_OK, ""};
    const dtnmos_result connected =
        websocket->connect(websocket->user, url, 2000, &connection, &error);
    if (connected != DTNMOS_OK)
    {
        printf("  %s\n", error.message);
    }
    if (connected == DTNMOS_OK)
    {
        dtnmos_string message = {0};
        CHECK(websocket->receive(websocket->user, connection, 2000, &message, &error) ==
              DTNMOS_OK);
        CHECK_STR(dtnmos_string_get(&message), "hello world");
        CHECK(websocket->receive(websocket->user, connection, 2000, &message, &error) ==
              DTNMOS_OK);
        CHECK_EQ(dtnmos_string_length(&message), 70000);
        CHECK(websocket->receive(websocket->user, connection, 2000, &message, &error) ==
              DTNMOS_E_NETWORK);
        dtnmos_string_clear(&message);
        websocket->close(websocket->user, connection);
    }
    CHECK(connected == DTNMOS_OK);
    dtnmos_thread_join(thread);
    CHECK_EQ(server.handshake_ok, 1);
    test_close_socket(server.listener);

    // Nobody listens any more: the WebSocket cannot be opened. Windows tries a refused
    // connection again for about two seconds, so it may run out of time instead.
    const dtnmos_result refused =
        websocket->connect(websocket->user, url, 500, &connection, &error);
    CHECK(refused == DTNMOS_E_NETWORK || refused == DTNMOS_E_TIMEOUT);
    CHECK(connection == NULL);
}
