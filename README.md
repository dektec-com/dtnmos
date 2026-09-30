# dtnmos

A small C library that reads and writes the SDP of SMPTE ST 2110 flows: ST 2110-20
uncompressed video, -22 compressed video (JPEG XS), -30 audio and -40 ancillary data. It
also asks an NMOS registry (AMWA IS-04 v1.3) for its senders and their SDP, and finds the
registries on the network with multicast DNS. And it is an NMOS node that registers senders
and receivers with a registry (IS-04) and serves its Node API, and a controller that
connects the receivers of a registry to its senders (IS-05). It follows a registry
through the subscriptions of its Query API.
It is the NMOS support of gst-dektec, and is meant to be used by other projects as well,
such as CDTAPI and FFmpeg (plan [0016](../../docs/plans/0016-st-2110-sdp-and-nmos.md)).

- C11, built with MSVC, GCC and Clang; the headers compile as C and as C++.
- No dependencies. HTTP goes through a function the caller passes in; with
  `-DDTNMOS_WITH_CURL=ON` the library brings one on libcurl, `dtnmos_curl_http()`, and with
  `-DDTNMOS_WITH_SERVER=ON` a server of the node on civetweb, `dtnmos_node_serve()`.
- BSD-3-Clause.

## Building

It builds, tests and installs on its own with CMake 3.25 or later:

```bash
cmake -S src/dtnmos -B build/dtnmos -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/dtnmos
ctest --test-dir build/dtnmos
cmake --install build/dtnmos --prefix /usr/local
```

It installs a static library by default, or a shared one with `-DBUILD_SHARED_LIBS=ON`, its
headers under `dtnmos/`, a CMake package (`find_package(dtnmos)`, target `dtnmos::dtnmos`)
and a pkg-config file (`pkg-config --cflags --libs dtnmos`).

## Using it

Every function that can fail returns a `dtnmos_result` and fills an optional
`dtnmos_error` with a message that says what failed. Strings the library hands out are
`dtnmos_string`: read them with `dtnmos_string_get()`, and free a struct that holds them
with its `_clear()`. A struct set to zero is valid and empty, and must not be copied with
`=`: use its `_copy()`.

Reading an SDP gives a handle that owns its flows:

```c
#include <dtnmos/sdp.h>
#include <stdio.h>
#include <string.h>

int print_flows(const char* text)
{
  dtnmos_sdp* sdp = NULL;
  dtnmos_error error = {0};
  if (dtnmos_sdp_parse(text, strlen(text), &sdp, &error) != DTNMOS_OK)
  {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  for (size_t i = 0; i < dtnmos_sdp_flow_count(sdp); ++i)
  {
    const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, i);
    printf("%s to %s:%u", dtnmos_media_name(flow->media),
           dtnmos_string_get(&flow->destination_ip), (unsigned)flow->destination_port);
    if (flow->media == DTNMOS_MEDIA_VIDEO)
    {
      printf(", %ux%u at %u/%u", (unsigned)flow->format.video.width,
             (unsigned)flow->format.video.height, (unsigned)flow->format.video.rate_numerator,
             (unsigned)flow->format.video.rate_denominator);
    }
    printf("\n");
  }
  dtnmos_sdp_free(sdp);
  return 0;
}
```

Writing one takes the session and flows the caller fills:

```c
dtnmos_session session = {0};
session.size = sizeof(session);
dtnmos_string_set_text(&session.origin_ip, "192.168.1.10");
dtnmos_flow flow = {0};
flow.size = sizeof(flow);
flow.media = DTNMOS_MEDIA_AUDIO;
dtnmos_string_set_text(&flow.destination_ip, "239.0.0.2");
flow.destination_port = 5004;
flow.payload_type = 97;
flow.clock_rate = 48000;
dtnmos_string_set_text(&flow.format.audio.encoding, "L24");
flow.format.audio.sample_rate = 48000;
flow.format.audio.channels = 2;
flow.format.audio.packet_time_ns = 1000000;

dtnmos_string text = {0};
if (dtnmos_sdp_write(&session, &flow, 1, &text, NULL) == DTNMOS_OK)
{
  puts(dtnmos_string_get(&text));
}
dtnmos_string_clear(&text);
dtnmos_flow_clear(&flow);
dtnmos_session_clear(&session);
```

