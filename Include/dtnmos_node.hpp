// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_node.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API of an NMOS node: registration (IS-04 v1.3) and connection
// management (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos_node.h. A node is the program as NMOS sees it. It holds devices,
// and each device holds senders and receivers. The node registers them with an NMOS
// registry, where controllers find them, and serves the Node API and the Connection API,
// through which a controller connects a sender to a receiver. A program uses a node in
// these steps:
//
// 1. Node::Open() with the registry, or a RegistrySearch, and the node's ID.
// 2. AddDevice(), then AddSender() and AddReceiver(), each with a function that is called
//    when a controller activates it.
// 3. Serve(), which serves the APIs and keeps the registration up to date on a thread of
//    its own. A program with an HTTP server of its own calls Handle() for each request
//    and Poll() regularly instead.
// 4. Destroying the node closes it, which also unregisters everything.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "dtnmos.hpp"
#include "dtnmos_http.hpp"
#include "dtnmos_node.h"
#include "dtnmos_query.hpp"
#include "dtnmos_sdp.hpp"

namespace DtNmos
{

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The node +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//

// The kinds of a node's clock: the values of DtNmosClockKind.
enum class ClockKind : int
{
    None = DTNMOS_CLOCK_NONE,         // Refused
    Internal = DTNMOS_CLOCK_INTERNAL, // No external reference
    Ptp = DTNMOS_CLOCK_PTP            // A PTP grandmaster
};

// The node's clock, clk0, which every source names (IS-04's clocks).
struct Clock
{
    ClockKind Kind = ClockKind::None;
    // Ptp: the grandmaster's EUI-64, eight pairs of hexadecimal digits joined by '-', in
    // either case, e.g. "00-1B-19-FF-FE-00-00-01". The node writes it in lower case, as
    // IS-04 asks.
    std::string Grandmaster;
    bool Traceable = false; // Ptp: the grandmaster is traceable to TAI
    bool Locked = false;    // Ptp: the node follows the grandmaster; false: not yet

