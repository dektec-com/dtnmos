// #*#*#*#*#*#*#*#*#*#*#*#*#*# dtnmos_query.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - A registry asked, watched and controlled: IS-04 v1.3 Query, IS-05 and DNS-SD
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The client side of NMOS. With these functions a program finds the registries on the
// network, asks a registry which senders and receivers there are, follows changes as they
// happen, and connects receivers to senders as a controller does.

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
//
// A DtNmosQuery asks one registry for its senders and receivers. Open it on the
// registry's URL; then list or find senders and receivers. The lists it returns are the
// program's to free.
//

// A connection to one registry, to ask it questions.
typedef struct DtNmosQuery DtNmosQuery;

// How a query is opened. Strings are copied.
typedef struct DtNmosQueryConfig
{
    size_t Size;             // sizeof(DtNmosQueryConfig)
    const char* RegistryUrl; // Base URL of the registry, e.g. "http://registry.local"
    const char* ApiVersion;  // Query API version; NULL for "v1.3", the only one supported
    DtNmosHttpFunc Http;     // Sends the requests, e.g. DtNmos_CurlHttp
    void* HttpUser;          // Passed to Http
    uint32_t TimeoutMs;      // How long a request may take; 5000 when 0
    DtNmosLogFunc Log;       // Receives log messages; may be NULL
    void* LogUser;           // Passed to Log
} DtNmosQueryConfig;

// A sender, as the registry lists it. Its strings belong to the list it is in, and are
// valid until that list is freed. A string the registry does not give is "", never NULL.
typedef struct DtNmosSenderInfo
{
    DtNmosId Id;
    DtNmosId FlowId; // The flow it sends; empty when it has none
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    DtNmosMedia Media;        // From its flow; DTNMOS_MEDIA_OTHER without one
    const char* Transport;    // e.g. "urn:x-nmos:transport:rtp.mcast"
    const char* ManifestHref; // The URL of its SDP; "" when it has none
} DtNmosSenderInfo;

// A list of senders. It owns them and their strings. A function that finds one sender
// returns a list of one.
typedef struct DtNmosSenderList DtNmosSenderList;

// A receiver, as the registry lists it. Its strings belong to the list it is in, and are
// valid until that list is freed. A string the registry does not give is "", never NULL.
typedef struct DtNmosReceiverInfo
{
    DtNmosId Id;
    DtNmosId DeviceId;
    const char* Label;
    const char* Description;
    DtNmosMedia Media;     // From its format; DTNMOS_MEDIA_OTHER for another format
    const char* Transport; // e.g. "urn:x-nmos:transport:rtp"
    DtNmosId SenderId;     // The sender it is connected to; empty when none
    bool Active;           // True when that connection is active
} DtNmosReceiverInfo;

// A list of receivers. It owns them and their strings. A function that finds one
// receiver returns a list of one.
typedef struct DtNmosReceiverList DtNmosReceiverList;

// Every function of a query except _Alloc, _Open, _Free and _Freep, and every function of
// the controller below, needs an open query, and returns DTNMOS_E_STATE for one that is
// not open.

// Creates a query, not yet open. Returns NULL when there is not enough memory.
DTNMOS_API DtNmosQuery* DtNmosQuery_Alloc(void);

// Closes Query. It may be opened again, on another registry. Close a subscription made
// with it first.
//
// Returns DTNMOS_OK, or DTNMOS_E_STATE when the query is not open.
DTNMOS_API DtNmosResult DtNmosQuery_Close(DtNmosQuery* Query);

// Finds a receiver by its ID or its label, as DtNmosQuery_FindSender() finds a sender,
// with the same results.
DTNMOS_API DtNmosResult DtNmosQuery_FindReceiver(DtNmosQuery* Query,
                                                 const char* IdOrLabel,
                                                 DtNmosReceiverList** Found);

