# Examples

Small command-line programs that show how an application uses dtnmos. Each is one C
file, built with the library unless `DTNMOS_BUILD_EXAMPLES` is off.

| Program | Does |
|---|---|
| `DtNmosReadSdp` | Reads the SDP of an ST 2110 sender and prints its session and flows; `--write` writes them back as the SDP a sender of dtnmos gives |
| `DtNmosListSenders` | Lists the senders a registry holds, with `--receivers` its receivers too, and with `--sdp` the SDP of each sender |
| `DtNmosConnect` | Connects a receiver of a registry to a sender, each by its ID or label, as a controller of IS-05 does; `--disconnect` disconnects it |
| `DtNmosRegisterNode` | Registers a node with a sender and a receiver, serves its Node API and Connection API, and prints what a controller activates on them; with `--ptp` the node has a PTP clock |

Every program lists its options with `--help`. A program that talks to a registry takes
its base URL with `--registry`, or without it finds one with DNS-SD, through multicast
DNS and the DNS server of the host. All but `DtNmosReadSdp` need dtnmos built with
libcurl, `-DDTNMOS_WITH_CURL=ON`, and `DtNmosRegisterNode` serves only when it is built
with civetweb as well, `-DDTNMOS_WITH_SERVER=ON`; the presets `windows-full` and
`linux-full` build both.

## Without a network

    DtNmosReadSdp --file Examples/Data/Camera.sdp
    DtNmosReadSdp --file Examples/Data/Camera.sdp --write

`Data/Camera.sdp` is a video flow on two paths of ST 2022-7 and an audio flow.

## With a registry

A node registers, its sender and receiver are listed, and the receiver is connected to
the sender and disconnected again, against a registry at `http://registry:8010`, such as
the one of nmos-cpp:

    DtNmosRegisterNode --registry http://registry:8010 --seconds 60 &
    DtNmosListSenders --registry http://registry:8010 --receivers
    DtNmosConnect --registry http://registry:8010 --receiver "dtnmos example receiver" \
        --sender "dtnmos example sender" --sdp
    DtNmosConnect --registry http://registry:8010 --receiver "dtnmos example receiver" \
        --disconnect

`DtNmosRegisterNode` prints each activation as its callback gets it:

    receiver a3b1ccff-...: receive video 239.100.1.1:5004 from any source, sender 9dfb...
    receiver a3b1ccff-...: stop receiving

When it ends, it deletes what it registered from the registry. Its IDs follow from
`--label`, so that a node started again keeps them, as IS-04 asks.
