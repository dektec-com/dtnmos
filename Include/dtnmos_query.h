// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_query.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A registry asked, watched and controlled: IS-04 v1.3 Query, IS-05 and DNS-SD
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdint.h>

#include "dtnmos.h"
#include "dtnmos_http.h"
#include "dtnmos_sdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= The Query API (IS-04 v1.3) +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct DtNmosQuery DtNmosQuery;

typedef struct DtNmosQueryConfig
{
    size_t Size;             // sizeof(DtNmosQueryConfig)
    const char* RegistryUrl; // base URL of the registry, e.g. "http://registry.local"
    const char*
        ApiVersion;      // of the Query API; "v1.3" when null, the only one accepted yet
    DtNmosHttpFunc Http; // e.g. DtNmos_CurlHttp
    void* HttpUser;
    uint32_t TimeoutMs; // of each request; 5000 when 0
    DtNmosLogFunc Log;  // optional
    void* LogUser;
} DtNmosQueryConfig;

// A sender as the registry lists it. Its strings belong to the list it is in and stay
// valid until that list is freed; one the registry leaves out is empty, never null.
typedef struct DtNmosSenderInfo
{
    DtNmosId Id;
    DtNmosId FlowId; // empty when the sender has no flow
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    DtNmosMedia Media;        // from its flow; DTNMOS_MEDIA_OTHER without one
    const char* Transport;    // e.g. "urn:x-nmos:transport:rtp.mcast"
    const char* ManifestHref; // the URL of its SDP; empty when it has none
} DtNmosSenderInfo;

// Senders of a registry, which the list owns with their strings. What finds or reads a
// single sender returns a list of that one, at index 0.
typedef struct DtNmosSenderList DtNmosSenderList;

// A receiver as the registry lists it. Its strings belong to the list it is in and stay
// valid until that list is freed; one the registry leaves out is empty, never null.
typedef struct DtNmosReceiverInfo
{
    DtNmosId Id;
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    DtNmosMedia Media;     // from its format; DTNMOS_MEDIA_OTHER for another one
    const char* Transport; // e.g. "urn:x-nmos:transport:rtp"
    DtNmosId SenderId;     // the sender it is subscribed to; empty when none
    int Active;            // whether that subscription is active
} DtNmosReceiverInfo;

// Receivers of a registry, which the list owns with their strings. What finds a single
// receiver returns a list of that one, at index 0.
typedef struct DtNmosReceiverList DtNmosReceiverList;

// A function of a query other than _Alloc(), _Open(), _Free() and _Freep(), and one of
// the controller, needs an open query, and fails with DTNMOS_E_STATE on another.

// Allocates a query, closed. Returns null when the memory ran out.
DTNMOS_API DtNmosQuery* DtNmosQuery_Alloc(void);

// Forgets the registry, leaving the query closed. Fails with DTNMOS_E_STATE when the
// query is not open. A subscription made with it must be closed first.
DTNMOS_API DtNmosResult DtNmosQuery_Close(DtNmosQuery* Query);

// Finds the receiver whose ID is IdOrLabel when it is a UUID, or else whose label it
// is, as DtNmosQuery_FindSender() finds a sender, with the same errors.
DTNMOS_API DtNmosResult DtNmosQuery_FindReceiver(DtNmosQuery* Query,
                                                 const char* IdOrLabel,
                                                 DtNmosReceiverList** Found);

// Finds the sender whose ID is IdOrLabel when it is a UUID, or else whose label it
// is, and returns it in a list of one, which the caller frees. Fails with
// DTNMOS_E_NOT_FOUND, or DTNMOS_E_AMBIGUOUS when senders share the label, the message
// listing their IDs.
DTNMOS_API DtNmosResult DtNmosQuery_FindSender(DtNmosQuery* Query, const char* IdOrLabel,
                                               DtNmosSenderList** Found);

// Closes the query when it is open, and frees it. Null is allowed.
DTNMOS_API void DtNmosQuery_Free(DtNmosQuery* Query);