    friend bool operator==(const Clock&, const Clock&) = default;
};

// The program's function that chooses another registry when the node's registry keeps
// failing: Failures polls in a row got no answer, or an error. It returns the base URL of
// another registry, with which the node then registers from scratch, or nothing to stay
// with the current one. The node calls it from Poll(), on that thread; one that throws,
// or returns a URL longer than the C API takes, keeps the node where it is.
using RegistryFailedFunction =
    std::function<std::optional<std::string>(uint32_t Failures)>;

// How a node is opened.
struct NodeConfig
{
    DtNmos::Id
        Id; // The node's ID; keep it the same across runs, e.g. with Id::FromName()
    std::string Label;
    std::string Description;
    std::string Hostname;
    // The address at which controllers reach the node's APIs. "": the address of this
    // host on the route to the registry, or, with Search, on the default route.
    std::string ApiHost;
    uint16_t ApiPort = 0; // The port of the APIs; 0 lets Serve() pick a free one
    // The base URL of the registry, e.g. "http://registry.local". "": use Search.
    std::string RegistrationUrl;
    std::string ApiVersion; // The IS-04 version; "" for "v1.3", the only one supported
    HttpFunction Http;      // Sends the requests to the registry, e.g. CurlHttp. Required
    uint32_t TimeoutMs = 0; // How long a request to the registry may take; 5000 when 0
    uint32_t HeartbeatMs = 0; // Time between heartbeats to the registry; 5000 when 0
    LogFunction Log;          // Receives log messages; may be empty
    RegistryFailedFunction RegistryFailed; // Chooses another registry; may be empty
    // How many polls in a row must fail before the node moves on. 0: 3, or 1 for a node
    // that uses Search, as IS-04 asks.
    uint32_t FailuresBeforeSwitch = 0;
    // The search for registries the node uses when it has no RegistrationUrl; none
    // without one. The node borrows it until it is closed or destroyed, and the program
    // keeps it until then. The node uses the most preferred registry found. When that one
    // fails, the node asks RegistryFailed if set, and otherwise moves to the next
    // registry that has not failed yet; after all have failed, it starts again from the
    // first.
    const RegistrySearch* Search = nullptr;
    std::optional<DtNmos::Clock> Clock; // The node's clock from the start; none: internal
};

// How a device is added.
struct DeviceConfig
{
    DtNmos::Id Id;     // The device's ID; keep it the same across runs
    std::string Label; // e.g. "DTA-2110 2110000076 port 1"
    std::string Description;
};

// How a sender is added.
struct SenderConfig
{
    DtNmos::Id Id;       // The sender's ID; keep it the same across runs
    DtNmos::Id DeviceId; // The device it belongs to
    std::string Label;
    std::string Description;
    // What it sends, video or audio. The node serves it as the sender's SDP. A reference
    // clock of RefClockKind::LocalMac gets the MAC address of SourceIp's interface.
    DtNmos::Flow Flow;
    // The address it sends from, e.g. that of a card's network port. Required. The SDP
    // gives it as the origin and the source filter.
    std::string SourceIp;
    // How long the program needs to apply an activation, in milliseconds. The function
    // of a scheduled activation is called this much early. 0: at the time itself.
    uint32_t ActivationLeadMs = 0;
};

// How a receiver is added.
struct ReceiverConfig
{
    DtNmos::Id Id;       // The receiver's ID; keep it the same across runs
    DtNmos::Id DeviceId; // The device it belongs to
    std::string Label;
    std::string Description;
    DtNmos::Media Media = DtNmos::Media::None; // What it receives: Video or Audio
    // The address it receives on, e.g. that of a card's network port. Required.
    std::string InterfaceIp;
    uint32_t ActivationLeadMs = 0; // As in SenderConfig
    // The stream the receiver receives from the start, until a controller connects it,
    // which the node gives as its active transport parameters. "": any source, or no
    // group, which is unicast to InterfaceIp.
    std::string SourceIp;         // The one source it takes
    std::string MulticastIp;      // The group it has joined
    uint16_t DestinationPort = 0; // The UDP port it receives on; 0 for 5004
};

// What a controller asks of a receiver: whether to receive, and which stream.
//
// With a transport file (an SDP), Flow is the flow in it of the receiver's media, with
// the address, source and port the controller set. Without one, HasFlow is false and
// Flow gives only the transport: its format is an empty one of the receiver's media, and
// the destination, source and port are those the controller set, so that the receiver
// keeps its format and only moves to the new stream.
struct ReceiverActivation
{
    bool MasterEnable = false; // False: stop receiving
    bool HasFlow = false;      // True: Flow comes from a transport file and has a format
    DtNmos::Flow Flow;         // The stream to receive
    DtNmos::Id SenderId;       // The sender the controller connects, or empty
    // When the change takes effect, in nanoseconds of TAI since the PTP epoch: the time a
    // scheduled activation asks for, or now for an immediate one. A function called
    // ActivationLeadMs early may wait until then.
    uint64_t AtNs = 0;

    friend bool operator==(const ReceiverActivation&,
                           const ReceiverActivation&) = default;
};

// What a controller asks of a sender: whether to send, and where to. "auto" in the
// controller's request is already replaced by the current destination and the SourceIp
// of the sender's config.
struct SenderActivation
{
    bool MasterEnable = false;    // False: stop sending
    std::string DestinationIp;    // Where to send to
    uint16_t DestinationPort = 0; // UDP port to send to
    std::string SourceIp;         // Where to send from
    uint64_t AtNs = 0;            // As in ReceiverActivation