// Finds a sender by its ID or its label: by ID when IdOrLabel is a UUID, by label
// otherwise. Sets *Found to a list with that one sender, for the program to free.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_NOT_FOUND  no sender has that ID or label
//   DTNMOS_E_AMBIGUOUS  several senders have that label; the message lists their IDs
// and the errors of the request.
DTNMOS_API DtNmosResult DtNmosQuery_FindSender(DtNmosQuery* Query, const char* IdOrLabel,
                                               DtNmosSenderList** Found);

// Closes Query if it is open, and frees it. NULL does nothing.
DTNMOS_API void DtNmosQuery_Free(DtNmosQuery* Query);

// Frees *Query, as DtNmosQuery_Free() does, and sets *Query to NULL. NULL does nothing.
DTNMOS_API void DtNmosQuery_Freep(DtNmosQuery** Query);

// Opens Query on the registry Config names. A closed query may be opened again.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_INVALID_ARGUMENT  no registry URL or Http, or an API version other than
//                              v1.3
//   DTNMOS_E_STATE             the query is already open
DTNMOS_API DtNmosResult DtNmosQuery_Open(DtNmosQuery* Query,
                                         const DtNmosQueryConfig* Config);

// Lists all receivers of the registry, also when the registry returns them page by page.
// Sets *List to the list, for the program to free.
DTNMOS_API DtNmosResult DtNmosQuery_Receivers(DtNmosQuery* Query,
                                              DtNmosReceiverList** List);

// Downloads the SDP of Sender, from its ManifestHref, into Buffer of *Size bytes, with
// its null, and sets *Size to its length. DtNmosQuery_SenderSdp() downloads and parses it
// in one call.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_NOT_FOUND         the sender has no SDP
//   DTNMOS_E_BUFFER_TOO_SMALL  Buffer is too small; *Size is the size needed. Call
//                              again with a larger buffer, which downloads it again
// and the errors of the request.
DTNMOS_API DtNmosResult DtNmosQuery_SenderManifest(DtNmosQuery* Query,
                                                   const DtNmosSenderInfo* Sender,
                                                   char* Buffer, size_t* Size);

// Lists all senders of the registry, with the media of each one's flow, also when the
// registry returns them page by page. Sets *List to the list, for the program to free.
DTNMOS_API DtNmosResult DtNmosQuery_Senders(DtNmosQuery* Query, DtNmosSenderList** List);

// Downloads the SDP of Sender and parses it, as DtNmosSdp_Parse() does, into *Sdp, for
// the program to free.
DTNMOS_API DtNmosResult DtNmosQuery_SenderSdp(DtNmosQuery* Query,
                                              const DtNmosSenderInfo* Sender,
                                              DtNmosSdp** Sdp);

// Returns receiver Index (from 0) of List, or NULL past the end.
DTNMOS_API const DtNmosReceiverInfo* DtNmosReceiverList_At(const DtNmosReceiverList* List,
                                                           size_t Index);

// Returns how many receivers List has; 0 for NULL.
DTNMOS_API size_t DtNmosReceiverList_Count(const DtNmosReceiverList* List);

// Frees List and its receivers. NULL does nothing.
DTNMOS_API void DtNmosReceiverList_Free(DtNmosReceiverList* List);

// Returns sender Index (from 0) of List, or NULL past the end.
DTNMOS_API const DtNmosSenderInfo* DtNmosSenderList_At(const DtNmosSenderList* List,
                                                       size_t Index);

// Returns how many senders List has; 0 for NULL.
DTNMOS_API size_t DtNmosSenderList_Count(const DtNmosSenderList* List);

// Frees List and its senders. NULL does nothing.
DTNMOS_API void DtNmosSenderList_Free(DtNmosSenderList* List);

// +=+=+=+=+=+=+=+=+= A controller that connects receivers (IS-05 v1.1) +=+=+=+=+=+=+=+=+=
//
// These functions do what an NMOS controller does: connect a receiver to a sender,
// disconnect it, or move a sender to another destination. They find the node's
// Connection API through the registry of the query, and send it the request.
//

// The result of DtNmosQuery_Connect(): the receiver and sender, as the registry lists
// them, and the SDP the receiver was given. What its functions return is valid until
// DtNmosConnection_Free().
typedef struct DtNmosConnection DtNmosConnection;