The parser reports what a description says and judges no format: a receiver decides
whether it can carry a flow. A media it does not know comes back as `DTNMOS_MEDIA_OTHER`
with its encoding and the text of its `a=fmtp`, and both paths of ST 2022-7 come back as
flows, the second with `leg` 1.

`dtnmos_id_from_name()` makes the name-based UUIDs (version 5) that NMOS resources keep
across restarts.

## Asking a registry

A query lists the senders of a registry, finds one by its ID or label, and fetches its SDP.
HTTP goes through the function in its config, which fills a response:

```c
#include <dtnmos/query.h>

dtnmos_query_config config = {0};
config.size = sizeof(config);
config.registry_url = "http://registry.local";
config.http = dtnmos_curl_http;  // or a function on the HTTP stack of the program
dtnmos_query* query = NULL;
dtnmos_error error = {0};
if (dtnmos_query_create(&config, &query, &error) == DTNMOS_OK)
{
  dtnmos_sender_info sender = {0};
  dtnmos_sdp* sdp = NULL;
  if (dtnmos_query_find_sender(query, "camera 1", &sender, &error) == DTNMOS_OK &&
      dtnmos_query_sender_sdp(query, &sender, &sdp, &error) == DTNMOS_OK)
  {
    // The flows of the sender, as dtnmos_sdp_parse() gives them.
    dtnmos_sdp_free(sdp);
  }
  dtnmos_sender_info_clear(&sender);
  dtnmos_query_destroy(query);
}
```

An HTTP function of its own receives a `dtnmos_http_request` and fills the response with
`dtnmos_http_response_set_status()`, `_add_header()` and `_set_body()`; it returns
`DTNMOS_OK` whenever the server answered, whatever the status.

`dtnmos_query_receivers()` and `dtnmos_query_find_receiver()` list and find the receivers
of a registry the same way, each with the sender it is subscribed to.

## Connecting a receiver

A controller connects a receiver of the registry to a sender, each by its ID or label, and
disconnects it (`dtnmos/controller.h`). It finds the Connection API of the receiver through
the control `urn:x-nmos:control:sr-ctrl/v1.1` of its device, and activates at once its
staged parameters with the sender and the SDP of the sender as transport file. The
requests to the node go through the HTTP function of the query:

```c
#include <dtnmos/controller.h>

dtnmos_connection connection = {0};
if (dtnmos_connect(query, "monitor", "camera 1", &connection, &error) == DTNMOS_OK)
{
  // connection.receiver and connection.sender as the registry lists them, and
  // connection.sdp, the transport file the receiver was given.
  dtnmos_connection_clear(&connection);
}
dtnmos_disconnect(query, "monitor", NULL, &error);
```

A sender of another kind of media than the receiver, video, audio or data, is refused
before the node is asked; what the node refuses comes back with the error it gave.

`dtnmos_move_sender()` moves a sender to another destination the same way, through the
Connection API of the sender:

```c
dtnmos_move_sender(query, "camera 1", "239.10.1.2", 5004, NULL, &error);
```

## Following a registry

A subscription of the Query API tells what changes in the registry as it happens
(`dtnmos/subscription.h`): first every resource of its path as it is, then each one that
is added, modified or removed, with its JSON before and after. The messages come over a
WebSocket, which, as HTTP, goes through functions the caller passes in; with
`-DDTNMOS_WITH_CURL=ON` and a libcurl with WebSockets, `dtnmos_curl_websocket()` is one.
dtnmos starts no thread: the caller polls.

```c
#include <dtnmos/subscription.h>

static void on_change(void* user, const dtnmos_change* change)
{
  // change->kind, change->id, and change->pre and change->post, the JSON of the
  // resource, which dtnmos_sender_info_parse() reads for a sender.
}

dtnmos_subscription_config config = {0};
config.size = sizeof(config);
config.resource_path = "/senders";
config.on_change = on_change;  // websocket left null: dtnmos_curl_websocket()
dtnmos_subscription* subscription = NULL;
if (dtnmos_subscription_create(query, &config, &subscription, &error) == DTNMOS_OK)
{
  dtnmos_result result = DTNMOS_OK;
  while (result == DTNMOS_OK || result == DTNMOS_E_TIMEOUT)
  {
    result = dtnmos_subscription_poll(subscription, 1000, &error);
  }
  // DTNMOS_E_NETWORK: the WebSocket closed; a new subscription starts again.
  dtnmos_subscription_destroy(subscription);
}
```