// Frees *Query as DtNmosQuery_Free() does and sets *Query to null. Null is allowed.
DTNMOS_API void DtNmosQuery_Freep(DtNmosQuery** Query);

// Opens query on the registry of config, whose strings it copies. Fails with
// DTNMOS_E_INVALID_ARGUMENT without a registry URL or HTTP function, or with another
// API version than v1.3, and with DTNMOS_E_STATE when the query is open. A query closed
// can be opened again, on another registry, keeping its handle.
DTNMOS_API DtNmosResult DtNmosQuery_Open(DtNmosQuery* Query,
                                         const DtNmosQueryConfig* Config);

// Lists the receivers of the registry, following its paging.
DTNMOS_API DtNmosResult DtNmosQuery_Receivers(DtNmosQuery* Query,
                                              DtNmosReceiverList** List);

// Fetches the SDP of sender from its manifest_href into the caller's buffer of *Size
// bytes, with a terminating null; *Size is then its length. Its size is known only once
// it is fetched, so a buffer too small fails with DTNMOS_E_BUFFER_TOO_SMALL, *Size then
// giving the bytes needed, and a second call fetches it again; DtNmosQuery_SenderSdp()
// fetches and parses it at once. Fails with DTNMOS_E_NOT_FOUND when the sender has no
// manifest.
DTNMOS_API DtNmosResult DtNmosQuery_SenderManifest(DtNmosQuery* Query,
                                                   const DtNmosSenderInfo* Sender,
                                                   char* Buffer, size_t* Size);

// Lists the senders of the registry, following its paging, each with the media of its
// flow.
DTNMOS_API DtNmosResult DtNmosQuery_Senders(DtNmosQuery* Query, DtNmosSenderList** List);

// Fetches the SDP of sender and parses it as DtNmosSdp_Parse() does.
DTNMOS_API DtNmosResult DtNmosQuery_SenderSdp(DtNmosQuery* Query,
                                              const DtNmosSenderInfo* Sender,
                                              DtNmosSdp** Sdp);

// Returns the receiver at Index, from 0, or null past the end of the list.
DTNMOS_API const DtNmosReceiverInfo* DtNmosReceiverList_At(const DtNmosReceiverList* List,
                                                           size_t Index);

// Returns the number of receivers in the list; 0 for null.
DTNMOS_API size_t DtNmosReceiverList_Count(const DtNmosReceiverList* List);

// Frees the list and the strings of its receivers. Null is allowed.
DTNMOS_API void DtNmosReceiverList_Free(DtNmosReceiverList* List);

// Returns the sender at Index, from 0, or null past the end of the list.
DTNMOS_API const DtNmosSenderInfo* DtNmosSenderList_At(const DtNmosSenderList* List,
                                                       size_t Index);

// Returns the number of senders in the list; 0 for null.
DTNMOS_API size_t DtNmosSenderList_Count(const DtNmosSenderList* List);

// Frees the list and the strings of its senders. Null is allowed.
DTNMOS_API void DtNmosSenderList_Free(DtNmosSenderList* List);

// +=+=+=+=+=+=+=+=+= A controller that connects receivers (IS-05 v1.1) +=+=+=+=+=+=+=+=+=

// What DtNmosQuery_Connect() connected: the receiver and the sender as the registry lists
// them, and the SDP of the sender that the receiver was given. It owns them, and what
// its functions return stays valid until DtNmosConnection_Free().
typedef struct DtNmosConnection DtNmosConnection;

// Frees the connection and what it holds. Null is allowed.
DTNMOS_API void DtNmosConnection_Free(DtNmosConnection* Connection);

// Returns the receiver that was connected.
DTNMOS_API const DtNmosReceiverInfo*
DtNmosConnection_Receiver(const DtNmosConnection* Connection);

// Returns the SDP of the sender, which the receiver was given as its transport file.
DTNMOS_API const char* DtNmosConnection_Sdp(const DtNmosConnection* Connection);

