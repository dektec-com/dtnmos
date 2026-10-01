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

// A sender as the registry lists it. Strings the registry leaves out are empty.
typedef struct DtNmosSenderInfo
{
    DtNmosId Id;
    DtNmosId FlowId; // empty when the sender has no flow
    DtNmosId DeviceId;
    DtNmosString Label;
    DtNmosString Description;
    DtNmosMedia Media;         // from its flow; DTNMOS_MEDIA_OTHER without one
    DtNmosString Transport;    // e.g. "urn:x-nmos:transport:rtp.mcast"
    DtNmosString ManifestHref; // the URL of its SDP; empty when it has none
} DtNmosSenderInfo;

DTNMOS_API void DtNmosSenderInfo_Clear(DtNmosSenderInfo* sender);
DTNMOS_API DtNmosResult DtNmosSenderInfo_Copy(DtNmosSenderInfo* target,
                                              const DtNmosSenderInfo* source);

// The senders of a registry, which the list owns.
typedef struct DtNmosSenderList DtNmosSenderList;

DTNMOS_API size_t DtNmosSenderList_Count(const DtNmosSenderList* list);
DTNMOS_API const DtNmosSenderInfo* DtNmosSenderList_At(const DtNmosSenderList* list,
                                                       size_t index);
DTNMOS_API void DtNmosSenderList_Free(DtNmosSenderList* list);

// Creates a query of the registry of config, whose strings it copies. Fails with
// DTNMOS_E_INVALID_ARGUMENT without a registry URL or HTTP function, or with another
// API version than v1.3.
DTNMOS_API DtNmosResult DtNmosQuery_Create(const DtNmosQueryConfig* config,
                                           DtNmosQuery** query, DtNmosError* error);
DTNMOS_API void DtNmosQuery_Destroy(DtNmosQuery* query);

// Lists the senders of the registry, following its paging, each with the media of its
// flow.
DTNMOS_API DtNmosResult DtNmosQuery_Senders(DtNmosQuery* query, DtNmosSenderList** list,
                                            DtNmosError* error);

// Finds the sender whose ID is id_or_label when it is a UUID, or else whose label it
// is, into sender, which is cleared first and which the caller clears. Fails with
// DTNMOS_E_NOT_FOUND, or DTNMOS_E_AMBIGUOUS when senders share the label, the message
// listing their IDs.
DTNMOS_API DtNmosResult DtNmosQuery_FindSender(DtNmosQuery* query,
                                               const char* id_or_label,
                                               DtNmosSenderInfo* sender,
                                               DtNmosError* error);

// Fetches the SDP of sender from its manifest_href into text, which is cleared first.
// Fails with DTNMOS_E_NOT_FOUND when the sender has no manifest.
DTNMOS_API DtNmosResult DtNmosQuery_SenderManifest(DtNmosQuery* query,
                                                   const DtNmosSenderInfo* sender,
                                                   DtNmosString* text,
                                                   DtNmosError* error);

// Fetches the SDP of sender and parses it as DtNmosSdp_Parse() does.
DTNMOS_API DtNmosResult DtNmosQuery_SenderSdp(DtNmosQuery* query,
                                              const DtNmosSenderInfo* sender,
                                              DtNmosSdp** sdp, DtNmosError* error);

// A receiver as the registry lists it. Strings the registry leaves out are empty.
typedef struct DtNmosReceiverInfo
{
    DtNmosId Id;
    DtNmosId DeviceId;
    DtNmosString Label;
    DtNmosString Description;
    DtNmosMedia Media;      // from its format; DTNMOS_MEDIA_OTHER for another one
    DtNmosString Transport; // e.g. "urn:x-nmos:transport:rtp"
    DtNmosId SenderId;      // the sender it is subscribed to; empty when none
    int Active;             // whether that subscription is active
} DtNmosReceiverInfo;

DTNMOS_API void DtNmosReceiverInfo_Clear(DtNmosReceiverInfo* receiver);
DTNMOS_API DtNmosResult DtNmosReceiverInfo_Copy(DtNmosReceiverInfo* target,
                                                const DtNmosReceiverInfo* source);

// The receivers of a registry, which the list owns.
typedef struct DtNmosReceiverList DtNmosReceiverList;

DTNMOS_API size_t DtNmosReceiverList_Count(const DtNmosReceiverList* list);
DTNMOS_API const DtNmosReceiverInfo* DtNmosReceiverList_At(const DtNmosReceiverList* list,
                                                           size_t index);
DTNMOS_API void DtNmosReceiverList_Free(DtNmosReceiverList* list);

