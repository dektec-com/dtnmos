# dtnmos

A small C library that reads and writes the SDP of SMPTE ST 2110 flows: ST 2110-20
uncompressed video, -22 compressed video (JPEG XS), -30 audio and -40 ancillary data. It
also asks an NMOS registry (AMWA IS-04 v1.3) for its senders and their SDP. It is the NMOS
support of gst-dektec, and is meant to be used by other projects as well, such as CDTAPI
and FFmpeg; the node that registers senders and receivers follows (plan
[0016](../../docs/plans/0016-st-2110-sdp-and-nmos.md)).

- C11, built with MSVC, GCC and Clang; the headers compile as C and as C++.
- No dependencies. HTTP goes through a function the caller passes in; with
  `-DDTNMOS_WITH_CURL=ON` the library brings one on libcurl, `dtnmos_curl_http()`.
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