A message the subscription cannot read fails its poll with `DTNMOS_E_PARSE`, after which
it can be polled again.

## Finding registries

A search finds the Query or Registration APIs that the registries on the local network
announce, as IS-04 does with DNS-SD, through a one-shot multicast DNS query of the library
itself: it goes to 224.0.0.251:5353 from a port of its own, so the responders answer with
unicast, and it needs neither port 5353 nor Avahi or Bonjour. IPv4 only.

```c
#include <dtnmos/discovery.h>

dtnmos_discovery_config config = {0};
config.size = sizeof(config);
config.service = DTNMOS_SERVICE_QUERY;  // or DTNMOS_SERVICE_REGISTRATION
config.interface_address = NULL;        // or the IPv4 address of the interface to ask on
dtnmos_registry_list* list = NULL;
dtnmos_error error = {0};
if (dtnmos_discover(&config, &list, &error) == DTNMOS_OK)
{
  // Usable ones first, by priority: take the first, and the next when it fails.
  for (size_t i = 0; i < dtnmos_registry_list_count(list); ++i)
  {
    const dtnmos_registry_info* registry = dtnmos_registry_list_at(list, i);
    // dtnmos_string_get(&registry->url), e.g. "http://192.168.1.5:8080"
  }
  dtnmos_registry_list_free(list);
}
```

A search takes about a second, `timeout_ms` of the config, and finding nothing is no
failure. The URL of a Query API goes into `dtnmos_query_config.registry_url`, and that of a
Registration API into `dtnmos_node_config.registration_url`.

## Being a node

A node holds devices, and senders and receivers on them. `dtnmos_node_poll()` registers
what is new, deletes what was removed and sends heartbeats; `dtnmos_node_handle()` answers
a request to the Node API or the transport file of a sender. A program with a loop and an
HTTP server of its own calls both; with the server of the library, `dtnmos_node_serve()`
does both on threads of its own:

```c
#include <dtnmos/node.h>

dtnmos_node_config config = {0};
config.size = sizeof(config);
dtnmos_id_from_name(&my_namespace, "my node", &config.id, NULL);
config.label = "my node";
config.registration_url = "http://registry.local";
config.http = dtnmos_curl_http;
dtnmos_node* node = NULL;
if (dtnmos_node_create(&config, &node, NULL) == DTNMOS_OK)
{
  dtnmos_device_config device = {sizeof(device)};
  dtnmos_id_from_name(&config.id, "card 1", &device.id, NULL);
  device.label = "card 1";
  dtnmos_node_add_device(node, &device, NULL);
  // dtnmos_node_add_sender() with the flow it sends, dtnmos_node_add_receiver() ...
  dtnmos_node_serve(node, NULL);
  // ... until the program ends, which deletes what the node registered:
  dtnmos_node_destroy(node);
}
```

A node can move to another registry when its own fails: `registry_failed` of the config
is called on the poll thread after `failures_before_switch` polls in a row failed (3 when
0), and returns the URL of the next registry, e.g. the next of a `dtnmos_discover()` for
`DTNMOS_SERVICE_REGISTRATION`, which the node then registers with from the start.

## Being connected

The node answers the Connection API of IS-05 in `dtnmos_node_handle()` as well. When a
controller activates a sender or receiver, the node calls the function given to
`dtnmos_node_add_sender()` or `dtnmos_node_add_receiver()`, without its lock, and makes the
staged parameters the active ones only when that function succeeds:

```c
static dtnmos_result connect_receiver(void* user, const dtnmos_id* receiver,
                                      const dtnmos_receiver_activation* activation,
                                      dtnmos_error* error)
{
  if (activation->has_flow)
  {
    // Receive activation->flow, e.g. dtnmos_string_get(&activation->flow.destination_ip).
  }
  // Receive, or stop receiving, as activation->master_enable says.
  return DTNMOS_OK;
}
```

Immediate activation is supported; a scheduled activation and the bulk interface are
answered with 501.