// Returns the sender the receiver was connected to.
DTNMOS_API const DtNmosSenderInfo*
DtNmosConnection_Sender(const DtNmosConnection* Connection);

// Connects the receiver of the registry of query to a sender, each given by its ID or
// its label, as a controller of IS-05 does: it finds the Connection API of the receiver
// through the control urn:x-nmos:control:sr-ctrl/v1.1 of its device, and activates at
// once its staged parameters with the sender, master_enable true and the SDP of the
// sender as its transport file. The requests to the node go through the HTTP function
// of the query, with its timeout. connection, when not null, receives what was
// connected, which the caller frees; it is set to null when the connect fails.
//
// Fails as DtNmosQuery_FindReceiver() and DtNmosQuery_FindSender() do; with
// DTNMOS_E_INVALID_ARGUMENT when the flow of the sender is of another kind than the
// format of the receiver, video, audio or data; with DTNMOS_E_NOT_FOUND when the
// device has no such control or the sender no SDP; and with DTNMOS_E_HTTP when the node
// answers with another status than 200, the message holding the error it gave.
DTNMOS_API DtNmosResult DtNmosQuery_Connect(DtNmosQuery* Query, const char* Receiver,
                                            const char* Sender,
                                            DtNmosConnection** Connection);

// Disconnects the receiver of the registry of query, given by its ID or label: activates
// at once its staged parameters with master_enable false and no sender. disconnected,
// when not null, receives the receiver as the registry lists it, in a list of one which
// the caller frees. Fails as DtNmosQuery_Connect() does.
DTNMOS_API DtNmosResult DtNmosQuery_Disconnect(DtNmosQuery* Query, const char* Receiver,
                                               DtNmosReceiverList** Disconnected);

// Moves a sender of the registry of query, given by its ID or label, to DestinationIp
// and DestinationPort, as a controller of IS-05 does: through the Connection API of the
// sender, found as DtNmosQuery_Connect() finds that of a receiver, it activates at once
// the staged parameters of its leg with the new destination. moved, when not null,
// receives the sender as the registry lists it, in a list of one which the caller frees.
// Fails as DtNmosQuery_Connect() does, and with DTNMOS_E_INVALID_ARGUMENT for a sender of
// another transport than RTP.
DTNMOS_API DtNmosResult DtNmosQuery_MoveSender(DtNmosQuery* Query, const char* Sender,
                                               const char* DestinationIp,
                                               uint16_t DestinationPort,
                                               DtNmosSenderList** Moved);

// +=+=+=+=+=+=+=+=+=+= A subscription to the resources of a registry +=+=+=+=+=+=+=+=+=+=

// What happened to a resource.
typedef enum DtNmosChangeKind
{
    DTNMOS_CHANGE_PRESENT = 0, // it is there: the first message of a subscription
    DTNMOS_CHANGE_ADDED = 1,
    DTNMOS_CHANGE_MODIFIED = 2,
    DTNMOS_CHANGE_REMOVED = 3
} DtNmosChangeKind;

// A change of one resource, valid during the call it is passed to: its ID, and its JSON
// before and after, each null when the resource was not there.
typedef struct DtNmosChange
{
    DtNmosChangeKind Kind;
    const char* Id;
    const char* Pre;
    size_t PreLength;
    const char* Post;
    size_t PostLength;
} DtNmosChange;

typedef void (*DtNmosChangeFunc)(void* User, const DtNmosChange* Change);

typedef struct DtNmosSubscriptionConfig
{
    size_t Size;              // sizeof(DtNmosSubscriptionConfig)
    const char* ResourcePath; // e.g. "/senders"
    uint32_t MaxUpdateRateMs; // how often the registry sends at most; 100 when 0
    // The WebSocket; null takes DtNmos_CurlWebSocket().
    const DtNmosWebSocketTransport* WebSocket;
    DtNmosChangeFunc OnChange; // called by DtNmosSubscription_Poll()
    void* OnChangeUser;
} DtNmosSubscriptionConfig;

