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
#include "NmosTest.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
typedef SOCKET NmosTestSocket;
    #define TEST_NO_SOCKET INVALID_SOCKET
    #define TestCloseSocket closesocket
    // The length that send() and recv() take.
    #define TEST_LENGTH(n) ((int)(n))
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
typedef int NmosTestSocket;
    #define TEST_NO_SOCKET (-1)
    #define TestCloseSocket close
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
typedef struct NmosFakeSubscription
{
    int Status;          // what the registry answers the POST
    const char* Answer;  // and with what
    char Body[512];      // of the POST
    char Url[256];       // of the POST
    char Connected[256]; // the URL the WebSocket connected to
    int ConnectFails;
    const char* const* Messages; // what the WebSocket gives, then it closes
    size_t Count;
    size_t Next;
    int Closed;
} NmosFakeSubscription;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeHttp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtNmosResult FakeHttp(void* User, const DtNmosHttpRequest* Request,
                             DtNmosHttpResponse* Response)
{
    NmosFakeSubscription* Fake = User;
    NMOS_EXPECT(strcmp(Request->Method, "POST") == 0);
    NMOS_EXPECT(strcmp(Request->ContentType, "application/json") == 0);
    snprintf(Fake->Url, sizeof(Fake->Url), "%s", Request->Url);
    snprintf(Fake->Body, sizeof(Fake->Body), "%.*s", (int)Request->BodyLength,
             Request->Body);
    DtNmosHttpResponse_SetStatus(Response, Fake->Status);
    DtNmosHttpResponse_SetBody(Response, "application/json", Fake->Answer,
                               strlen(Fake->Answer));
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeConnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult FakeConnect(void* User, const char* Url, uint32_t TimeoutMs,
                                void** Connection)
{
    NmosFakeSubscription* Fake = User;
    NMOS_EXPECT(TimeoutMs == 2000);
    snprintf(Fake->Connected, sizeof(Fake->Connected), "%s", Url);
    if (Fake->ConnectFails)
    {
        return NmosError_Fail(DTNMOS_E_NETWORK, "connection refused");
    }
    *Connection = Fake;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtNmosResult FakeReceive(void* User, void* Connection, uint32_t TimeoutMs,
                                const char** Message, size_t* Length)
{
    (void)Connection;
    NmosFakeSubscription* Fake = User;
    if (Fake->Next < Fake->Count)
    {
        const char* Text = Fake->Messages[Fake->Next++];
        if (Text == NULL)
        {
            return NmosError_Fail(DTNMOS_E_TIMEOUT, "No message came within %u ms.",
                                  (unsigned)TimeoutMs);
        }
        // The messages of the fake outlive its connection.
        *Message = Text;
        *Length = strlen(Text);
        return DTNMOS_OK;
    }
    return NmosError_Fail(DTNMOS_E_NETWORK, "The server closed the WebSocket.");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FakeClose(void* User, void* Connection)
{
    (void)Connection;
    ((NmosFakeSubscription*)User)->Closed = 1;
}

// What the changes were, as "<kind> <id> <label before> <label after>".
typedef struct NmosRecordedChanges
{
    char Lines[8][160];
    size_t Count;
} NmosRecordedChanges;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RecordChange -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void RecordChange(void* User, const DtNmosChange* Change)
{
    NmosRecordedChanges* Recorded = User;
    DtNmosSenderList* Before = NULL;
    DtNmosSenderList* After = NULL;
    if (Change->Pre != NULL)
    {
        NMOS_ASSERT(DtNmosSenderInfo_Parse(Change->Pre, Change->PreLength, &Before) ==
                    DTNMOS_OK);
    }
    if (Change->Post != NULL)
    {
        NMOS_ASSERT(DtNmosSenderInfo_Parse(Change->Post, Change->PostLength, &After) ==
                    DTNMOS_OK);
        NMOS_ASSERT(DtNmosSenderList_Count(After) == 1);
        NMOS_ASSERT_STR(DtNmosSenderList_At(After, 0)->Id.Text, Change->Id);
    }
    if (Recorded->Count < 8)
    {
        snprintf(Recorded->Lines[Recorded->Count++], sizeof(Recorded->Lines[0]),
                 "%s %s %s %s", DtNmosChangeKind_Name(Change->Kind), Change->Id,
                 Before != NULL ? DtNmosSenderList_At(Before, 0)->Label : "-",
                 After != NULL ? DtNmosSenderList_At(After, 0)->Label : "-");
    }
    DtNmosSenderList_Free(Before);
    DtNmosSenderList_Free(After);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Subscribe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Creates a query of the registry of fake and a subscription to its senders through
// the WebSocket of fake; returns the result of creating it.
//
static DtNmosResult Subscribe(NmosFakeSubscription* Fake, NmosRecordedChanges* Recorded,
                              DtNmosQuery** Query, DtNmosSubscription** Subscription)
{
    static DtNmosWebSocketTransport Websocket;
    Websocket.Size = sizeof(Websocket);
    Websocket.User = Fake;
    Websocket.Connect = FakeConnect;
    Websocket.Receive = FakeReceive;
    Websocket.Close = FakeClose;
    DtNmosQueryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.RegistryUrl = "http://registry.test";
    Config.Http = FakeHttp;
    Config.HttpUser = Fake;
    Config.TimeoutMs = 2000;
    *Query = DtNmosQuery_Alloc();
    *Subscription = DtNmosSubscription_Alloc();
    if (*Query == NULL || *Subscription == NULL ||
        DtNmosQuery_Open(*Query, &Config) != DTNMOS_OK)
    {
        DtNmosSubscription_Freep(Subscription);
        return DTNMOS_E_INTERNAL;
    }
    DtNmosSubscriptionConfig Wanted;
    memset(&Wanted, 0, sizeof(Wanted));
    Wanted.Size = sizeof(Wanted);
    Wanted.ResourcePath = "/senders";
    Wanted.WebSocket = &Websocket;
    Wanted.OnChange = RecordChange;
    Wanted.OnChangeUser = Recorded;
    const DtNmosResult Result = DtNmosSubscription_Open(*Subscription, *Query, &Wanted);
    if (Result != DTNMOS_OK)
    {
        DtNmosSubscription_Freep(Subscription);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- SubscriptionReportsWhatChanges -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(SubscriptionReportsWhatChanges)
{
    static const char* const Messages[] = {
        // The first message: every sender as it is.
        GRAIN("{\"path\": \"" CAMERA_ID "\", \"pre\": " CAMERA ", \"post\": " CAMERA "}, "
              "{\"path\": \"" MIC_ID "\", \"pre\": " MIC ", \"post\": " MIC "}"),
        NULL, // a poll in which nothing came
        GRAIN("{\"path\": \"" CAMERA_ID "\", \"pre\": " CAMERA ", \"post\": " CAMERA_2
              "}"),
        GRAIN("{\"path\": \"" MIC_ID "\", \"pre\": " MIC "}, {\"path\": \"" MIC_ID
              "\", \"post\": " MIC "}"),
    };
    NmosFakeSubscription Fake;
    memset(&Fake, 0, sizeof(Fake));
    Fake.Status = 201;
    Fake.Answer = "{\"id\": \"1\", \"ws_href\": \"" WS_HREF "\"}";
    Fake.Messages = Messages;
    Fake.Count = sizeof(Messages) / sizeof(Messages[0]);
    NmosRecordedChanges Recorded;
    memset(&Recorded, 0, sizeof(Recorded));
    DtNmosQuery* Query = NULL;
    DtNmosSubscription* Subscription = NULL;
    const DtNmosResult Created = Subscribe(&Fake, &Recorded, &Query, &Subscription);
    if (Created != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    NMOS_ASSERT(Created == DTNMOS_OK);
    NMOS_ASSERT_STR(Fake.Url, BASE "subscriptions");
    NMOS_ASSERT_STR(Fake.Connected, WS_HREF);
    NMOS_ASSERT_STR(DtNmosSubscription_Url(Subscription), WS_HREF);
    NmosJson* Body = NULL;
    NMOS_ASSERT(NmosJson_Parse(Fake.Body, strlen(Fake.Body), &Body) == DTNMOS_OK);
    NMOS_ASSERT_STR(NmosJson_MemberText(Body, "resource_path"), "/senders");
    NMOS_ASSERT_EQ(NmosJson_Member(Body, "max_update_rate_ms")->Number, 100);
    NMOS_ASSERT_EQ(NmosJson_Member(Body, "persist")->Type, DTNMOS_JSON_FALSE);
    NMOS_ASSERT_EQ(NmosJson_Member(Body, "secure")->Type, DTNMOS_JSON_FALSE);
    NMOS_ASSERT_EQ(NmosJson_Member(Body, "params")->Type, DTNMOS_JSON_OBJECT);
    NmosJson_Free(Body);

    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_E_TIMEOUT);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_E_NETWORK);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "closed") != NULL);
    const char* const Expected[] = {
        "present " CAMERA_ID " camera 1 camera 1",
        "present " MIC_ID " mic mic",
        "modified " CAMERA_ID " camera 1 camera 1",
        "removed " MIC_ID " mic -",
        "added " MIC_ID " - mic",
    };
    NMOS_ASSERT(Recorded.Count == sizeof(Expected) / sizeof(Expected[0]));
    for (size_t i = 0; i < Recorded.Count; ++i)
    {
        NMOS_ASSERT_STR(Recorded.Lines[i], Expected[i]);
    }
    DtNmosSubscription_Free(Subscription);
    NMOS_ASSERT_EQ(Fake.Closed, 1);
    DtNmosQuery_Free(Query);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- SubscriptionNamesWhatWentWrong -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(SubscriptionNamesWhatWentWrong)
{
    static const char* const Messages[] = {"not json", "{\"grain\": {}}",
                                           GRAIN("{\"path\": \"" MIC_ID "\"}")};
    NmosFakeSubscription Fake;
    memset(&Fake, 0, sizeof(Fake));
    NmosRecordedChanges Recorded;
    memset(&Recorded, 0, sizeof(Recorded));
    DtNmosQuery* Query = NULL;
    DtNmosSubscription* Subscription = NULL;

    // A registry that refuses, and one that names no WebSocket.
    Fake.Status = 400;
    Fake.Answer = "{\"code\": 400, \"error\": \"bad path\"}";
    NMOS_ASSERT(Subscribe(&Fake, &Recorded, &Query, &Subscription) == DTNMOS_E_HTTP);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(),
                       "answered the subscription to /senders with 400") != NULL);
    NMOS_ASSERT(Subscription == NULL);
    DtNmosQuery_Free(Query);
    Fake.Status = 200;
    Fake.Answer = "{\"id\": \"1\"}";
    NMOS_ASSERT(Subscribe(&Fake, &Recorded, &Query, &Subscription) == DTNMOS_E_PARSE);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "without the ws_href") != NULL);
    DtNmosQuery_Free(Query);
    // A WebSocket that cannot be opened.
    Fake.Answer = "{\"id\": \"1\", \"ws_href\": \"" WS_HREF "\"}";
    Fake.ConnectFails = 1;
    NMOS_ASSERT(Subscribe(&Fake, &Recorded, &Query, &Subscription) == DTNMOS_E_NETWORK);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "connection refused") != NULL);
    DtNmosQuery_Free(Query);

    // Messages that are no grain fail the poll and not the subscription, and an item
    // with neither pre nor post is no change.
    Fake.ConnectFails = 0;
    Fake.Messages = Messages;
    Fake.Count = sizeof(Messages) / sizeof(Messages[0]);
    NMOS_ASSERT(Subscribe(&Fake, &Recorded, &Query, &Subscription) == DTNMOS_OK);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_E_PARSE);
    NMOS_ASSERT(strstr(DtNmos_GetLastError(), "is no grain with data") != NULL);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_E_PARSE);
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 50) == DTNMOS_OK);
    NMOS_ASSERT_EQ(Recorded.Count, 0);
    DtNmosSubscription_Free(Subscription);
    DtNmosQuery_Free(Query);

    // A subscription needs a path and a function.
    DtNmosSubscriptionConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.ResourcePath = "senders";
    Subscription = DtNmosSubscription_Alloc();
    NMOS_ASSERT(Subscription != NULL);
    NMOS_ASSERT(DtNmosSubscription_Open(Subscription, NULL, &Config) ==
                DTNMOS_E_INVALID_ARGUMENT);
    // One that is not open neither polls nor closes.
    NMOS_ASSERT(DtNmosSubscription_Poll(Subscription, 0) == DTNMOS_E_STATE);
    NMOS_ASSERT(DtNmosSubscription_Close(Subscription) == DTNMOS_E_STATE);
    NMOS_ASSERT_STR(DtNmosSubscription_Url(Subscription), "");
    DtNmosSubscription_Freep(&Subscription);
    NMOS_ASSERT(Subscription == NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- JsonWritesWhatItReadsBack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NMOS_TEST(JsonWritesWhatItReadsBack)
{
    const char* Text = "{\"a\": [1, -2.5, 1e3, true, false, null, \"x\\\"y\\n\"], "
                       "\"b\": {}, \"c\": []}";
    NmosJson* Json = NULL;
    NMOS_ASSERT(NmosJson_Parse(Text, strlen(Text), &Json) == DTNMOS_OK);
    NmosBuffer Written;
    memset(&Written, 0, sizeof(Written));
    NmosJson_Write(&Written, Json);
    NMOS_ASSERT(!Written.Failed);
    NMOS_ASSERT_STR(
        Written.Data,
        "{\"a\":[1,-2.5,1000,true,false,null,\"x\\\"y\\n\"],\"b\":{},\"c\":[]}");
    NmosJson_Free(Json);
    NmosBuffer_Free(&Written);
}

// A server of one WebSocket that the test runs on a thread: it accepts one client,
// answers its handshake, and sends what the test of the WebSocket on libcurl reads.
typedef struct NmosTestServer
{
    NmosTestSocket Listener;
    uint16_t Port;
    int HandshakeOk;
} NmosTestServer;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Base64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Base64(const uint8_t* Data, size_t Length, char* Text)
{
    static const char Digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t Out = 0;
    for (size_t i = 0; i < Length; i += 3)
    {
        const uint32_t Group = (uint32_t)Data[i] << 16 |
                               (i + 1 < Length ? (uint32_t)Data[i + 1] << 8 : 0) |
                               (i + 2 < Length ? (uint32_t)Data[i + 2] : 0);
        Text[Out++] = Digits[Group >> 18 & 63];
        Text[Out++] = Digits[Group >> 12 & 63];
        Text[Out++] = i + 1 < Length ? Digits[Group >> 6 & 63] : '=';
        Text[Out++] = i + 2 < Length ? Digits[Group & 63] : '=';
    }
    Text[Out] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends a frame of a server, unmasked: opcode, whether it is the last of its message,
// and its payload.
//
static void SendFrame(NmosTestSocket Client, int Opcode, int Last, const char* Payload,
                      size_t Length)
{
    uint8_t Header[10];
    size_t Size = 2;
    Header[0] = (uint8_t)((Last ? 0x80 : 0) | Opcode);
    if (Length < 126)
    {
        Header[1] = (uint8_t)Length;
    }
    else if (Length < 65536)
    {
        Header[1] = 126;
        Header[2] = (uint8_t)(Length >> 8);
        Header[3] = (uint8_t)Length;
        Size = 4;
    }
    else
    {
        Header[1] = 127;
        for (int i = 0; i < 8; ++i)
        {
            Header[2 + i] = (uint8_t)((uint64_t)Length >> (56 - 8 * i));
        }
        Size = 10;
    }
    send(Client, (const char*)Header, TEST_LENGTH(Size), 0);
    size_t Sent = 0;
    while (Sent < Length)
    {
        const int Now = (int)send(Client, Payload + Sent, TEST_LENGTH(Length - Sent), 0);
        if (Now <= 0)
        {
            return;
        }
        Sent += (size_t)Now;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ServeClient -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ServeClient(void* Argument)
{
    NmosTestServer* Server = Argument;
    const NmosTestSocket Client = accept(Server->Listener, NULL, NULL);
    if (Client == TEST_NO_SOCKET)
    {
        return;
    }
    char Request[4096];
    size_t Length = 0;
    Request[0] = '\0';
    while (Length < sizeof(Request) - 1 && strstr(Request, "\r\n\r\n") == NULL)
    {
        const int Now = (int)recv(Client, Request + Length,
                                  TEST_LENGTH(sizeof(Request) - 1 - Length), 0);
        if (Now <= 0)
        {
            break;
        }
        Length += (size_t)Now;
        Request[Length] = '\0';
    }
    Request[Length] = '\0';
    const char* Key = strstr(Request, "Sec-WebSocket-Key: ");
    if (Key != NULL)
    {
        Key += strlen("Sec-WebSocket-Key: ");
        const size_t KeyLength = strcspn(Key, "\r");
        NmosSha1 Sha1;
        NmosSha1_Init(&Sha1);
        NmosSha1_Update(&Sha1, Key, KeyLength);
        NmosSha1_Update(&Sha1, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
        uint8_t Digest[20];
        NmosSha1_Final(&Sha1, Digest);
        char AcceptKey[32];
        Base64(Digest, sizeof(Digest), AcceptKey);
        char Answer[256];
        const int AnswerLength =
            snprintf(Answer, sizeof(Answer),
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                     AcceptKey);
        send(Client, Answer, TEST_LENGTH(AnswerLength), 0);
        Server->HandshakeOk = 1;

        // A message in two fragments with a ping between them, one of 70000 bytes, and
        // the close.
        SendFrame(Client, 0x1, 0, "hello ", 6);
        SendFrame(Client, 0x9, 1, "", 0);
        SendFrame(Client, 0x0, 1, "world", 5);
        char* Large = malloc(70000);
        if (Large != NULL)
        {
            memset(Large, 'x', 70000);
            SendFrame(Client, 0x1, 1, Large, 70000);
            free(Large);
        }
        SendFrame(Client, 0x8, 1, "\x03\xe8", 2);
        // Wait for the client to close, so that nothing is lost in a reset.
        char Rest[256];
        while (recv(Client, Rest, TEST_LENGTH(sizeof(Rest)), 0) > 0)
        {
        }
    }
    TestCloseSocket(Client);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- WebsocketOnCurlReadsMessages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
NMOS_TEST(WebsocketOnCurlReadsMessages)
{
    if (!DtNmos_HasCurlWebSocket())
    {
        printf("  skipped: no WebSocket on libcurl\n");
        return;
    }
#ifdef _WIN32
    WSADATA Data;
    WSAStartup(MAKEWORD(2, 2), &Data);
#endif
    NmosTestServer Server;
    memset(&Server, 0, sizeof(Server));
    Server.Listener = socket(AF_INET, SOCK_STREAM, 0);
    NMOS_ASSERT(Server.Listener != TEST_NO_SOCKET);
    struct sockaddr_in Address;
    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    NMOS_ASSERT(bind(Server.Listener, (struct sockaddr*)&Address, sizeof(Address)) == 0);
    NMOS_ASSERT(listen(Server.Listener, 1) == 0);
    socklen_t AddressLength = sizeof(Address);
    NMOS_ASSERT(
        getsockname(Server.Listener, (struct sockaddr*)&Address, &AddressLength) == 0);
    Server.Port = ntohs(Address.sin_port);
    NmosThread* Thread = NmosOs_ThreadStart(ServeClient, &Server);
    NMOS_ASSERT(Thread != NULL);

    char Url[64];
    snprintf(Url, sizeof(Url), "ws://127.0.0.1:%u/ws", (unsigned)Server.Port);
    const DtNmosWebSocketTransport* Websocket = DtNmos_CurlWebSocket();
    void* Connection = NULL;
    const DtNmosResult Connected =
        Websocket->Connect(Websocket->User, Url, 2000, &Connection);
    if (Connected != DTNMOS_OK)
    {
        printf("  %s\n", DtNmos_GetLastError());
    }
    if (Connected == DTNMOS_OK)
    {
        const char* Message = NULL;
        size_t Length = 0;
        NMOS_ASSERT(Websocket->Receive(Websocket->User, Connection, 2000, &Message,
                                       &Length) == DTNMOS_OK);
        NMOS_ASSERT_STR(Message, "hello world");
        NMOS_ASSERT_EQ(Length, 11);
        NMOS_ASSERT(Websocket->Receive(Websocket->User, Connection, 2000, &Message,
                                       &Length) == DTNMOS_OK);
        NMOS_ASSERT_EQ(Length, 70000);
        NMOS_ASSERT_EQ(strlen(Message), 70000);
        NMOS_ASSERT(Websocket->Receive(Websocket->User, Connection, 2000, &Message,
                                       &Length) == DTNMOS_E_NETWORK);
        Websocket->Close(Websocket->User, Connection);
    }
    NMOS_ASSERT(Connected == DTNMOS_OK);
    NmosOs_ThreadJoin(Thread);
    NMOS_ASSERT_EQ(Server.HandshakeOk, 1);
    TestCloseSocket(Server.Listener);

    // Nobody listens any more: the WebSocket cannot be opened. Windows tries a refused
    // connection again for about two seconds, so it may run out of time instead.
    const DtNmosResult Refused =
        Websocket->Connect(Websocket->User, Url, 500, &Connection);
    NMOS_ASSERT(Refused == DTNMOS_E_NETWORK || Refused == DTNMOS_E_TIMEOUT);
    NMOS_ASSERT(Connection == NULL);
}

NMOS_TEST_MAIN("Subscription", NMOS_RUN(JsonWritesWhatItReadsBack),
               NMOS_RUN(SubscriptionReportsWhatChanges),
               NMOS_RUN(SubscriptionNamesWhatWentWrong),
               NMOS_RUN(WebsocketOnCurlReadsMessages))