    friend bool operator==(const SenderActivation&, const SenderActivation&) = default;
};

// The program's function that applies a controller's activation of a receiver or sender.
// It returns a Status: the node passes the message of an Error to the controller, which
// gets 500. One that throws fails with the exception's message.
//
// The node calls it for an immediate activation on the thread that handles the request,
// and for a scheduled one in Poll(), ActivationLeadMs before it is due. It may block for
// as long as applying takes; the node's lock is not held. Meanwhile, another request for
// the same sender or receiver is answered with 423 (locked), and the function is never
// called twice at once for one sender or receiver. The new parameters become active at
// AtNs, or when the function returns if that is later. A failed scheduled activation is
// logged. The node keeps the function, and what it captured, until the sender or
// receiver is removed or the node closes.
using ReceiverActivate =
    std::function<Status(const Id& Receiver, const ReceiverActivation& Activation)>;

using SenderActivate =
    std::function<Status(const Id& Sender, const SenderActivation& Activation)>;

// Returns whether the library was built with the HTTP server, so that Node::Serve()
// works.
bool HasServer();

namespace Detail
{

// What the node keeps of the function of a sender or receiver: the function, and the
// device the sender or receiver belongs to, so that removing the device frees it too.
// The node owns it; the C library is given its address.
struct ActivateEntry
{
    DtNmos::Id DeviceId;
    SenderActivate Sender;
    ReceiverActivate Receiver;
};

// An entry whose function runs on this thread, and where Node::Remove() puts it when it
// removes the entry's sender or receiver from within that function, so that the call
// frees it when the function has returned. Outer is the one whose function runs around
// this one, as when a function has a request handled on its thread.
struct RunningEntry
{
    const ActivateEntry* Entry;
    std::unique_ptr<ActivateEntry>* Taken;
    RunningEntry* Outer;
};

// The innermost entry whose function runs on this thread, or null.
inline thread_local RunningEntry* Running = nullptr;

// Frees Entry, unless its function runs on this thread: it then goes to that call.
inline void FreeEntry(std::unique_ptr<ActivateEntry> Entry)
{
    for (RunningEntry* r = Running; r != nullptr; r = r->Outer)
    {
        if (r->Entry == Entry.get())
        {
            *r->Taken = std::move(Entry);
            return;
        }
    }
}

// What a Node keeps beside the C node: the functions the C node calls, the search it
// borrows, and the entries of its senders and receivers by ID, which Mutex guards. It
// returns the search when it is freed, which is after the C node.
struct NodeState
{
    HttpFunction Http;
    LogFunction Log;
    RegistryFailedFunction RegistryFailed;
    RegistrySearchState* Search = nullptr;
    std::mutex Mutex;
    std::map<DtNmos::Id, std::unique_ptr<ActivateEntry>> Entries;

    NodeState() = default;
    NodeState(const NodeState&) = delete;
    NodeState& operator=(const NodeState&) = delete;
    ~NodeState() { ReturnSearch(); }

    // Ends the borrowing of the search, once the C node is closed.
    void ReturnSearch()
    {
        if (Search != nullptr)
        {
            --Search->Borrowers;
            Search = nullptr;
        }
    }
};

// Frees a DtNmosNode, which closes it, for a std::unique_ptr.
struct NodeFree
{
    void operator()(DtNmosNode* Node) const { DtNmosNode_Free(Node); }
};

} // namespace Detail

// A node, open from Open() until it is destroyed, or moved from. It is moved, not copied.
//
// Every function fails with Result::State, or returns 0, false or an empty value, for a
// node that is closed or moved from.
class Node
{
  public:
    // Opens a node with Config. Nothing is registered until the node is polled. Fails
    // with Result::InvalidArgument when Config has no Id or no Http, neither a
    // RegistrationUrl nor a Search, or a clock that the C API refuses.
    [[nodiscard]] static Expected<Node> Open(const NodeConfig& Config);

    Node(Node&&) noexcept = default;
    // Closes and frees this node before it takes Other's.
    Node& operator=(Node&& Other) noexcept;
    // Closes and frees the C node before the functions it calls: State is declared
    // first, so that it is freed last.
    ~Node() = default;

    // Adds a device. The next poll registers it. Fails with Result::InvalidArgument when
    // the node already has the ID.
    [[nodiscard]] Status AddDevice(const DeviceConfig& Device);

    // Adds a receiver to a device; the node calls Activate when a controller activates
    // it. The next poll registers it. Fails with Result::InvalidArgument when the node
    // already has the ID, does not have the device, or the config has no InterfaceIp.
    [[nodiscard]] Status AddReceiver(const ReceiverConfig& Receiver,
                                     ReceiverActivate Activate);