typedef struct DtNmosSubscription DtNmosSubscription;

// Returns the name of a kind, e.g. "added"; a static string.
DTNMOS_API const char* DtNmosChangeKind_Name(DtNmosChangeKind Kind);

// Reads the JSON of a sender, as a change gives it, into a list of one, which the caller
// frees. Its media stays DTNMOS_MEDIA_OTHER: it is a property of its flow. Fails with
// DTNMOS_E_PARSE for what is no JSON object.
DTNMOS_API DtNmosResult DtNmosSenderInfo_Parse(const char* Json, size_t Length,
                                               DtNmosSenderList** Sender);

// Allocates a subscription, closed. Returns null when the memory ran out.
DTNMOS_API DtNmosSubscription* DtNmosSubscription_Alloc(void);

// Closes the WebSocket, leaving the subscription closed; the registry drops a
// subscription that is not persistent when its last WebSocket closes. Fails with
// DTNMOS_E_STATE when the subscription is not open.
DTNMOS_API DtNmosResult DtNmosSubscription_Close(DtNmosSubscription* Subscription);

// Closes the subscription when it is open, and frees it. Null is allowed.
DTNMOS_API void DtNmosSubscription_Free(DtNmosSubscription* Subscription);

// Frees *Subscription as DtNmosSubscription_Free() does and sets *Subscription to null.
// Null is allowed.
DTNMOS_API void DtNmosSubscription_Freep(DtNmosSubscription** Subscription);

// Opens subscription: asks the registry of query, which is open, for a subscription to
// the resources at Config->ResourcePath, through the HTTP function of the query, and
// connects to its WebSocket, with the timeout of the query. The query must stay open
// while the subscription is. Fails with DTNMOS_E_STATE when the subscription is open or
// the query is not, as the requests of the query do, with DTNMOS_E_PARSE when the answer
// names no WebSocket, and as the connect of the WebSocket does.
DTNMOS_API DtNmosResult DtNmosSubscription_Open(DtNmosSubscription* Subscription,
                                                DtNmosQuery* Query,
                                                const DtNmosSubscriptionConfig* Config);

// Waits at most TimeoutMs for a message, and calls OnChange for each change in it, on
// the thread of the caller. The first message holds every resource as it is, each a
// DTNMOS_CHANGE_PRESENT. Fails with DTNMOS_E_TIMEOUT when no message came, with
// DTNMOS_E_PARSE for a message it cannot read, after which it can be polled again, and
// with DTNMOS_E_NETWORK when the WebSocket closed or failed, after which a new
// subscription starts again from the first message.
DTNMOS_API DtNmosResult DtNmosSubscription_Poll(DtNmosSubscription* Subscription,
                                                uint32_t TimeoutMs);

// The URL of the WebSocket of the subscription.
DTNMOS_API const char* DtNmosSubscription_Url(const DtNmosSubscription* Subscription);

// +=+=+=+=+=+=+=+=+=+=+=+=+= Finding registries through DNS-SD +=+=+=+=+=+=+=+=+=+=+=+=+=

typedef enum DtNmosService
{
    DTNMOS_SERVICE_QUERY,        // _nmos-query._tcp, the Query API
    DTNMOS_SERVICE_REGISTRATION, // _nmos-register._tcp, the Registration API
} DtNmosService;

// How a registry is found: through multicast DNS on the link, or through the DNS server
// of the network in its domain (unicast DNS-SD). The searches of a config or them
// together.
typedef enum DtNmosSearch
{
    DTNMOS_SEARCH_MULTICAST = 1, // multicast DNS, in the domain local
    DTNMOS_SEARCH_UNICAST = 2,   // the DNS server of the host, in its domain
} DtNmosSearch;

