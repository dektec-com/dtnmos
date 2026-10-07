// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_node.hpp *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The C++ API of an NMOS node: registration (IS-04 v1.3) and connection
// management (IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The C++ API of dtnmos_node.h. A node is the program as NMOS sees it. It holds devices,
// and each device holds senders and receivers. The node registers them with an NMOS
// registry, where controllers find them. It also serves the Node API and the Connection
// API, through which a controller connects a sender to a receiver. A program uses a node
// in these steps:
//
// 1. Node::Open() opens it, with the registry or a RegistrySearch, and the node's ID.
// 2. AddDevice() adds a device. AddSender() and AddReceiver() add senders and receivers
//    to it, each with a function that the node calls when a controller activates it.
// 3. Serve() serves the APIs and keeps the registration up to date, on a thread of its
//    own. A program with an HTTP server of its own calls Handle() for each request, and
//    Poll() regularly, instead.
// 4. Destroying the node closes it, and that unregisters everything.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
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

// The kind of clock a node has. The values are those of DtNmosClockKind.
enum class ClockKind : int
{
    None = DTNMOS_CLOCK_NONE,         // Refused
    Internal = DTNMOS_CLOCK_INTERNAL, // No external reference
    Ptp = DTNMOS_CLOCK_PTP            // A PTP grandmaster
};

// The clock of a node. IS-04 calls it clk0, and every source of the node names it.
struct Clock
{
    ClockKind Kind = ClockKind::None;
    // For Ptp: the grandmaster's EUI-64. It is eight pairs of hexadecimal digits joined
    // by '-', in upper or lower case, e.g. "00-1B-19-FF-FE-00-00-01". The node writes it
    // in lower case, as IS-04 asks.
    std::string Grandmaster;
    bool Traceable = false; // For Ptp: true when the grandmaster is traceable to TAI
    bool Locked = false;    // For Ptp: true when the node follows the grandmaster

    friend bool operator==(const Clock&, const Clock&) = default;
};

// The program's function that chooses another registry when the node's registry keeps
// failing.
//
// The node calls it from Poll(), on the thread of Poll(), when Failures polls in a row
// got no answer or an error. The function returns the base URL of another registry, and
// the node then registers with that registry from the start. To keep the current
// registry, the function returns nothing. The node also keeps it when the function
// throws, or returns a URL that is longer than the C API takes.
using RegistryFailedFunction =
    std::function<std::optional<std::string>(uint32_t Failures)>;

// How a node is opened.
struct NodeConfig
{
    // The node's ID. Keep it the same across runs, e.g. by making it with Id::FromName().
    DtNmos::Id Id;
    std::string Label;
    std::string Description;
    std::string Hostname;
    // The address at which controllers reach the node's APIs. With "", the node takes
    // the address of this host on the route to the registry, or, with Search, on the
    // default route.
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
    // The search for registries that the node uses when it has no RegistrationUrl, or
    // nullptr. The node borrows the search until the node is closed or destroyed, so the
    // program must keep the search until then. The node registers with the most
    // preferred registry that the search found. When that registry fails, the node asks
    // RegistryFailed, if it is set. Otherwise the node moves to the next registry that
    // has not failed yet. After all of them have failed, it starts again with the first.
    const RegistrySearch* Search = nullptr;
    // The clock the node starts with. Without one, the node's clock is internal.
    std::optional<DtNmos::Clock> Clock;
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
    // The stream it sends, video or audio. The node serves it as the sender's SDP. When
    // its reference clock is RefClockKind::LocalMac, the node fills in the MAC address of
    // the interface that has SourceIp.
    DtNmos::Flow Flow;
    // The address it sends from, e.g. that of a card's network port. Required. The SDP
    // gives it as the origin and the source filter.
    std::string SourceIp;
    // How long the program needs to apply an activation, in milliseconds. The node calls
    // the function of a scheduled activation this much early. With 0, it calls it at the
    // time of the activation.
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
    // The stream the receiver receives until a controller connects it. The node gives
    // this stream as the receiver's active transport parameters. A SourceIp of "" means
    // any source, and a MulticastIp of "" means unicast to InterfaceIp.
    std::string SourceIp;         // The one source it takes
    std::string MulticastIp;      // The group it has joined
    uint16_t DestinationPort = 0; // The UDP port it receives on; 0 for 5004
};

