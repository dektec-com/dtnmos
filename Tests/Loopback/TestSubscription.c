// #*#*#*#*#*#*#*#*#*#*#*#*# TestSubscription.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Tests of the subscriptions of a registry, and of the WebSocket on libcurl
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "dtnmos_query.h"

#include <stdlib.h>
#include <string.h>

#include "NmosInternal.h"
#include "NmosJson.h"
#include "NmosOs.h"
#include "check.h"
#include "tests.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
typedef SOCKET test_socket;
    #define TEST_NO_SOCKET INVALID_SOCKET
    #define test_close_socket closesocket
    // The length that send() and recv() take.
    #define TEST_LENGTH(n) ((int)(n))
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
typedef int test_socket;
    #define TEST_NO_SOCKET (-1)
    #define test_close_socket close
    #define TEST_LENGTH(n) ((size_t)(n))
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
static DtNmosResult fake_http(void* user, const DtNmosHttpRequest* request,
                              DtNmosHttpResponse* response)
{
    fake_subscription* fake = user;
    CHECK_STR(request->Method, "POST");
    CHECK_STR(request->ContentType, "application/json");
    snprintf(fake->url, sizeof(fake->url), "%s", request->Url);
    snprintf(fake->body, sizeof(fake->body), "%.*s", (int)request->BodyLength,
             request->Body);
    DtNmosHttpResponse_SetStatus(response, fake->status);
    DtNmosHttpResponse_SetBody(response, "application/json", fake->answer,
                               strlen(fake->answer));
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult fake_connect(void* user, const char* url, uint32_t timeout_ms,
                                 void** connection)
{
    fake_subscription* fake = user;
    CHECK_EQ(timeout_ms, 2000);
    snprintf(fake->connected, sizeof(fake->connected), "%s", url);
    if (fake->connect_fails)
    {
        return dtnmos_fail(DTNMOS_E_NETWORK, "connection refused");
    }
    *connection = fake;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- fake_receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult fake_receive(void* user, void* connection, uint32_t timeout_ms,
                                 DtNmosString* message)
{
    (void)connection;
    fake_subscription* fake = user;
    if (fake->next < fake->count)
    {
        const char* text = fake->messages[fake->next++];
        if (text == NULL)
        {
            return dtnmos_fail(DTNMOS_E_TIMEOUT, "No message came within %u ms.",
                               (unsigned)timeout_ms);
        }
        return DtNmosString_SetText(message, text);
    }
    return dtnmos_fail(DTNMOS_E_NETWORK, "The server closed the WebSocket.");
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
static void record_change(void* user, const DtNmosChange* change)
{
    recorded_changes* recorded = user;
    DtNmosSenderInfo before = {0};
    DtNmosSenderInfo after = {0};
    if (change->Pre != NULL)
    {
        CHECK(DtNmosSenderInfo_Parse(change->Pre, change->PreLength, &before) ==
              DTNMOS_OK);
    }
    if (change->Post != NULL)
    {
        CHECK(DtNmosSenderInfo_Parse(change->Post, change->PostLength, &after) ==
              DTNMOS_OK);
        CHECK_STR(after.Id.Text, change->Id);
    }
    if (recorded->count < 8)
    {
        snprintf(recorded->lines[recorded->count++], sizeof(recorded->lines[0]),
                 "%s %s %s %s", DtNmosChangeKind_Name(change->Kind), change->Id,
                 change->Pre != NULL ? DtNmosString_Get(&before.Label) : "-",
                 change->Post != NULL ? DtNmosString_Get(&after.Label) : "-");
    }
    DtNmosSenderInfo_Clear(&before);
    DtNmosSenderInfo_Clear(&after);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- subscribe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Creates a query of the registry of fake and a subscription to its senders through
// the WebSocket of fake; returns the result of creating it.
//
static DtNmosResult subscribe(fake_subscription* fake, recorded_changes* recorded,
                              DtNmosQuery** query, DtNmosSubscription** subscription)
{
    static DtNmosWebSocketTransport websocket;
    websocket.Size = sizeof(websocket);
    websocket.User = fake;
    websocket.Connect = fake_connect;
    websocket.Receive = fake_receive;
    websocket.Close = fake_close;
    DtNmosQueryConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.RegistryUrl = "http://registry.test";
    config.Http = fake_http;
    config.HttpUser = fake;
    config.TimeoutMs = 2000;
    *query = NULL;
    *subscription = NULL;
    if (DtNmosQuery_Create(&config, query) != DTNMOS_OK)
    {
        return DTNMOS_E_INTERNAL;
    }
    DtNmosSubscriptionConfig wanted;
    memset(&wanted, 0, sizeof(wanted));
    wanted.Size = sizeof(wanted);
    wanted.ResourcePath = "/senders";
    wanted.WebSocket = &websocket;
    wanted.OnChange = record_change;
    wanted.OnChangeUser = recorded;
    return DtNmosSubscription_Create(*query, &wanted, subscription);
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
    DtNmosQuery* query = NULL;
    DtNmosSubscription* subscription = NULL;
    const DtNmosResult created = subscribe(&fake, &recorded, &query, &subscription);
    if (created != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    REQUIRE(created == DTNMOS_OK);
    CHECK_STR(fake.url, BASE "subscriptions");
    CHECK_STR(fake.connected, WS_HREF);
    CHECK_STR(DtNmosSubscription_Url(subscription), WS_HREF);
    dtnmos_json* body = NULL;
    REQUIRE(dtnmos_json_parse(fake.body, strlen(fake.body), &body) == DTNMOS_OK);
    CHECK_STR(dtnmos_json_member_text(body, "resource_path"), "/senders");
    CHECK_EQ(dtnmos_json_member(body, "max_update_rate_ms")->number, 100);
    CHECK_EQ(dtnmos_json_member(body, "persist")->type, DTNMOS_JSON_FALSE);
    CHECK_EQ(dtnmos_json_member(body, "secure")->type, DTNMOS_JSON_FALSE);
    CHECK_EQ(dtnmos_json_member(body, "params")->type, DTNMOS_JSON_OBJECT);
    dtnmos_json_free(body);

    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_OK);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_E_TIMEOUT);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_OK);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_OK);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_E_NETWORK);
    CHECK(strstr(DtNmos_GetLastError(), "closed") != NULL);
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
    DtNmosSubscription_Destroy(subscription);
    CHECK_EQ(fake.closed, 1);
    DtNmosQuery_Destroy(query);
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
    DtNmosQuery* query = NULL;
    DtNmosSubscription* subscription = NULL;

    // A registry that refuses, and one that names no WebSocket.
    fake.status = 400;
    fake.answer = "{\"code\": 400, \"error\": \"bad path\"}";
    CHECK(subscribe(&fake, &recorded, &query, &subscription) == DTNMOS_E_HTTP);
    CHECK(strstr(DtNmos_GetLastError(),
                 "answered the subscription to /senders with 400") != NULL);
    CHECK(subscription == NULL);
    DtNmosQuery_Destroy(query);
    fake.status = 200;
    fake.answer = "{\"id\": \"1\"}";
    CHECK(subscribe(&fake, &recorded, &query, &subscription) == DTNMOS_E_PARSE);
    CHECK(strstr(DtNmos_GetLastError(), "without the ws_href") != NULL);
    DtNmosQuery_Destroy(query);
    // A WebSocket that cannot be opened.
    fake.answer = "{\"id\": \"1\", \"ws_href\": \"" WS_HREF "\"}";
    fake.connect_fails = 1;
    CHECK(subscribe(&fake, &recorded, &query, &subscription) == DTNMOS_E_NETWORK);
    CHECK(strstr(DtNmos_GetLastError(), "connection refused") != NULL);
    DtNmosQuery_Destroy(query);

    // Messages that are no grain fail the poll and not the subscription, and an item
    // with neither pre nor post is no change.
    fake.connect_fails = 0;
    fake.messages = messages;
    fake.count = sizeof(messages) / sizeof(messages[0]);
    REQUIRE(subscribe(&fake, &recorded, &query, &subscription) == DTNMOS_OK);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_E_PARSE);
    CHECK(strstr(DtNmos_GetLastError(), "is no grain with data") != NULL);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_E_PARSE);
    CHECK(DtNmosSubscription_Poll(subscription, 50) == DTNMOS_OK);
    CHECK_EQ(recorded.count, 0);
    DtNmosSubscription_Destroy(subscription);
    DtNmosQuery_Destroy(query);

    // A subscription needs a path and a function.
    DtNmosSubscriptionConfig config;
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.ResourcePath = "senders";
    CHECK(DtNmosSubscription_Create(NULL, &config, &subscription) ==
          DTNMOS_E_INVALID_ARGUMENT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- json_writes_what_it_reads_back -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void json_writes_what_it_reads_back(void)
{
    const char* text = "{\"a\": [1, -2.5, 1e3, true, false, null, \"x\\\"y\\n\"], "
                       "\"b\": {}, \"c\": []}";
    dtnmos_json* json = NULL;
    REQUIRE(dtnmos_json_parse(text, strlen(text), &json) == DTNMOS_OK);
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
    send(client, (const char*)header, TEST_LENGTH(size), 0);
    size_t sent = 0;
    while (sent < length)
    {
        const int now = (int)send(client, payload + sent, TEST_LENGTH(length - sent), 0);
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
        const int now = (int)recv(client, request + length,
                                  TEST_LENGTH(sizeof(request) - 1 - length), 0);
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
        send(client, answer, TEST_LENGTH(answer_length), 0);
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
        while (recv(client, rest, TEST_LENGTH(sizeof(rest)), 0) > 0)
        {
        }
    }
    test_close_socket(client);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- websocket_on_curl_reads_messages -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void websocket_on_curl_reads_messages(void)
{
    if (!DtNmos_HasCurlWebSocket())
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
    const DtNmosWebSocketTransport* websocket = DtNmos_CurlWebSocket();
    void* connection = NULL;
    const DtNmosResult connected =
        websocket->Connect(websocket->User, url, 2000, &connection);
    if (connected != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    if (connected == DTNMOS_OK)
    {
        DtNmosString message = {0};
        CHECK(websocket->Receive(websocket->User, connection, 2000, &message) ==
              DTNMOS_OK);
        CHECK_STR(DtNmosString_Get(&message), "hello world");
        CHECK(websocket->Receive(websocket->User, connection, 2000, &message) ==
              DTNMOS_OK);
        CHECK_EQ(DtNmosString_Length(&message), 70000);
        CHECK(websocket->Receive(websocket->User, connection, 2000, &message) ==
              DTNMOS_E_NETWORK);
        DtNmosString_Clear(&message);
        websocket->Close(websocket->User, connection);
    }
    CHECK(connected == DTNMOS_OK);
    dtnmos_thread_join(thread);
    CHECK_EQ(server.handshake_ok, 1);
    test_close_socket(server.listener);

    // Nobody listens any more: the WebSocket cannot be opened. Windows tries a refused
    // connection again for about two seconds, so it may run out of time instead.
    const DtNmosResult refused =
        websocket->Connect(websocket->User, url, 500, &connection);
    CHECK(refused == DTNMOS_E_NETWORK || refused == DTNMOS_E_TIMEOUT);
    CHECK(connection == NULL);
}