typedef struct DtNmosDiscoveryConfig
{
    size_t Size; // sizeof(DtNmosDiscoveryConfig)
    DtNmosService Service;
    // The IPv4 address of the interface the query leaves through; null takes that of the
    // default route.
    const char* InterfaceAddress;
    // Where the query goes, "<IPv4 address>:<port>"; null is "224.0.0.251:5353". A test
    // gives a responder of its own.
    const char* Destination;
    uint32_t TimeoutMs; // how long answers are collected; 1000 when 0
    DtNmosLogFunc Log;  // optional
    void* LogUser;
    // The searches made at the same time, DTNMOS_SEARCH_MULTICAST and
    // DTNMOS_SEARCH_UNICAST or-ed; 0 makes both.
    unsigned Searches;
    // The DNS server of the unicast search, "<IPv4 address>:<port>"; null takes the first
    // IPv4 DNS server of the host, at port 53.
    const char* DnsServer;
    // The domain the unicast search browses, e.g. "example.com"; null takes the one the
    // host searches. Without a server or a domain, only multicast DNS is asked.
    const char* DnsDomain;
} DtNmosDiscoveryConfig;

// A Query or Registration API that a registry announces. Its strings that are no arrays
// belong to the list it is in and stay valid until that list is freed. A string an
// announcement leaves out is empty, never null.
typedef struct DtNmosRegistryInfo
{
    DtNmosService Service;
    const char* Instance; // the name of the service instance, e.g. "Registry 1"
    // The host of its SRV record, e.g. "registry-1.local".
    char Host[DTNMOS_MAX_ADDRESS_SIZE];
    char Address[DTNMOS_MAX_ADDRESS_SIZE]; // the IPv4 address of host, when given
    uint16_t Port;
    // The base URL of the API: "<api_proto>://<address>:<port>", with the host instead of
    // the address for https, or when the answers gave no address.
    const char* Url;
    char ApiProto[DTNMOS_MAX_SHORT_SIZE]; // "http" or "https"
    const char* ApiVersions;              // e.g. "v1.2,v1.3"
    int Priority; // pri: lower is preferred, 100 and up are for development;
                  // -1 when the announcement has none
    int Auth;     // api_auth is true: the API asks for authorization (IS-10)
    // The API offers v1.3 over http or https without authorization, as dtnmos can use it.
    int Usable;
    DtNmosSearch FoundBy; // the search that found it
} DtNmosRegistryInfo;

// The registries a search found, which the list owns with their strings: the usable ones
// first, each in the
// order of priority, those without a priority last, those of the DNS server before those
// of multicast DNS, then by instance name. IS-04 has a client take one at random among
// those of the same priority, which is for the caller.
typedef struct DtNmosRegistryList DtNmosRegistryList;

// Searches for the registries of Config->Service, through multicast DNS and the DNS
// server at the same time, from one socket: sends the query three times within the
// timeout, the one to the DNS server until it answers, collects the answers until it
// ends, and asks once more, within half the timeout again, for the records the answers
// left out; the DNS server one question per query. Finding none is no failure: the list
// is then empty. Fails with DTNMOS_E_INVALID_ARGUMENT for a malformed address or domain,
// and with DTNMOS_E_NETWORK when the socket cannot be opened or the query of multicast
// DNS cannot be sent; a DNS server that cannot be reached is only logged.
DTNMOS_API DtNmosResult DtNmos_Discover(const DtNmosDiscoveryConfig* Config,
                                        DtNmosRegistryList** List);

// Returns the registry at Index, from 0, or null past the end of the list.
DTNMOS_API const DtNmosRegistryInfo* DtNmosRegistryList_At(const DtNmosRegistryList* List,
                                                           size_t Index);

// Returns the number of registries in the list; 0 for null.
DTNMOS_API size_t DtNmosRegistryList_Count(const DtNmosRegistryList* List);

// Frees the list and the strings of its registries. Null is allowed.
DTNMOS_API void DtNmosRegistryList_Free(DtNmosRegistryList* List);

#ifdef __cplusplus
}
#endif