// What a controller asks of a receiver: whether to receive, and which stream.
//
// When the controller gives a transport file (an SDP), Flow is the flow from that file
// that has the receiver's media. Its address, source and port are those the controller
// set. Without a transport file, HasFlow is false, and Flow holds only the destination,
// source and port that the controller set. Its format is then empty, so that the
// receiver keeps its format and only moves to the new stream.
struct ReceiverActivation
{
    bool MasterEnable = false; // False: stop receiving
    bool HasFlow = false;      // True: Flow comes from a transport file and has a format
    DtNmos::Flow Flow;         // The stream to receive
    DtNmos::Id SenderId;       // The sender the controller connects, or empty
    // When the change takes effect, in nanoseconds of TAI since the PTP epoch. For a
    // scheduled activation it is the time the controller asked for; for an immediate one
    // it is now. A function that the node calls ActivationLeadMs early may wait until
    // then.
    uint64_t AtNs = 0;

    friend bool operator==(const ReceiverActivation&,
                           const ReceiverActivation&) = default;
};

// What a controller asks of a sender: whether to send, and where to. Where the
// controller's request says "auto", the node has already filled in the sender's current
// destination and the SourceIp of its config.
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
//
// The function returns a Status. When it returns an Error, or throws, the controller
// gets 500 with the error's or the exception's message.
//
// For an immediate activation, the node calls the function on the thread that handles
// the request. For a scheduled one, it calls the function in Poll(), ActivationLeadMs
// before the activation is due. The function may block for as long as applying takes,
// as the node does not hold its lock. Meanwhile, the node answers another request for
// the same sender or receiver with 423 (locked), and it never calls the function twice
// at once for one sender or receiver. The new parameters become active at AtNs, or when
// the function returns, whichever is later. The node logs a scheduled activation that
// fails.
//
// The node frees the function, with what it captured, when it will not call it again.
// That is after the sender or receiver is removed, or the node is closed, and after the
// last call of the function that was running has returned.
using ReceiverActivate =
    std::function<Status(const Id& Receiver, const ReceiverActivation& Activation)>;

using SenderActivate =
    std::function<Status(const Id& Sender, const SenderActivation& Activation)>;

// Returns whether the library was built with the HTTP server, so that Node::Serve()
// works.
bool HasServer();

namespace Detail
{

// The function of a sender or receiver, which the C node gets as User. The C node owns
// the entry once it has added the sender or receiver. It frees the entry through
// ReleaseEntry() when it will not call the function again.
struct ActivateEntry
{
    SenderActivate Sender;
    ReceiverActivate Receiver;
};

// Frees the ActivateEntry that User points to. The C node calls it as the ReleaseUser of
// the sender or receiver.
inline void ReleaseEntry(void* User) noexcept
{
    const std::unique_ptr<ActivateEntry> Released(static_cast<ActivateEntry*>(User));
}

// What a Node keeps beside the C node: the functions that the C node calls, and the
// search that the node borrows. Destroying the state gives the search back. The Node
// destroys it after the C node.
struct NodeState
{
    HttpFunction Http;
    LogFunction Log;
    RegistryFailedFunction RegistryFailed;
    RegistrySearchState* Search = nullptr;

    NodeState() = default;
    NodeState(const NodeState&) = delete;
    NodeState& operator=(const NodeState&) = delete;
    ~NodeState() { ReturnSearch(); }

    // Gives the borrowed search back. The Node calls it once the C node is closed.
    void ReturnSearch()
    {
        if (Search != nullptr)
        {
            --Search->Borrowers;
            Search = nullptr;
        }
    }
};

// Frees a DtNmosNode, which closes it. It is the deleter of a std::unique_ptr.
struct NodeFree
{
    void operator()(DtNmosNode* Node) const { DtNmosNode_Free(Node); }
};

} // namespace Detail

// A node. Open() opens it, and destroying it closes it. A node is moved, not copied.
//
// Every function fails with Result::State, or returns 0, false or an empty value, when
// the node is closed or was moved from.
class Node
{
  public:
    // Opens a node with Config. The node registers nothing until it is polled. Fails with
    // Result::InvalidArgument when Config has no Id or no Http, has neither a
    // RegistrationUrl nor a Search, or has a clock that the C API refuses.
    [[nodiscard]] static Expected<Node> Open(const NodeConfig& Config);