// Frees Connection. NULL does nothing.
DTNMOS_API void DtNmosConnection_Free(DtNmosConnection* Connection);

// Returns the receiver that was connected.
DTNMOS_API const DtNmosReceiverInfo*
DtNmosConnection_Receiver(const DtNmosConnection* Connection);

// Returns the sender's SDP, which the receiver was given as its transport file.
DTNMOS_API const char* DtNmosConnection_Sdp(const DtNmosConnection* Connection);

// Returns the sender the receiver was connected to.
DTNMOS_API const DtNmosSenderInfo*
DtNmosConnection_Sender(const DtNmosConnection* Connection);

// Connects a receiver to a sender, both given by ID or label: the receiver starts
// receiving what the sender sends. The node of the receiver is asked to activate it at
// once, with the sender's SDP. The request goes through the query's Http, with its
// timeout.
//
// When Connection is not NULL, *Connection gets the result, for the program to free; it
// is NULL after a failure.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_NOT_FOUND         the receiver or sender does not exist, the receiver's
//                              device has no Connection API, or the sender has no SDP
//   DTNMOS_E_AMBIGUOUS         a label names more than one
//   DTNMOS_E_INVALID_ARGUMENT  the sender sends another kind of media (video, audio or
//                              data) than the receiver receives
//   DTNMOS_E_HTTP              the node refused; the message holds its reason
// and the errors of the requests.
DTNMOS_API DtNmosResult DtNmosQuery_Connect(DtNmosQuery* Query, const char* Receiver,
                                            const char* Sender,
                                            DtNmosConnection** Connection);

// Disconnects a receiver, given by ID or label: it stops receiving. When Disconnected is
// not NULL, *Disconnected gets the receiver in a list of one, for the program to free.
// Returns the results of DtNmosQuery_Connect().
DTNMOS_API DtNmosResult DtNmosQuery_Disconnect(DtNmosQuery* Query, const char* Receiver,
                                               DtNmosReceiverList** Disconnected);

// Makes a sender, given by ID or label, send to DestinationIp and DestinationPort. The
// sender's node is asked to activate the new destination at once. When Moved is not
// NULL, *Moved gets the sender in a list of one, for the program to free.
//
// Returns the results of DtNmosQuery_Connect(), and DTNMOS_E_INVALID_ARGUMENT for a
// sender that does not send RTP.
DTNMOS_API DtNmosResult DtNmosQuery_MoveSender(DtNmosQuery* Query, const char* Sender,
                                               const char* DestinationIp,
                                               uint16_t DestinationPort,
                                               DtNmosSenderList** Moved);

// +=+=+=+=+=+=+=+=+=+= A subscription to the resources of a registry +=+=+=+=+=+=+=+=+=+=
//
// A subscription tells the program when resources in the registry change: when a sender
// is added, changed or removed, for example. Open it with a query; then call
// DtNmosSubscription_Poll() regularly, which calls the program's function for each
// change. The first poll reports every resource that is there.
//

// What happened to a resource.
typedef enum DtNmosChangeKind
{
    DTNMOS_CHANGE_PRESENT = 0, // It is there; the first poll reports every resource so
    DTNMOS_CHANGE_ADDED = 1,
    DTNMOS_CHANGE_MODIFIED = 2,
    DTNMOS_CHANGE_REMOVED = 3
} DtNmosChangeKind;

// One change of a resource. It is valid only during the call it is passed to.
typedef struct DtNmosChange
{
    DtNmosChangeKind Kind;
    const char* Id;    // The resource's ID
    const char* Pre;   // Its JSON before the change; NULL when it was not there
    size_t PreLength;  // Bytes in Pre
    const char* Post;  // Its JSON after the change; NULL when it is gone
    size_t PostLength; // Bytes in Post
} DtNmosChange;

// The program's function that receives each change, with the User it gave.
typedef void (*DtNmosChangeFunc)(void* User, const DtNmosChange* Change);

