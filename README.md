# dtnmos

A small C library that reads and writes the SDP of SMPTE ST 2110 flows: ST 2110-20
uncompressed video, -22 compressed video (JPEG XS), -30 audio and -40 ancillary data. It
also asks an NMOS registry (AMWA IS-04 v1.3) for its senders and their SDP, and finds the
registries on the network with multicast DNS and the DNS server of the host. And it is an
NMOS node that registers senders and receivers with a registry (IS-04) and serves its
Node API, and a controller that connects the receivers of a registry to its senders
(IS-05). It follows a registry through the subscriptions of its Query API.

It is the NMOS support of DekTec's GStreamer plugins, and is meant to be used by other
projects as well, such as CDTAPI and FFmpeg.

- C11, built with MSVC, GCC and Clang; the headers compile as C and as C++.
- No dependencies. HTTP goes through a function the caller passes in; with
  `-DDTNMOS_WITH_CURL=ON` the library brings one on libcurl, `DtNmos_CurlHttp()`, and with
  `-DDTNMOS_WITH_SERVER=ON` a server of the node on civetweb, `DtNmosNode_Serve()`.
- BSD-3-Clause. A build with libcurl or civetweb links them and what they need;
  `THIRD-PARTY-NOTICES` has their licences.

[`Docs/getting-started.md`](Docs/getting-started.md) takes a program from nothing to
reading an SDP and talking to a registry, and [`Examples/`](Examples/README.md) has
programs that read an SDP, list the senders of a registry, connect a receiver and
register a node.

## Building

It builds, tests and installs on its own with CMake 3.25 or later:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix /usr/local
```

It installs a static library by default, or a shared one with `-DBUILD_SHARED_LIBS=ON`,
its headers `dtnmos.h` and `dtnmos_*.h`, a CMake package (`find_package(dtnmos)`, target
`dtnmos::dtnmos`) and a pkg-config file (`pkg-config --cflags --libs dtnmos`).

## Using it

Every function that can fail returns a `DtNmosResult`, and `DtNmos_GetLastError()` then
gives a message that says what failed, kept for each thread. A callback the library
calls, the activation of a node or an HTTP function, fails with
`return DtNmos_SetLastError(DTNMOS_E_..., "what failed");`.

Every string has an owner, so no struct needs a function to free or copy it, and every
struct is copied with `=`:

- a string the caller gives is a `const char*` in the caller's memory, and the library
  copies what it keeps;
- a value the standards bound, an encoding or an address, is a `char` array of a named
  size, `DTNMOS_MAX_..._SIZE`;
- any other string in a result is a `const char*` into the object that returned it, a
  `DtNmosSdp` or a list, valid until that object is freed;
- text the library makes, an SDP, a manifest or a URL, goes into the caller's buffer of
  `*size` bytes. Too small a buffer fails with `DTNMOS_E_BUFFER_TOO_SMALL`, and `*size`
  then gives the bytes needed.

Reading an SDP gives a handle that owns its flows:

```c
#include <dtnmos_sdp.h>
#include <stdio.h>
#include <string.h>

int print_flows(const char* text)
{
  DtNmosSdp* sdp = NULL;
  if (DtNmosSdp_Parse(text, strlen(text), &sdp) != DTNMOS_OK)
  {
    fprintf(stderr, "%s\n", DtNmos_GetLastError());
    return 1;
  }
  for (size_t i = 0; i < DtNmosSdp_FlowCount(sdp); ++i)
  {
    const DtNmosFlow* flow = DtNmosSdp_Flow(sdp, i);
    printf("%s to %s:%u", DtNmosMedia_Name(flow->Media),
           flow->DestinationIp, (unsigned)flow->DestinationPort);
    if (flow->Media == DTNMOS_MEDIA_VIDEO)
    {
      printf(", %ux%u at %u/%u", (unsigned)flow->Format.Video.Width,
             (unsigned)flow->Format.Video.Height, (unsigned)flow->Format.Video.RateNumerator,
             (unsigned)flow->Format.Video.RateDenominator);
    }
    printf("\n");
  }
  DtNmosSdp_Free(sdp);
  return 0;
}
```

Writing one takes the session and flows the caller fills:

```c
DtNmosSession session = {0};
session.Size = sizeof(session);
snprintf(session.OriginIp, sizeof(session.OriginIp), "%s", "192.168.1.10");
DtNmosFlow flow = {0};
flow.Size = sizeof(flow);
flow.Media = DTNMOS_MEDIA_AUDIO;
snprintf(flow.DestinationIp, sizeof(flow.DestinationIp), "%s", "239.0.0.2");
flow.DestinationPort = 5004;
flow.PayloadType = 97;
flow.ClockRate = 48000;
snprintf(flow.Format.Audio.Encoding, sizeof(flow.Format.Audio.Encoding), "%s", "L24");
flow.Format.Audio.SampleRate = 48000;
flow.Format.Audio.Channels = 2;
flow.Format.Audio.PacketTimeNs = 1000000;