    Node(Node&&) noexcept = default;
    // Closes and frees this node, and then takes the node of Other.
    Node& operator=(Node&& Other) noexcept;
    // Closes and frees the C node, and then the functions that the C node calls. State is
    // declared first, so that it is freed last.
    ~Node() = default;

    // Adds a device. The next poll registers it. Fails with Result::InvalidArgument when
    // the node already has a device, sender or receiver with the ID.
    [[nodiscard]] Status AddDevice(const DeviceConfig& Device);

    // Adds a receiver to a device. The node calls Activate when a controller activates
    // the receiver, and the next poll registers it. Fails with Result::InvalidArgument
    // when the node already has the ID, does not have the device, or the config has no
    // InterfaceIp.
    [[nodiscard]] Status AddReceiver(const ReceiverConfig& Receiver,
                                     ReceiverActivate Activate);

    // Adds a sender to a device. The node calls Activate when a controller activates the
    // sender, and the next poll registers it. Fails with Result::InvalidArgument when the
    // node already has the ID, does not have the device, the flow is neither video nor
    // audio, the config has no SourceIp, or a text in the flow is too long for the C API.
    [[nodiscard]] Status AddSender(const SenderConfig& Sender, SenderActivate Activate);

    // Returns the port at which controllers reach the node's APIs. It is ApiPort, or the
    // port that Serve() picked.
    uint16_t ApiPort() const;

    // Returns the base URL of the node's APIs, e.g. "http://192.168.1.5:8080".
    [[nodiscard]] Expected<std::string> ApiUrl() const;

    // Closes the node. It stops serving, unregisters everything from the registry, and
    // forgets all devices, senders and receivers. The node frees their functions. A
    // closed node cannot be opened again; open a new one.
    [[nodiscard]] Status Close();

    // Returns the node's ID, from its config.
    Id GetId() const;

    // Answers a request to the Node API or the Connection API. A program uses it when it
    // runs an HTTP server of its own instead of Serve(). Request.Url holds the path and
    // the query. An answer with an error status is an answer too, not a failure. Stop
    // that server before the node is destroyed.
    [[nodiscard]] Expected<HttpResponse> Handle(const HttpRequest& Request);

    // Returns whether the registry has the node and all its devices, senders and
    // receivers.
    bool IsRegistered() const;

    // Does the node's periodic work. It applies the scheduled activations that are due,
    // registers what is not registered yet, unregisters what was removed, and sends a
    // heartbeat when one is due. Serve() calls it on a thread of its own; a program that
    // does not use Serve() calls it itself. Returns how long the node can wait for the
    // next poll. Having no registry is not a failure. A request that failed is a failure,
    // and the next poll tries again.
    [[nodiscard]] Expected<std::chrono::milliseconds> Poll();

    // Removes a device, sender or receiver. Removing a device also removes its senders
    // and receivers. The next poll unregisters them.
    //
    // Remove() returns at once. An activation that is running may still end after
    // Remove() returns, but no new activation starts. The node frees the function of a
    // removed sender or receiver when no call of it runs. When no call runs, that happens
    // inside Remove(). A function may remove its own sender or receiver.
    //
    // Fails with Result::NotFound when the node has no such ID.
    [[nodiscard]] Status Remove(const Id& Resource);

    // Serves the Node API and the Connection API at ApiHost and ApiPort, and polls the
    // node on a thread of its own, until the node closes. Fails with Result::State when
    // the library was built without the server (see HasServer()), and with Result::Http
    // when the server cannot listen at the address and port.
    [[nodiscard]] Status Serve();

    // Sets the node's clock, clk0, which every source names. When the clock changed, the
    // node registers again. Setting the clock that the node already has changes nothing,
    // so a program may set it each time it checks its clock. Fails with
    // Result::InvalidArgument when its Kind is not Internal or Ptp, or when the
    // Grandmaster of a Ptp clock is not an EUI-64.
    [[nodiscard]] Status SetClock(const Clock& NodeClock);

