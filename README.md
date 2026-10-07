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
- A C++23 API over it, header only, in `dtnmos.hpp` and `dtnmos_*.hpp`: value types
  that own what they hold, `std::expected` for results and `std::function` for
  callbacks. See [In C++](#in-c).
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
its headers `dtnmos.h` and `dtnmos_*.h`, with those of the C++ API, a CMake package
(`find_package(dtnmos)`, targets `dtnmos::dtnmos`, and `dtnmos::cpp` for C++) and a
pkg-config file (`pkg-config --cflags --libs dtnmos`). The tests and the example of the
C++ API need a C++23 compiler, and are built where CMake finds a C++ compiler;
`-DDTNMOS_WITH_CPP=OFF` builds without them, e.g. with GCC 11.

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

A video flow's colorimetry follows its raster when the caller sets none:
`DtNmosVideoFormat_SetDefaults()` gives BT601 to SD, BT709 to HD and BT2020 to UHD, by
the height, and SDR; HDR is the caller's to state.

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
failure. The URL of a Query API goes into `DtNmosQueryConfig.RegistryUrl`. A node
takes its registries from a search that keeps looking instead.

A program that keeps looking, as a node needs, opens one `DtNmosRegistrySearch` and
shares it among its nodes and clients. It searches on a thread of its own for the Query
API, the Registration API or both, every 3 seconds, and sooner while a node of it has no
registry; `DtNmosRegistrySearch_List()` copies what it found. A search opened as fed
never searches, and holds the URLs `DtNmosRegistrySearch_Feed()` gives it, for a program
that finds its registries in another way, or a test without a network:

```c
DtNmosRegistrySearchConfig search_config = {0};
search_config.Size = sizeof(search_config);
search_config.Finds = DTNMOS_FINDS_REGISTRATION;  // and/or DTNMOS_FINDS_QUERY
DtNmosRegistrySearch* search = DtNmosRegistrySearch_Alloc();
DtNmosRegistrySearch_Open(search, &search_config);
// ... the nodes that borrow it, closed before it:
DtNmosRegistrySearch_Freep(&search);
```

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
config.RegistrationUrl = NULL;  // or e.g. "http://registry.local"
config.Search = search;          // the search of the program, for a node without a URL
config.Http = DtNmos_CurlHttp;
DtNmosNode* node = DtNmosNode_Alloc();
if (DtNmosNode_Open(node, &config) == DTNMOS_OK)
{
  DtNmosDeviceConfig device = {sizeof(device)};
  DtNmosId_FromName(&config.Id, "card 1", &device.Id);
  device.Label = "card 1";
  DtNmosNode_AddDevice(node, &device);
  // DtNmosNode_AddSender() with the flow it sends and SourceIp, the address of the
  // network port it sends from; DtNmosNode_AddReceiver() with InterfaceIp, that of the
  // port it receives on ...
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

A sender and a receiver need the address of the network port they send from or receive
on, which the node lists as an interface of IS-04 and binds them to: the interface of
the host that has the address, a port of a DekTec card among them. A sender's SDP gives
the address as origin and source filter, and the MAC address of the interface as a
reference clock of `localmac`.

A node has one clock, `clk0`, which every source names: internal unless the `Clock` of
its config says otherwise. A node whose senders follow a PTP grandmaster gives it a
clock of `DTNMOS_CLOCK_PTP`, with the grandmaster's EUI-64 and whether the grandmaster is
traceable and the node locked to it, and `DtNmosNode_SetClock()` changes it, e.g. each
time the program checks its lock; a change registers the node again, and the same clock
changes nothing.

A node without a `RegistrationUrl` takes its registries from the `Search` of its config,
which it borrows; a node with neither fails to open. It registers with the most
preferred usable registry; and when that fails, moves on at once to the next one the
search found, with a heartbeat first, going down the list and starting over from the
top when all have failed, as IS-04 asks of a node. A node given
its registry can move to another when it fails: `RegistryFailed` of the config is called
on the poll thread after `FailuresBeforeSwitch` polls in a row failed (3 when 0), and
returns the URL of the next registry, which the node then registers with from the
start. A registry that answers a first registration with 200 holds an old node of the
same ID, which the node deletes before it registers again.

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

The node never frees `user`; it only passes it to the function. `DtNmosNode_Remove()`
returns at once, and if the function is running at that moment, it may still be
running afterwards. So a program that frees `user` gives a `ReleaseUser` function in the
config, and frees it there. The node calls that function when it is done with `user`:
once the sender or receiver is removed and no call of the function is running. That is
inside `Remove()` or `Close()` when the function is not running, and otherwise when its
last call returns.

```c
static void release_port(void* user)
{
  struct my_port* port = user;  // the object of this receiver alone
  my_port_stop(port);           // what must wait until no activation runs
  free(port);
}

receiver.ReleaseUser = release_port;
DtNmosNode_AddReceiver(node, &receiver, connect_receiver, port);
```

Senders that share one object lower a count of its users in the release instead, and
free the object when the count reaches 0.

An immediate activation calls the function while the controller waits for the answer. A
scheduled one, absolute or relative, is answered with 202 and called from
`DtNmosNode_Poll()` when its time comes; until then the staged parameters take only a
PATCH that cancels it. A sender or receiver whose applying takes time says how long in
`ActivationLeadMs`: its function is called that much before the time, which it gets in
`AtNs`, and the parameters still become active at the time, never before it. While the
function runs, a PATCH of the same sender or receiver is answered with 423, as what it
applies cannot be taken back. The bulk interface applies each of its
patches as a PATCH of its own would. The active parameters hold no `"auto"`: a sender's
`source_ip` is its `SourceIp`, a receiver's `interface_ip` its `InterfaceIp`, and a
receiver given a transport file takes the group, source and port of its flow. Until a
controller connects it, a receiver's active parameters are the `SourceIp`,
`MulticastIp` and `DestinationPort` of its config: what the program has it receive.

The AMWA NMOS Testing Tool passes the node in IS-04-01, IS-05-01 and IS-05-02 without a
failure; the interop tests (`DTNMOS_INTEROP_TESTS`, run with `ctest -L interop`) run
them against it.

## In C++

The C++ API is a set of headers on top of the C library. There is one beside each C
header: `dtnmos.hpp`, `dtnmos_sdp.hpp`, `dtnmos_http.hpp`, `dtnmos_query.hpp` and
`dtnmos_node.hpp`. A program compiles them with its own compiler, which must support
C++23, and links the CMake target `dtnmos::cpp`. All the work is still done by the C
library. The names are those of the C API, with the prefix as the namespace:
`DtNmos::Node::AddSender()` is `DtNmosNode_AddSender()`.

- **C++ types only.** Each C struct has a C++ struct that owns its strings and lists.
  You copy it with `=` and compare it with `==`. Each C enum has an `enum class` with the
  same values. A flow's format is a `std::variant`, and the kind of format it holds is
  the flow's media.
- **Results.** A function that can fail returns a `DtNmos::Expected<T>`, which is a
  `std::expected<T, DtNmos::Error>`. A function without a value returns a
  `DtNmos::Status`. The compiler warns when a program ignores one. An `Error` holds the
  `Result` code and a message. The API never throws an exception itself, and it builds
  with exceptions turned off.
- **One owner.** `Open()` returns an object that is open, and destroying the object
  closes it. You move a node, a search, a query or a subscription; you cannot copy them.
  A node borrows the `RegistrySearch` in its config, and a subscription borrows its
  `Query`. Destroy the search or query after the objects that borrow it. If a program
  destroys it too early, `std::terminate()` stops the program, as it does for a
  `std::thread` that was not joined.
- **Callbacks** are `std::function`s, which may capture what they need. The node frees
  the function of a sender or receiver after it is removed, as soon as no call of the
  function is running. An activation can fail by returning an `Error` or by throwing; the
  controller then gets the message.

A node that finds its registry with DNS-SD, with a receiver:

```cpp
#include <dtnmos_node.hpp>
#include <iostream>

int main()
{
  // The program owns the search, and the node borrows it: declared before the node, it
  // goes after it.
  auto Search = DtNmos::RegistrySearch::Open(
      {.Finds = DtNmos::Finds::Registration, .Discovery = {}, .Fed = false});
  if (!Search)
  {
    std::cerr << Search.error().Message << '\n';
    return 1;
  }
  const DtNmos::Id NodeId =
      DtNmos::Id::FromText("6aac9516-fc8a-5fa0-9ee3-31c7a5219c14").value();
  DtNmos::NodeConfig Config;
  Config.Id = NodeId;
  Config.Label = "my node";
  Config.Http = DtNmos::CurlHttp;
  Config.Search = &*Search;
  auto Node = DtNmos::Node::Open(Config);
  if (!Node)
  {
    std::cerr << Node.error().Message << '\n';
    return 1;
  }
  const DtNmos::Id DeviceId = DtNmos::Id::FromName(NodeId, "card 1").value();
  DtNmos::Status Done =
      Node->AddDevice({.Id = DeviceId, .Label = "card 1", .Description = ""});
  if (Done)
  {
    DtNmos::ReceiverConfig Receiver;
    Receiver.Id = DtNmos::Id::FromName(NodeId, "receiver").value();
    Receiver.DeviceId = DeviceId;
    Receiver.Media = DtNmos::Media::Video;
    Receiver.InterfaceIp = "192.168.1.5"; // the network port it receives on
    Done = Node->AddReceiver(
        Receiver,
        [](const DtNmos::Id&, const DtNmos::ReceiverActivation& Activation) -> DtNmos::Status
        {
          if (Activation.MasterEnable && Activation.HasFlow)
          {
            // Receive Activation.Flow, e.g. Activation.Flow.DestinationIp.
          }
          return {}; // or std::unexpected(DtNmos::Error{DtNmos::Result::State, "why"})
        });
  }
  if (Done)
  {
    Done = Node->Serve();
  }
  if (!Done)
  {
    std::cerr << Done.error().Message << '\n';
    return 1;
  }
  // ... until the program ends; destroying the node deletes what it registered.
  return 0;
}
```

`Examples/DtNmosRegisterNode.cpp` is the whole program. What is in `DtNmos::Detail` is
the wrapper's own, and not for programs.