char text[4096];
size_t size = sizeof(text);
if (DtNmosSdp_Write(&session, &flow, 1, text, &size) == DTNMOS_OK)
{
  puts(text);
}
```

The parser reports what a description says and judges no format: a receiver decides
whether it can carry a flow. A media it does not know comes back as `DTNMOS_MEDIA_OTHER`
with its encoding and the text of its `a=fmtp`, and both paths of ST 2022-7 come back as
flows, the second with `Leg` 1.

`DtNmosId_FromName()` makes the name-based UUIDs (version 5) that NMOS resources keep
across restarts.

## Asking a registry

A query lists the senders of a registry, finds one by its ID or label, and fetches its SDP.
HTTP goes through the function in its config, which fills a response:

```c
#include <dtnmos_query.h>

DtNmosQueryConfig config = {0};
config.Size = sizeof(config);
config.RegistryUrl = "http://registry.local";
config.Http = DtNmos_CurlHttp;  // or a function on the HTTP stack of the program
DtNmosQuery* query = DtNmosQuery_Alloc();
if (DtNmosQuery_Open(query, &config) == DTNMOS_OK)
{
  DtNmosSenderList* found = NULL;
  DtNmosSdp* sdp = NULL;
  if (DtNmosQuery_FindSender(query, "camera 1", &found) == DTNMOS_OK &&
      DtNmosQuery_SenderSdp(query, DtNmosSenderList_At(found, 0), &sdp) == DTNMOS_OK)
  {
    // The flows of the sender, as DtNmosSdp_Parse() gives them.
    DtNmosSdp_Free(sdp);
  }
  DtNmosSenderList_Free(found);
}
DtNmosQuery_Freep(&query);  // closes it when it is open
```

An HTTP function of its own receives a `DtNmosHttpRequest` and fills the response with
`DtNmosHttpResponse_SetStatus()`, `_add_header()` and `_set_body()`; it returns
`DTNMOS_OK` whenever the server answered, whatever the status.

`DtNmosQuery_Receivers()` and `DtNmosQuery_FindReceiver()` list and find the receivers
of a registry the same way, each with the sender it is subscribed to. What finds one
sender or receiver returns it in a list of one, which owns its strings, as every list
does: they stay valid until the list is freed.

## Connecting a receiver

A controller connects a receiver of the registry to a sender, each by its ID or label, and
disconnects it (`dtnmos_query.h`). It finds the Connection API of the receiver through
the control `urn:x-nmos:control:sr-ctrl/v1.1` of its device, and activates at once its
staged parameters with the sender and the SDP of the sender as transport file. The
requests to the node go through the HTTP function of the query:

```c

DtNmosConnection* connection = NULL;
if (DtNmosQuery_Connect(query, "monitor", "camera 1", &connection) == DTNMOS_OK)
{
  // DtNmosConnection_Receiver() and _Sender() as the registry lists them, and
  // DtNmosConnection_Sdp(), the transport file the receiver was given.
  DtNmosConnection_Free(connection);
}
DtNmosQuery_Disconnect(query, "monitor", NULL);
```

A sender of another kind of media than the receiver, video, audio or data, is refused
before the node is asked; what the node refuses comes back with the error it gave.

`DtNmosQuery_MoveSender()` moves a sender to another destination the same way, through the
Connection API of the sender:

```c
DtNmosQuery_MoveSender(query, "camera 1", "239.10.1.2", 5004, NULL);
```

## Following a registry

A subscription of the Query API tells what changes in the registry as it happens
(`dtnmos_query.h`): first every resource of its path as it is, then each one that
is added, modified or removed, with its JSON before and after. The messages come over a
WebSocket, which, as HTTP, goes through functions the caller passes in; with
`-DDTNMOS_WITH_CURL=ON` and a libcurl with WebSockets, `DtNmos_CurlWebSocket()` is one.
dtnmos starts no thread: the caller polls.

```c

static void on_change(void* user, const DtNmosChange* change)
{
  // change->Kind, change->Id, and change->Pre and change->Post, the JSON of the
  // resource, which DtNmosSenderInfo_Parse() reads for a sender.
}