// How a subscription is opened.
typedef struct DtNmosSubscriptionConfig
{
    size_t Size;              // sizeof(DtNmosSubscriptionConfig)
    const char* ResourcePath; // Which resources, e.g. "/senders"
    uint32_t MaxUpdateRateMs; // The least time between two messages; 100 when 0
    // The WebSocket functions; NULL for DtNmos_CurlWebSocket().
    const DtNmosWebSocketTransport* WebSocket;
    DtNmosChangeFunc OnChange; // Called by DtNmosSubscription_Poll() for each change
    void* OnChangeUser;        // Passed to OnChange
} DtNmosSubscriptionConfig;

// A subscription to changes in a registry.
typedef struct DtNmosSubscription DtNmosSubscription;

// Returns the name of a kind of change, e.g. "added". Do not free the string.
DTNMOS_API const char* DtNmosChangeKind_Name(DtNmosChangeKind Kind);

// Reads a sender from its JSON, as a change gives it, into *Sender, a list of one for the
// program to free. Its Media is DTNMOS_MEDIA_OTHER, as the media is part of the flow, not
// of the sender.
//
// Returns DTNMOS_OK, or DTNMOS_E_PARSE when Json is not a JSON object.
DTNMOS_API DtNmosResult DtNmosSenderInfo_Parse(const char* Json, size_t Length,
                                               DtNmosSenderList** Sender);

// Creates a subscription, not yet open. Returns NULL when there is not enough memory.
DTNMOS_API DtNmosSubscription* DtNmosSubscription_Alloc(void);

// Closes Subscription's WebSocket. The registry then ends the subscription, unless it is
// persistent.
//
// Returns DTNMOS_OK, or DTNMOS_E_STATE when the subscription is not open.
DTNMOS_API DtNmosResult DtNmosSubscription_Close(DtNmosSubscription* Subscription);

// Closes Subscription if it is open, and frees it. NULL does nothing.
DTNMOS_API void DtNmosSubscription_Free(DtNmosSubscription* Subscription);

// Frees *Subscription, as DtNmosSubscription_Free() does, and sets *Subscription to NULL.
// NULL does nothing.
DTNMOS_API void DtNmosSubscription_Freep(DtNmosSubscription** Subscription);

// Opens Subscription: asks the registry of Query, which must be open, for a subscription
// to the resources at Config->ResourcePath, and connects to its WebSocket. Keep Query
// open while the subscription is open.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_STATE  the subscription is already open, or Query is not open
//   DTNMOS_E_PARSE  the registry's answer names no WebSocket
// and the errors of the request and of the WebSocket's Connect.
DTNMOS_API DtNmosResult DtNmosSubscription_Open(DtNmosSubscription* Subscription,
                                                DtNmosQuery* Query,
                                                const DtNmosSubscriptionConfig* Config);

// Waits up to TimeoutMs milliseconds for a message from the registry, and calls OnChange
// for each change in it, on the calling thread. The first message reports every resource
// as DTNMOS_CHANGE_PRESENT.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_TIMEOUT  no message came
//   DTNMOS_E_PARSE    a message could not be read; polling may go on
//   DTNMOS_E_NETWORK  the WebSocket closed or failed; open a new subscription, which
//                     starts again with every resource
DTNMOS_API DtNmosResult DtNmosSubscription_Poll(DtNmosSubscription* Subscription,
                                                uint32_t TimeoutMs);

// Returns the URL of Subscription's WebSocket.
DTNMOS_API const char* DtNmosSubscription_Url(const DtNmosSubscription* Subscription);

// +=+=+=+=+=+=+=+=+=+=+=+=+= Finding registries through DNS-SD +=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A registry announces its APIs on the network with DNS-SD. DtNmos_Discover() searches
// once, through multicast DNS on the local link and through the network's DNS server,
// and returns the registries it found.
//

// Which API of a registry to look for.
typedef enum DtNmosService
{
    DTNMOS_SERVICE_NONE = 0,     // Not given; refused
    DTNMOS_SERVICE_QUERY,        // The Query API (_nmos-query._tcp)
    DTNMOS_SERVICE_REGISTRATION, // The Registration API (_nmos-register._tcp)
} DtNmosService;