// Lists the receivers of the registry, following its paging.
DTNMOS_API DtNmosResult DtNmosQuery_Receivers(DtNmosQuery* query,
                                              DtNmosReceiverList** list,
                                              DtNmosError* error);

// Finds the receiver whose ID is id_or_label when it is a UUID, or else whose label it
// is, as DtNmosQuery_FindSender() finds a sender, with the same errors.
DTNMOS_API DtNmosResult DtNmosQuery_FindReceiver(DtNmosQuery* query,
                                                 const char* id_or_label,
                                                 DtNmosReceiverInfo* receiver,
                                                 DtNmosError* error);

// +=+=+=+=+=+=+=+=+= A controller that connects receivers (IS-05 v1.1) +=+=+=+=+=+=+=+=+=

// What DtNmosQuery_Connect() connected: the receiver and the sender as the registry lists
// them, and the SDP of the sender that the receiver was given. Set to zero, it is empty;
// DtNmosConnection_Clear() frees what it holds.
typedef struct DtNmosConnection
{
    DtNmosReceiverInfo Receiver;
    DtNmosSenderInfo Sender;
    DtNmosString Sdp;
} DtNmosConnection;

DTNMOS_API void DtNmosConnection_Clear(DtNmosConnection* connection);

// Connects the receiver of the registry of query to a sender, each given by its ID or
// its label, as a controller of IS-05 does: it finds the Connection API of the receiver
// through the control urn:x-nmos:control:sr-ctrl/v1.1 of its device, and activates at
// once its staged parameters with the sender, master_enable true and the SDP of the
// sender as its transport file. The requests to the node go through the HTTP function
// of the query, with its timeout. connection, when not null, is cleared first and
// receives what was connected.
//
// Fails as DtNmosQuery_FindReceiver() and DtNmosQuery_FindSender() do; with
// DTNMOS_E_INVALID_ARGUMENT when the flow of the sender is of another kind than the
// format of the receiver, video, audio or data; with DTNMOS_E_NOT_FOUND when the
// device has no such control or the sender no SDP; and with DTNMOS_E_HTTP when the node
// answers with another status than 200, the message holding the error it gave.
DTNMOS_API DtNmosResult DtNmosQuery_Connect(DtNmosQuery* query, const char* receiver,
                                            const char* sender,
                                            DtNmosConnection* connection,
                                            DtNmosError* error);

// Disconnects the receiver of the registry of query, given by its ID or label: activates
// at once its staged parameters with master_enable false and no sender. disconnected,
// when not null, is cleared first and receives the receiver as the registry lists it.
// Fails as DtNmosQuery_Connect() does.
DTNMOS_API DtNmosResult DtNmosQuery_Disconnect(DtNmosQuery* query, const char* receiver,
                                               DtNmosReceiverInfo* disconnected,
                                               DtNmosError* error);

// Moves a sender of the registry of query, given by its ID or label, to destination_ip
// and destination_port, as a controller of IS-05 does: through the Connection API of the
// sender, found as DtNmosQuery_Connect() finds that of a receiver, it activates at once
// the staged parameters of its leg with the new destination. moved, when not null, is
// cleared first and receives the sender as the registry lists it. Fails as
// DtNmosQuery_Connect() does, and with DTNMOS_E_INVALID_ARGUMENT for a sender of another
// transport than RTP.
DTNMOS_API DtNmosResult DtNmosQuery_MoveSender(DtNmosQuery* query, const char* sender,
                                               const char* destination_ip,
                                               uint16_t destination_port,
                                               DtNmosSenderInfo* moved,
                                               DtNmosError* error);

// +=+=+=+=+=+=+=+=+=+= A subscription to the resources of a registry +=+=+=+=+=+=+=+=+=+=

// What happened to a resource.
typedef enum DtNmosChangeKind
{
    DTNMOS_CHANGE_PRESENT = 0, // it is there: the first message of a subscription
    DTNMOS_CHANGE_ADDED = 1,
    DTNMOS_CHANGE_MODIFIED = 2,
    DTNMOS_CHANGE_REMOVED = 3
} DtNmosChangeKind;

// Returns the name of a kind, e.g. "added"; a static string.
DTNMOS_API const char* DtNmosChangeKind_Name(DtNmosChangeKind kind);

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

typedef void (*DtNmosChangeFunc)(void* user, const DtNmosChange* change);

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