    // Adds a sender to a device; the node calls Activate when a controller activates it.
    // The next poll registers it. Fails with Result::InvalidArgument when the node
    // already has the ID, does not have the device, the flow is neither video nor audio,
    // the config has no SourceIp, or a text of the flow is too long for the C API.
    [[nodiscard]] Status AddSender(const SenderConfig& Sender, SenderActivate Activate);

    // Returns the port at which the node's APIs are reached: ApiPort, or the port Serve()
    // picked.
    uint16_t ApiPort() const;

    // Returns the base URL of the node's APIs, e.g. "http://192.168.1.5:8080".
    [[nodiscard]] Expected<std::string> ApiUrl() const;

    // Closes the node: stops serving, unregisters everything from the registry, and
    // forgets all devices, senders and receivers, and their functions. The node is not
    // opened again; open a new one.
    [[nodiscard]] Status Close();

    // Returns the node's ID, from its config.
    Id GetId() const;

    // Answers a request to the Node API or the Connection API, for a program that runs an
    // HTTP server of its own instead of Serve(). Request.Url is the path and query. The
    // answer is also one with an error status. Stop that server before the node is
    // destroyed.
    [[nodiscard]] Expected<HttpResponse> Handle(const HttpRequest& Request);

    // Returns whether the registry has the node and all its devices, senders and
    // receivers.
    bool IsRegistered() const;

    // Does the node's periodic work: applies scheduled activations that are due,
    // registers what is not registered yet, unregisters what was removed, and sends a
    // heartbeat when one is due. Serve() calls it on its own thread; a program without it
    // calls it itself. Returns how long until the node wants to be polled again. Having
    // no registry is not a failure; a request that failed is, and the next poll tries
    // again.
    [[nodiscard]] Expected<std::chrono::milliseconds> Poll();

    // Removes a device, sender or receiver; removing a device also removes its senders
    // and receivers. The next poll unregisters them. Returns when no function of what it
    // removes runs: an activation being applied is waited for, and none starts
    // meanwhile; the functions are then freed. So do not call it while holding a lock
    // that such a function takes. Called from the function of what it removes, it does
    // not wait for that function. Fails with Result::NotFound when the node has no such
    // ID.
    [[nodiscard]] Status Remove(const Id& Resource);

    // Serves the Node API and the Connection API at ApiHost and ApiPort, and polls the
    // node on a thread of its own, until the node closes. Fails with Result::State when
    // the library was built without the server (see HasServer()), and Result::Http when
    // the server cannot listen at the address and port.
    [[nodiscard]] Status Serve();

    // Sets the node's clock, clk0, which every source names, and registers the node again
    // when it changed. Setting the clock the node has changes nothing, so a program may
    // set it each time it checks its clock. Fails with Result::InvalidArgument when its
    // Kind is not Internal or Ptp, or a Ptp clock's Grandmaster is not an EUI-64.
    [[nodiscard]] Status SetClock(const Clock& NodeClock);

    // Replaces the flow of a sender, e.g. after its format changed. Its SDP changes with
    // it, and the next poll registers the new version. Fails with Result::NotFound when
    // the node has no such sender, and Result::InvalidArgument when the flow is of
    // another media than the sender's, or a text of it is too long for the C API.
    [[nodiscard]] Status UpdateSender(const Id& Sender, const Flow& SenderFlow);

  private:
    Node() = default;

    [[nodiscard]] Status CheckOpen() const;

