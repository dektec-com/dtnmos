# Getting started

This page takes an application from nothing to a program that reads an SDP and talks to
an NMOS registry with dtnmos. It assumes C and a compiler, and nothing about NMOS but
what the README of the repository tells.

## What you need

- **A C11 compiler** and **CMake 3.25** or newer. MSVC, GCC and Clang all build it.
- **For HTTP and WebSockets**, libcurl with WebSockets, or HTTP functions of the program
  itself: dtnmos makes no request but through a function the caller passes in, and
  brings one on libcurl, `DtNmos_CurlHttp()`, when it is built with it.
- **For a node that serves its APIs itself**, civetweb; a program with an HTTP server of
  its own answers the requests of a node with `DtNmosNode_Handle()` instead.

Reading and writing SDP needs neither.

## Getting the library

### With vcpkg

dtnmos has a port in DekTec's own registry. A project names that registry in
`vcpkg-configuration.json`, beside its manifest:

    {
      "registries": [
        {
          "kind": "git",
          "repository": "https://github.com/dektec-com/dektec-vcpkg-registry",
          "baseline": "<the registry commit to build against>",
          "packages": [ "dtnmos" ]
        }
      ]
    }

and puts `dtnmos` among the `dependencies` in its `vcpkg.json`, with the features it
wants: `curl` for `DtNmos_CurlHttp()` and the WebSocket of a subscription, `server` for
`DtNmosNode_Serve()`:

    "dependencies": [
      { "name": "dtnmos", "features": [ "curl", "server" ] }
    ]

`find_package(dtnmos CONFIG)` then finds it, with its target `dtnmos::dtnmos`.

### From source

The sources are at <https://github.com/dektec-com/dtnmos>, BSD-3-Clause:

    git clone https://github.com/dektec-com/dtnmos.git
    cd dtnmos
    Scripts/build.sh linux-full     # configure, build and test, with curl and civetweb

`Scripts\build.ps1 windows-full` does the same from PowerShell, and `--list`, `-List`
there, lists the presets. Those without `-full` build dtnmos without libcurl and
civetweb, and need no vcpkg. To install the headers and the library somewhere of your own:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDTNMOS_WITH_CURL=ON
    cmake --build build
    cmake --install build --prefix /usr/local

### With CMake

Building the sources as part of a project needs no install step:

    add_subdirectory(dtnmos)
    target_link_libraries(myapp PRIVATE dtnmos::dtnmos)

`FetchContent` fetches them instead:

    include(FetchContent)
    FetchContent_Declare(dtnmos
        GIT_REPOSITORY https://github.com/dektec-com/dtnmos.git
        GIT_TAG v0.5.2)
    set(DTNMOS_WITH_CURL ON)
    FetchContent_MakeAvailable(dtnmos)
    target_link_libraries(myapp PRIVATE dtnmos::dtnmos)

The tests and the examples are built only when dtnmos is the project itself;
`DTNMOS_BUILD_TESTS` and `DTNMOS_BUILD_EXAMPLES` say otherwise.

### With pkg-config

An installed dtnmos has a `dtnmos.pc`, for a build that does not use CMake:

    cc myapp.c $(pkg-config --cflags --libs dtnmos) -o myapp

## A first program

Reads an SDP and prints where each of its flows is sent:

    #include <dtnmos_sdp.h>
    #include <stdio.h>
    #include <string.h>

    int main(void)
    {
        const char* Text = "v=0\r\n"
                           "o=- 1 1 IN IP4 192.168.1.10\r\n"
                           "s=Camera 1\r\n"
                           "t=0 0\r\n"
                           "m=audio 5006 RTP/AVP 97\r\n"
                           "c=IN IP4 239.10.1.2/64\r\n"
                           "a=rtpmap:97 L24/48000/2\r\n"
                           "a=ptime:1\r\n";
        DtNmosSdp* Sdp = NULL;
        if (DtNmosSdp_Parse(Text, strlen(Text), &Sdp) != DTNMOS_OK)
        {
            fprintf(stderr, "%s\n", DtNmos_GetLastError());
            return 1;
        }
        for (size_t i = 0; i < DtNmosSdp_FlowCount(Sdp); i++)
        {
            const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, i);
            printf("%s to %s:%u\n", DtNmosMedia_Name(Flow->Media), Flow->DestinationIp,
                   (unsigned)Flow->DestinationPort);
        }
        DtNmosSdp_Free(Sdp);
        return 0;
    }

It prints `audio to 239.10.1.2:5006`. `Examples/DtNmosReadSdp.c` prints the formats
too, and writes an SDP back.

## The shape of the API

The README goes through the API by what a program does: read an SDP, ask a registry,
connect a receiver, follow a registry, find registries, be a node. Four things hold
throughout:

- **Results.** Every call that can fail returns a `DtNmosResult`; a result at
  `DTNMOS_E` or above is a failure, and `DtNmos_GetLastError()` then gives a message
  that names what failed, kept for each thread. `DtNmosResult_Name()` gives the name of
  a code, such as `"DTNMOS_E_NOT_FOUND"`.
- **Objects.** A node, a query and a subscription are allocated with `_Alloc()`, opened
  with their config with `_Open()`, closed with `_Close()` and freed with `_Free()` or
  `_Freep()`. A closed one can be opened again with another config, keeping its handle.
- **Structs given to the library** begin with `Size`, which the caller sets to the
  struct's `sizeof`, so that a later version of dtnmos can add fields. 0 is refused.
- **Strings.** A string the caller gives is copied. A bounded value of a protocol is a
  `char` array; any other string in a result points into the object that returned it,
  a `DtNmosSdp` or a list, and stays valid until it is freed. Text the library makes, an
  SDP, a manifest or a URL, goes into the caller's buffer, and too small a buffer gives
  `DTNMOS_E_BUFFER_TOO_SMALL` with the size it needs.

## Where to go next

- [`Examples/README.md`](../Examples/README.md) lists example programs that read an
  SDP, list the senders of a registry, connect a receiver to a sender, and register a
  node, with the command lines to run them.
- The [README](../README.md) shows each part of the API in a short program.
- The headers are the reference: each function's comment says what it does, what it
  writes and which results it returns.