// How registries are searched for; the values may be combined.
typedef enum DtNmosSearch
{
    DTNMOS_SEARCH_MULTICAST = 1, // Multicast DNS on the local link, domain "local"
    DTNMOS_SEARCH_UNICAST = 2,   // The network's DNS server, in the host's domain
} DtNmosSearch;

// How DtNmos_Discover() searches.
typedef struct DtNmosDiscoveryConfig
{
    size_t Size;           // sizeof(DtNmosDiscoveryConfig)
    DtNmosService Service; // Which API to look for
    // The IPv4 address of the network interface to search on; NULL for the one of the
    // default route.
    const char* InterfaceAddress;
    // Where to send multicast DNS queries, "<IPv4 address>:<port>"; NULL for
    // "224.0.0.251:5353". A test may give a responder of its own.
    const char* Destination;
    uint32_t TimeoutMs; // How long to collect answers; 1000 when 0
    DtNmosLogFunc Log;  // Receives log messages; may be NULL
    void* LogUser;      // Passed to Log
    // DTNMOS_SEARCH_MULTICAST and DTNMOS_SEARCH_UNICAST, combined with |; 0 for both.
    unsigned Searches;
    // The DNS server, "<IPv4 address>:<port>"; NULL for the host's first IPv4 DNS server,
    // at port 53.
    const char* DnsServer;
    // The domain to search through the DNS server, e.g. "example.com"; NULL for the
    // host's own. Without a DNS server or a domain, only multicast DNS is searched.
    const char* DnsDomain;
} DtNmosDiscoveryConfig;

// One API of a registry that was found. Its pointer strings belong to the list it is
// in, and are valid until that list is freed. A string not announced is "", never NULL.
typedef struct DtNmosRegistryInfo
{
    DtNmosService Service;                 // Which API this is
    const char* Instance;                  // The announced name, e.g. "Registry 1"
    char Host[DTNMOS_MAX_ADDRESS_SIZE];    // The host name, e.g. "registry-1.local"
    char Address[DTNMOS_MAX_ADDRESS_SIZE]; // The host's IPv4 address, when announced
    uint16_t Port;
    // The base URL of the API, "<protocol>://<address>:<port>"; with the host name
    // instead of the address for https, or when no address was announced.
    const char* Url;
    char ApiProto[DTNMOS_MAX_SHORT_SIZE]; // "http" or "https"
    const char* ApiVersions;              // e.g. "v1.2,v1.3"
    int Priority; // Lower is preferred; 100 and up are for development; -1 for none
    bool Auth;    // The API asks for authorization (IS-10)
    bool Usable;  // dtnmos can use it: v1.3, over http or https, without authorization
    DtNmosSearch FoundBy; // The search that found it
} DtNmosRegistryInfo;

// A list of registries that were found. It owns them and their strings. The order is:
// usable ones first; then by priority, those without one last; those from the DNS server
// before those from multicast DNS; then by name. Among registries of the same priority,
// IS-04 asks a client to pick one at random; that is up to the program.
typedef struct DtNmosRegistryList DtNmosRegistryList;

// Searches the network for registries of Config->Service, through multicast DNS and the
// DNS server at the same time, and sets *List to what it found, for the program to free.
// Finding none is not a failure: the list is then empty.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_INVALID_ARGUMENT  an address or domain in Config is malformed
//   DTNMOS_E_NETWORK           the socket could not be opened, or the multicast query
//                              could not be sent
// A DNS server that cannot be reached is only logged.
DTNMOS_API DtNmosResult DtNmos_Discover(const DtNmosDiscoveryConfig* Config,
                                        DtNmosRegistryList** List);

// Returns registry Index (from 0) of List, or NULL past the end.
DTNMOS_API const DtNmosRegistryInfo* DtNmosRegistryList_At(const DtNmosRegistryList* List,
                                                           size_t Index);

// Returns how many registries List has; 0 for NULL.
DTNMOS_API size_t DtNmosRegistryList_Count(const DtNmosRegistryList* List);