    std::unique_ptr<Detail::NodeState> State;
    std::unique_ptr<DtNmosNode, Detail::NodeFree> Native;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= What the wrapper shares +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//

namespace Detail
{

// Converts a clock kind of the C API, as FromNative(DtNmosResult) does.
inline ClockKind FromNative(DtNmosClockKind Native)
{
    switch (Native)
    {
    case DTNMOS_CLOCK_NONE:
        return ClockKind::None;
    case DTNMOS_CLOCK_INTERNAL:
        return ClockKind::Internal;
    case DTNMOS_CLOCK_PTP:
        return ClockKind::Ptp;
    }
    return static_cast<ClockKind>(Native);
}

inline DtNmosClockKind ToNative(ClockKind Value)
{
    return static_cast<DtNmosClockKind>(Value);
}

// Converts a clock of the C API.
inline Clock FromNative(const DtNmosClock& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosClock, Locked);
    Clock Value;
    Value.Kind = FromNative(Native.Kind);
    Value.Grandmaster = FromArray(Native.Grandmaster);
    Value.Traceable = Native.Traceable;
    Value.Locked = Native.Locked;
    return Value;
}

// Converts a clock to the C API. Fails with Result::InvalidArgument when its Grandmaster
// is too long.
inline Expected<DtNmosClock> ToNative(const Clock& Value)
{
    DtNmosClock Native{};
    Native.Size = sizeof(Native);
    Native.Kind = ToNative(Value.Kind);
    Native.Traceable = Value.Traceable;
    Native.Locked = Value.Locked;
    const Status Copied =
        CopyText(Native.Grandmaster, Value.Grandmaster, "Clock.Grandmaster");
    if (!Copied)
    {
        return std::unexpected(Copied.error());
    }
    return Native;
}

// Converts an activation of a sender of the C API.
inline SenderActivation FromNative(const DtNmosSenderActivation& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosSenderActivation, AtNs);
    SenderActivation Value;
    Value.MasterEnable = Native.MasterEnable;
    Value.DestinationIp = FromArray(Native.DestinationIp);
    Value.DestinationPort = Native.DestinationPort;
    Value.SourceIp = FromArray(Native.SourceIp);
    Value.AtNs = Native.AtNs;
    return Value;
}

// Converts an activation of a receiver of the C API.
inline ReceiverActivation FromNative(const DtNmosReceiverActivation& Native)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosReceiverActivation, AtNs);
    ReceiverActivation Value;
    Value.MasterEnable = Native.MasterEnable;
    Value.HasFlow = Native.HasFlow;
    Value.Flow = FromNative(Native.Flow);
    Value.SenderId = Access::FromNative(Native.SenderId);
    Value.AtNs = Native.AtNs;
    return Value;
}

// The C callback of the function of a receiver, whose entry User points to.
inline DtNmosResult
ReceiverTrampoline(void* User, const DtNmosId* Receiver,
                   const DtNmosReceiverActivation* Activation) noexcept
{
    const ActivateEntry* Entry = static_cast<const ActivateEntry*>(User);
    std::unique_ptr<ActivateEntry> Taken;
    RunningEntry Here{Entry, &Taken, Running};
    Running = &Here;
    const Status Done = Guard(Result::Internal,
                              [&] {
                                  return Entry->Receiver(Access::FromNative(*Receiver),
                                                         FromNative(*Activation));
                              });
    Running = Here.Outer;
    return Fail(Done, Result::Internal);
}

// The C callback of the function of a sender, whose entry User points to.
inline DtNmosResult SenderTrampoline(void* User, const DtNmosId* Sender,
                                     const DtNmosSenderActivation* Activation) noexcept
{
    const ActivateEntry* Entry = static_cast<const ActivateEntry*>(User);
    std::unique_ptr<ActivateEntry> Taken;
    RunningEntry Here{Entry, &Taken, Running};
    Running = &Here;
    const Status Done = Guard(
        Result::Internal, [&]
        { return Entry->Sender(Access::FromNative(*Sender), FromNative(*Activation)); });
    Running = Here.Outer;
    return Fail(Done, Result::Internal);
}