DtNmosSubscriptionConfig config = {0};
config.Size = sizeof(config);
config.ResourcePath = "/senders";
config.OnChange = on_change;  // websocket left null: DtNmos_CurlWebSocket()
DtNmosSubscription* subscription = DtNmosSubscription_Alloc();
if (DtNmosSubscription_Open(subscription, query, &config) == DTNMOS_OK)
{
  DtNmosResult result = DTNMOS_OK;
  while (result == DTNMOS_OK || result == DTNMOS_E_TIMEOUT)
  {
    result = DtNmosSubscription_Poll(subscription, 1000);
  }
  // DTNMOS_E_NETWORK: the WebSocket closed; opened again, it starts again.
  DtNmosSubscription_Close(subscription);
}
DtNmosSubscription_Freep(&subscription);
```

A message the subscription cannot read fails its poll with `DTNMOS_E_PARSE`, after which
it can be polled again.

## Finding registries

A search finds the Query or Registration APIs that the registries on the network announce,
as IS-04 does with DNS-SD, in two ways at once from one socket of the library itself:

- a one-shot multicast DNS query in the domain `local`: it goes to 224.0.0.251:5353 from a
  port of its own, so the responders answer with unicast, and it needs neither port 5353
  nor Avahi or Bonjour;
- a query of the DNS server of the host in the domain the host searches (unicast
  DNS-SD): the first IPv4 `nameserver` and the last `search` or `domain` line of
  `/etc/resolv.conf`, or on Windows the DNS server and suffix of the network connection
  with a gateway. The server is asked one question per query, with recursion desired;
  a host without a server or domain asks multicast DNS alone.

Each registry tells which search found it (`FoundBy`); of equal priority, those of the DNS
server come first. `Searches`, `DnsServer` and `DnsDomain` of the config choose the
searches and give a server and domain of their own. IPv4 only, and DNS over UDP only.

```c

DtNmosDiscoveryConfig config = {0};
config.Size = sizeof(config);
config.Service = DTNMOS_SERVICE_QUERY;  // or DTNMOS_SERVICE_REGISTRATION
config.InterfaceAddress = NULL;        // or the IPv4 address of the interface to ask on
DtNmosRegistryList* list = NULL;
if (DtNmos_Discover(&config, &list) == DTNMOS_OK)
{
  // Usable ones first, by priority: take the first, and the next when it fails.
  for (size_t i = 0; i < DtNmosRegistryList_Count(list); ++i)
  {
    const DtNmosRegistryInfo* registry = DtNmosRegistryList_At(list, i);
    // registry->Url, e.g. "http://192.168.1.5:8080"
  }
  DtNmosRegistryList_Free(list);
}
```

A search takes about a second, `TimeoutMs` of the config, and finding nothing is no
failure. The URL of a Query API goes into `DtNmosQueryConfig.RegistryUrl`, and that of a
Registration API into `DtNmosNodeConfig.RegistrationUrl`.

## Being a node

A node holds devices, and senders and receivers on them. `DtNmosNode_Poll()` registers
what is new, deletes what was removed and sends heartbeats; `DtNmosNode_Handle()` answers
a request to the Node API or the transport file of a sender. A program with a loop and an
HTTP server of its own calls both; with the server of the library, `DtNmosNode_Serve()`
does both on threads of its own:

```c
#include <dtnmos_node.h>

DtNmosNodeConfig config = {0};
config.Size = sizeof(config);
DtNmosId_FromName(&my_namespace, "my node", &config.Id);
config.Label = "my node";
config.RegistrationUrl = "http://registry.local";
config.Http = DtNmos_CurlHttp;
DtNmosNode* node = DtNmosNode_Alloc();
if (DtNmosNode_Open(node, &config) == DTNMOS_OK)
{
  DtNmosDeviceConfig device = {sizeof(device)};
  DtNmosId_FromName(&config.Id, "card 1", &device.Id);
  device.Label = "card 1";
  DtNmosNode_AddDevice(node, &device);
  // DtNmosNode_AddSender() with the flow it sends, DtNmosNode_AddReceiver() ...
  DtNmosNode_Serve(node);
  // ... until the program ends, which deletes what the node registered:
  DtNmosNode_Close(node);
}
DtNmosNode_Freep(&node);
```

Every object of the library is allocated, opened, closed and freed, as CDTAPI's are. A
node, a query or a subscription that is closed can be opened again with another config,
keeping the handle a program has handed on; a function that needs it open fails with
`DTNMOS_E_STATE` on one that is not. `_Free()` closes an open object first, and
`_Freep()` also sets the pointer to null.

A node can move to another registry when its own fails: `RegistryFailed` of the config
is called on the poll thread after `FailuresBeforeSwitch` polls in a row failed (3 when
0), and returns the URL of the next registry, e.g. the next of a `DtNmos_Discover()` for
`DTNMOS_SERVICE_REGISTRATION`, which the node then registers with from the start.

## Being connected

The node answers the Connection API of IS-05 in `DtNmosNode_Handle()` as well. When a
controller activates a sender or receiver, the node calls the function given to
`DtNmosNode_AddSender()` or `DtNmosNode_AddReceiver()`, without its lock, and makes the
staged parameters the active ones only when that function succeeds:

```c
static DtNmosResult connect_receiver(void* user, const DtNmosId* receiver,
                                      const DtNmosReceiverActivation* activation)
{
  if (activation->HasFlow)
  {
    // Receive activation->Flow, e.g. activation->Flow.DestinationIp.
  }
  // Receive, or stop receiving, as activation->MasterEnable says.
  return DTNMOS_OK;
}
```

Immediate activation is supported; a scheduled activation and the bulk interface are
answered with 501.