// Frees List and its registries. NULL does nothing.
DTNMOS_API void DtNmosRegistryList_Free(DtNmosRegistryList* List);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= One search of an application +=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A DtNmosRegistrySearch keeps searching for registries on a thread of its own, and keeps
// the list up to date. A program opens one, and gives it to its nodes (as
// DtNmosNodeConfig.Search) and uses it for its clients. A program that finds registries
// in another way "feeds" the search the list instead.
//

// A search for registries that runs in the background.
typedef struct DtNmosRegistrySearch DtNmosRegistrySearch;

// The APIs a search looks for, combined with | in DtNmosRegistrySearchConfig.Finds.
#define DTNMOS_FINDS_QUERY 1u
#define DTNMOS_FINDS_REGISTRATION 2u

// How a search is opened.
typedef struct DtNmosRegistrySearchConfig
{
    size_t Size;    // sizeof(DtNmosRegistrySearchConfig)
    unsigned Finds; // The APIs to look for: DTNMOS_FINDS_QUERY, _REGISTRATION or both
    // How to search, as for DtNmos_Discover(), for each API in Finds; its Service is not
    // used. Copied. NULL: multicast DNS and the host's DNS server, on the interface of
    // the default route.
    const DtNmosDiscoveryConfig* Discovery;
    // True: never search; hold the lists the program gives with
    // DtNmosRegistrySearch_Feed() instead.
    bool Fed;
} DtNmosRegistrySearchConfig;

// Every function of a search except _Alloc, _Open, _Free and _Freep needs an open search,
// and returns DTNMOS_E_STATE for one that is not open.

// Creates a search, not yet open. Returns NULL when there is not enough memory.
DTNMOS_API DtNmosRegistrySearch* DtNmosRegistrySearch_Alloc(void);

// Stops searching and forgets what was found. Close the nodes that use the search first.
DTNMOS_API DtNmosResult DtNmosRegistrySearch_Close(DtNmosRegistrySearch* Search);

// Gives a fed search its list of registries for Service: Count base URLs, e.g.
// "http://registry.local:8010", the most preferred first. It replaces the list it had.
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_STATE             the search is not fed
//   DTNMOS_E_INVALID_ARGUMENT  Search does not look for Service, or a URL is not http or
//                              https
DTNMOS_API DtNmosResult DtNmosRegistrySearch_Feed(DtNmosRegistrySearch* Search,
                                                  DtNmosService Service,
                                                  const char* const* Urls, size_t Count);

// Closes Search if it is open, and frees it. NULL does nothing.
DTNMOS_API void DtNmosRegistrySearch_Free(DtNmosRegistrySearch* Search);

// Frees *Search, as DtNmosRegistrySearch_Free() does, and sets *Search to NULL. NULL does
// nothing.
DTNMOS_API void DtNmosRegistrySearch_Freep(DtNmosRegistrySearch** Search);

// Sets *List to a copy of the registries for Service found so far, in the order of
// DtNmos_Discover(), for the program to free. It is empty until the first search ends.
// The registries of a fed search are usable, with their URL as name and their place in
// the list as priority.
//
// Returns DTNMOS_OK, or DTNMOS_E_INVALID_ARGUMENT when Search does not look for Service.
DTNMOS_API DtNmosResult DtNmosRegistrySearch_List(DtNmosRegistrySearch* Search,
                                                  DtNmosService Service,
                                                  DtNmosRegistryList** List);

// Opens Search and, unless it is fed, starts searching on a thread of its own: every 3
// seconds, and sooner while a node that uses it has no registry (after 1, 2, 4 and 8
// seconds, as IS-04 asks).
//
// Returns DTNMOS_OK, or:
//   DTNMOS_E_INVALID_ARGUMENT  Config looks for nothing, or Discovery->Size is wrong
//   DTNMOS_E_STATE             the search is already open
DTNMOS_API DtNmosResult DtNmosRegistrySearch_Open(
    DtNmosRegistrySearch* Search, const DtNmosRegistrySearchConfig* Config);

#ifdef __cplusplus
}
#endif