// The C callback of a RegistryFailedFunction, which User points to: writes the URL it
// returns into NextUrl, of Size bytes.
inline bool RegistryFailedTrampoline(void* User, uint32_t Failures, char* NextUrl,
                                     size_t Size) noexcept
{
    const RegistryFailedFunction& Choose =
        *static_cast<const RegistryFailedFunction*>(User);
    std::optional<std::string> Next;
    const Status Done = Guard(Result::Internal, [&] { Next = Choose(Failures); });
    if (!Done || !Next.has_value() || Next->size() >= Size)
    {
        return false;
    }
    Next->copy(NextUrl, Next->size());
    NextUrl[Next->size()] = '\0';
    return true;
}

} // namespace Detail

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definitions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasServer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline bool HasServer()
{
    return DtNmos_HasServer();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The node keeps the functions and the search the C node calls; the C node copies the
// rest during the call.
//
inline Expected<Node> Node::Open(const NodeConfig& Config)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosNodeConfig, Clock);
    Node Made;
    Made.State = std::make_unique<Detail::NodeState>();
    Detail::NodeState& Kept = *Made.State;
    Kept.Http = Config.Http;
    Kept.Log = Config.Log;
    Kept.RegistryFailed = Config.RegistryFailed;

    DtNmosNodeConfig NativeConfig{};
    NativeConfig.Size = sizeof(NativeConfig);
    NativeConfig.Id = Detail::Access::ToNative(Config.Id);
    NativeConfig.Label = Config.Label.c_str();
    NativeConfig.Description = Config.Description.c_str();
    NativeConfig.Hostname = Config.Hostname.c_str();
    NativeConfig.ApiHost = Detail::NullIfEmpty(Config.ApiHost);
    NativeConfig.ApiPort = Config.ApiPort;
    NativeConfig.RegistrationUrl = Detail::NullIfEmpty(Config.RegistrationUrl);
    NativeConfig.ApiVersion = Detail::NullIfEmpty(Config.ApiVersion);
    const Detail::NativeHttp Http = Detail::ToNative(Kept.Http);
    NativeConfig.Http = Http.Function;
    NativeConfig.HttpUser = Http.User;
    NativeConfig.TimeoutMs = Config.TimeoutMs;
    NativeConfig.HeartbeatMs = Config.HeartbeatMs;
    NativeConfig.Log = Kept.Log ? Detail::LogTrampoline : nullptr;
    NativeConfig.LogUser = &Kept.Log;
    NativeConfig.RegistryFailed =
        Kept.RegistryFailed ? Detail::RegistryFailedTrampoline : nullptr;
    NativeConfig.RegistryFailedUser = &Kept.RegistryFailed;
    NativeConfig.FailuresBeforeSwitch = Config.FailuresBeforeSwitch;
    NativeConfig.Search = Config.Search != nullptr ? Config.Search->GetNative() : nullptr;
    if (Config.Search != nullptr && Config.Search->State != nullptr)
    {
        // Borrowed until the node returns it, also when it does not open.
        Kept.Search = Config.Search->State.get();
        ++Kept.Search->Borrowers;
    }
    DtNmosClock NativeClock{};
    if (Config.Clock.has_value())
    {
        const Expected<DtNmosClock> Converted = Detail::ToNative(*Config.Clock);
        if (!Converted)
        {
            return std::unexpected(Converted.error());
        }
        NativeClock = *Converted;
        NativeConfig.Clock = &NativeClock;
    }
    Made.Native.reset(DtNmosNode_Alloc());
    if (Made.Native == nullptr)
    {
        return std::unexpected(Error{Result::NoMemory, "Out of memory."});
    }
    const Status Opened =
        Detail::Check(DtNmosNode_Open(Made.Native.get(), &NativeConfig));
    if (!Opened)
    {
        return std::unexpected(Opened.error());
    }
    return Made;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::= -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The C node goes first, as in the destructor: a member-wise move would free the
// functions while the C node may still call them.
//
inline Node& Node::operator=(Node&& Other) noexcept
{
    if (this != &Other)
    {
        Native.reset();
        State = std::move(Other.State);
        Native = std::move(Other.Native);
    }
    return *this;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::AddDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Status Node::AddDevice(const DeviceConfig& Device)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosDeviceConfig, Description);
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    DtNmosDeviceConfig Config{};
    Config.Size = sizeof(Config);
    Config.Id = Detail::Access::ToNative(Device.Id);
    Config.Label = Device.Label.c_str();
    Config.Description = Device.Description.c_str();
    return Detail::Check(DtNmosNode_AddDevice(Native.get(), &Config));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::AddReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The entry of the function goes into the node's entries once the C node has the