// Asks the registry of query for a subscription to the resources at
// config->resource_path, through the HTTP function of the query, and connects to its
// WebSocket, with the timeout of the query. The query must outlive the subscription.
// Fails as the requests of the query do, with DTNMOS_E_PARSE when the answer names no
// WebSocket, and as the connect of the WebSocket does.
DTNMOS_API DtNmosResult DtNmosSubscription_Create(DtNmosQuery* query,
                                                  const DtNmosSubscriptionConfig* config,
                                                  DtNmosSubscription** subscription,
                                                  DtNmosError* error);

// Waits at most timeout_ms for a message, and calls on_change for each change in it, on
// the thread of the caller. The first message holds every resource as it is, each a
// DTNMOS_CHANGE_PRESENT. Fails with DTNMOS_E_TIMEOUT when no message came, with
// DTNMOS_E_PARSE for a message it cannot read, after which it can be polled again, and
// with DTNMOS_E_NETWORK when the WebSocket closed or failed, after which a new
// subscription starts again from the first message.
DTNMOS_API DtNmosResult DtNmosSubscription_Poll(DtNmosSubscription* subscription,
                                                uint32_t timeout_ms, DtNmosError* error);

// The URL of the WebSocket of the subscription.
DTNMOS_API const char* DtNmosSubscription_Url(const DtNmosSubscription* subscription);

// Closes the WebSocket and frees the subscription; the registry drops a subscription
// that is not persistent when its last WebSocket closes.
DTNMOS_API void DtNmosSubscription_Destroy(DtNmosSubscription* subscription);

// Reads the JSON of a sender, as a change gives it, into sender, which is cleared first
// and which the caller clears. Its media stays DTNMOS_MEDIA_OTHER: it is a property of
// its flow. Fails with DTNMOS_E_PARSE for what is no JSON object.
DTNMOS_API DtNmosResult DtNmosSenderInfo_Parse(const char* json, size_t length,
                                               DtNmosSenderInfo* sender,
                                               DtNmosError* error);

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

// A Query or Registration API that a registry announces. Strings an announcement leaves
// out are empty.
typedef struct DtNmosRegistryInfo
{
    DtNmosService Service;
    DtNmosString Instance; // the name of the service instance, e.g. "Registry 1"
    DtNmosString Host;     // the host of its SRV record, e.g. "registry-1.local"
    DtNmosString Address;  // the IPv4 address of host, when the answers gave it
    uint16_t Port;
    // The base URL of the API: "<api_proto>://<address>:<port>", with the host instead of
    // the address for https, or when the answers gave no address.
    DtNmosString Url;
    DtNmosString ApiProto;    // "http" or "https"
    DtNmosString ApiVersions; // e.g. "v1.2,v1.3"
    int Priority;             // pri: lower is preferred, 100 and up are for development;
                              // -1 when the announcement has none
    int Auth;                 // api_auth is true: the API asks for authorization (IS-10)
    // The API offers v1.3 over http or https without authorization, as dtnmos can use it.
    int Usable;
    DtNmosSearch FoundBy; // the search that found it
} DtNmosRegistryInfo;

DTNMOS_API void DtNmosRegistryInfo_Clear(DtNmosRegistryInfo* registry);
DTNMOS_API DtNmosResult DtNmosRegistryInfo_Copy(DtNmosRegistryInfo* target,
                                                const DtNmosRegistryInfo* source);

// The registries a search found, which the list owns: the usable ones first, each in the
// order of priority, those without a priority last, those of the DNS server before those
// of multicast DNS, then by instance name. IS-04 has a client take one at random among
// those of the same priority, which is for the caller.
typedef struct DtNmosRegistryList DtNmosRegistryList;

DTNMOS_API size_t DtNmosRegistryList_Count(const DtNmosRegistryList* list);
DTNMOS_API const DtNmosRegistryInfo* DtNmosRegistryList_At(const DtNmosRegistryList* list,
                                                           size_t index);
DTNMOS_API void DtNmosRegistryList_Free(DtNmosRegistryList* list);

// Searches for the registries of config->service, through multicast DNS and the DNS
// server at the same time, from one socket: sends the query three times within the
// timeout, the one to the DNS server until it answers, collects the answers until it
// ends, and asks once more, within half the timeout again, for the records the answers
// left out; the DNS server one question per query. Finding none is no failure: the list
// is then empty. Fails with DTNMOS_E_INVALID_ARGUMENT for a malformed address or domain,
// and with DTNMOS_E_NETWORK when the socket cannot be opened or the query of multicast
// DNS cannot be sent; a DNS server that cannot be reached is only logged.
DTNMOS_API DtNmosResult DtNmos_Discover(const DtNmosDiscoveryConfig* config,
                                        DtNmosRegistryList** list, DtNmosError* error);

#ifdef __cplusplus
}
#endif