    // Replaces the flow of a sender, e.g. after its format changed. The sender's SDP
    // changes with it, and the next poll registers the new version. Fails with
    // Result::NotFound when the node has no such sender, and with Result::InvalidArgument
    // when the flow carries another media than the sender's, or when a text in it is too
    // long for the C API.
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

// Converts a clock kind from the C API, as FromNative(DtNmosResult) does.
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

// Converts a clock from the C API.
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

// Converts a sender's activation from the C API.
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

// Converts a receiver's activation from the C API.
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

// Calls the program's function for a receiver's activation. The C node calls it; User
// points to the receiver's ActivateEntry.
inline DtNmosResult
ReceiverTrampoline(void* User, const DtNmosId* Receiver,
                   const DtNmosReceiverActivation* Activation) noexcept
{
    const ActivateEntry* Entry = static_cast<const ActivateEntry*>(User);
    const Status Done = Guard(Result::Internal,
                              [&] {
                                  return Entry->Receiver(Access::FromNative(*Receiver),
                                                         FromNative(*Activation));
                              });
    return Fail(Done, Result::Internal);
}

// Calls the program's function for a sender's activation. The C node calls it; User
// points to the sender's ActivateEntry.
inline DtNmosResult SenderTrampoline(void* User, const DtNmosId* Sender,
                                     const DtNmosSenderActivation* Activation) noexcept
{
    const ActivateEntry* Entry = static_cast<const ActivateEntry*>(User);
    const Status Done = Guard(
        Result::Internal, [&]
        { return Entry->Sender(Access::FromNative(*Sender), FromNative(*Activation)); });
    return Fail(Done, Result::Internal);
}

// Calls the program's RegistryFailedFunction, which User points to. The C node calls
// it. It writes the URL that the function returns into NextUrl, of Size bytes, and
// returns false when there is none.
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
// The node keeps the functions that the C node calls, and the search. The C node copies
// the rest of the config during the call.
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
        // The node borrows the search until it gives it back, also when it fails to open.
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
// The C node is freed first, as in the destructor. A member-wise move would free the
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
// The C node owns the entry of the function once it has added the receiver, and
// releases it later. Until then, this function owns the entry, and frees it when the C
// node refuses the receiver.
//
inline Status Node::AddReceiver(const ReceiverConfig& Receiver, ReceiverActivate Activate)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosReceiverConfig, ReleaseUser);
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
    Entry->Receiver = std::move(Activate);
    Config.ReleaseUser = Detail::ReleaseEntry;
    const Status Added = Detail::Check(DtNmosNode_AddReceiver(
        Native.get(), &Config, Entry->Receiver ? Detail::ReceiverTrampoline : nullptr,
        Entry.get()));
    if (Added)
    {
        // The C node owns the entry now, and releases it later.
        (void)Entry.release();
    }
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Node::AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// As AddReceiver().
//
inline Status Node::AddSender(const SenderConfig& Sender, SenderActivate Activate)
{
    DTNMOS_DETAIL_LAST_FIELD(DtNmosSenderConfig, ReleaseUser);
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
    Entry->Sender = std::move(Activate);
    Config.ReleaseUser = Detail::ReleaseEntry;
    const Status Added = Detail::Check(DtNmosNode_AddSender(
        Native.get(), &Config, Entry->Sender ? Detail::SenderTrampoline : nullptr,
        Entry.get()));
    if (Added)
    {
        // The C node owns the entry now, and releases it.
        (void)Entry.release();
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
// The C function first tells the size it needs, and then writes into a buffer of that
// size.
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
// A closed node keeps its C node, and the C node fails its calls by itself. Only a node
// that was moved from has no C node.
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
// The C node releases the entries of the functions as it closes. After that, the C node
// does not use the search, and the node gives the search back.
//
inline Status Node::Close()
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    const Status Closed = Detail::Check(DtNmosNode_Close(Native.get()));
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
// The C node releases the entry of a removed function when no call of it runs.
//
inline Status Node::Remove(const Id& Resource)
{
    const Status Open = CheckOpen();
    if (!Open)
    {
        return Open;
    }
    const DtNmosId NativeId = Detail::Access::ToNative(Resource);
    return Detail::Check(DtNmosNode_Remove(Native.get(), &NativeId));
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