// receiver; until then this function keeps it.
//
inline Status Node::AddReceiver(const ReceiverConfig& Receiver, ReceiverActivate Activate)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosReceiverConfig, DestinationPort);
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    DtNmosReceiverConfig Config{};
    Config.Size = sizeof(Config);
    Config.Id = Detail::Access::ToNative(Receiver.Id);
    Config.DeviceId = Detail::Access::ToNative(Receiver.DeviceId);
    Config.Label = Receiver.Label.c_str();
    Config.Description = Receiver.Description.c_str();
    Config.Media = Detail::ToNative(Receiver.Media);
    Config.InterfaceIp = Receiver.InterfaceIp.c_str();
    Config.ActivationLeadMs = Receiver.ActivationLeadMs;
    Config.SourceIp = Detail::NullIfEmpty(Receiver.SourceIp);
    Config.MulticastIp = Detail::NullIfEmpty(Receiver.MulticastIp);
    Config.DestinationPort = Receiver.DestinationPort;
    auto Entry = std::make_unique<Detail::ActivateEntry>();
    Entry->DeviceId = Receiver.DeviceId;
    Entry->Receiver = std::move(Activate);
    const Status Added = Detail::Check(DtNmosNode_AddReceiver(
        Native.get(), &Config, Entry->Receiver ? Detail::ReceiverTrampoline : nullptr,
        Entry.get()));
    if (Added)
    {
        const std::lock_guard<std::mutex> Lock(State->Mutex);
        State->Entries[Receiver.Id] = std::move(Entry);
    }
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// As AddReceiver().
//
inline Status Node::AddSender(const SenderConfig& Sender, SenderActivate Activate)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosSenderConfig, ActivationLeadMs);
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    Detail::NativeFlow Flow;
    const Status Converted = Flow.Set(Sender.Flow);
    if (!Converted)
    {
        return Converted;
    }
    DtNmosSenderConfig Config{};
    Config.Size = sizeof(Config);
    Config.Id = Detail::Access::ToNative(Sender.Id);
    Config.DeviceId = Detail::Access::ToNative(Sender.DeviceId);
    Config.Label = Sender.Label.c_str();
    Config.Description = Sender.Description.c_str();
    Config.Flow = &Flow.Get();
    Config.SourceIp = Sender.SourceIp.c_str();
    Config.ActivationLeadMs = Sender.ActivationLeadMs;
    auto Entry = std::make_unique<Detail::ActivateEntry>();
    Entry->DeviceId = Sender.DeviceId;
    Entry->Sender = std::move(Activate);
    const Status Added = Detail::Check(DtNmosNode_AddSender(
        Native.get(), &Config, Entry->Sender ? Detail::SenderTrampoline : nullptr,
        Entry.get()));
    if (Added)
    {
        const std::lock_guard<std::mutex> Lock(State->Mutex);
        State->Entries[Sender.Id] = std::move(Entry);
    }
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::ApiPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline uint16_t Node::ApiPort() const
{
    return Native != nullptr ? DtNmosNode_ApiPort(Native.get()) : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::ApiUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C function is asked for the size first, and then writes into a buffer of it.
//
inline Expected<std::string> Node::ApiUrl() const
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    std::size_t Size = 0;
    const DtNmosResult Asked = DtNmosNode_ApiUrl(Native.get(), nullptr, &Size);
    if (Asked != DTNMOS_E_BUFFER_TOO_SMALL)
    {
        return std::unexpected(
            Detail::LastError(Asked == DTNMOS_OK ? DTNMOS_E_INTERNAL : Asked));
    }
    std::string Url(Size, '\0');
    Size = Url.size();
    const Status Written =
        Detail::Check(DtNmosNode_ApiUrl(Native.get(), Url.data(), &Size));
    if (!Written)
    {
        return std::unexpected(Written.error());
    }
    Url.resize(Size);
    return Url;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A closed node keeps its C node, which then fails by itself; only a moved-from one has
// none.
//
inline Status Node::CheckOpen() const
{
    if (Native == nullptr)
    {
        return std::unexpected(
            Error{Result::State, "The node was moved to another, and is not open."});
    }
    return {};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Once the C node is closed, no function of it runs, and the entries go.
//
inline Status Node::Close()
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    const Status Closed = Detail::Check(DtNmosNode_Close(Native.get()));
    const std::lock_guard<std::mutex> Lock(State->Mutex);
    for (auto& Item : State->Entries)
    {
        Detail::FreeEntry(std::move(Item.second));
    }
    State->Entries.clear();
    State->ReturnSearch();
    return Closed;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::GetId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Id Node::GetId() const
{
    DtNmosId Made{};
    if (Native == nullptr || DtNmosNode_Id(Native.get(), &Made) != DTNMOS_OK)
    {
        return Id();
    }
    return Detail::Access::FromNative(Made);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<HttpResponse> Node::Handle(const HttpRequest& Request)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    const std::unique_ptr<DtNmosHttpResponse, Detail::HttpResponseFree> Response(
        DtNmosHttpResponse_Alloc());
    if (Response == nullptr)
    {
        return std::unexpected(Error{Result::NoMemory, "Out of memory."});
    }
    const DtNmosHttpRequest NativeRequest = Detail::ToNative(Request);
    const Status Answered =
        Detail::Check(DtNmosNode_Handle(Native.get(), &NativeRequest, Response.get()));
    if (!Answered)
    {
        return std::unexpected(Answered.error());
    }
    return Detail::FromNative(Response.get());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::IsRegistered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline bool Node::IsRegistered() const
{
    return Native != nullptr && DtNmosNode_IsRegistered(Native.get());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Expected<std::chrono::milliseconds> Node::Poll()
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return std::unexpected(Open.error());
    }
    uint32_t NextMs = 0;
    const Status Polled = Detail::Check(DtNmosNode_Poll(Native.get(), &NextMs));
    if (!Polled)
    {
        return std::unexpected(Polled.error());
    }
    return std::chrono::milliseconds(NextMs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::Remove -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The C node waits for the functions of what it removes, without the lock of the
// entries, which such a function may need; the entries of the resource, or of the
// senders and receivers of the device, go after it.
//
inline Status Node::Remove(const Id& Resource)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    const DtNmosId NativeId = Detail::Access::ToNative(Resource);
    const Status Removed = Detail::Check(DtNmosNode_Remove(Native.get(), &NativeId));
    if (Removed)
    {
        const std::lock_guard<std::mutex> Lock(State->Mutex);
        for (auto Item = State->Entries.begin(); Item != State->Entries.end();)
        {
            const bool Goes =
                Item->first == Resource || Item->second->DeviceId == Resource;
            if (Goes)
            {
                Detail::FreeEntry(std::move(Item->second));
            }
            Item = Goes ? State->Entries.erase(Item) : std::next(Item);
        }
    }
    return Removed;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::Serve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
inline Status Node::Serve()
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    return Detail::Check(DtNmosNode_Serve(Native.get()));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::SetClock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Status Node::SetClock(const Clock& NodeClock)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    const Expected<DtNmosClock> Converted = Detail::ToNative(NodeClock);
    if (!Converted)
    {
        return std::unexpected(Converted.error());
    }
    return Detail::Check(DtNmosNode_SetClock(Native.get(), &*Converted));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::UpdateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
inline Status Node::UpdateSender(const Id& Sender, const Flow& SenderFlow)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    Detail::NativeFlow Converted;
    const Status Set = Converted.Set(SenderFlow);
    if (!Set)
    {
        return Set;
    }
    const DtNmosId NativeId = Detail::Access::ToNative(Sender);
    return Detail::Check(
        DtNmosNode_UpdateSender(Native.get(), &NativeId, &Converted.Get()));
}

} // namespace DtNmos
