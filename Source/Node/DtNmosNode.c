// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosNode.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The NMOS node
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "NmosNode.h"
#include "NmosOs.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosNode_Lock(DtNmosNode* node)
{
    NmosOs_MutexLock(node->mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosNode_Unlock(DtNmosNode* node)
{
    NmosOs_MutexUnlock(node->mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- copy_text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static char* copy_text(const char* text)
{
    const size_t length = text == NULL ? 0 : strlen(text);
    char* copy = malloc(length + 1);
    if (copy != NULL)
    {
        if (length > 0)
        {
            memcpy(copy, text, length);
        }
        copy[length] = '\0';
    }
    return copy;
}

static void node_log(DtNmosNode* node, DtNmosLogLevel level, const char* format, ...)
    DTNMOS_PRINTF(3, 4);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- node_log -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void node_log(DtNmosNode* node, DtNmosLogLevel level, const char* format, ...)
{
    if (node->log == NULL)
    {
        return;
    }
    char message[640];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    node->log(node->log_user, level, message);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- host_of_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the host of an URL, without brackets around an IPv6 address, into host.
//
static int host_of_url(const char* url, char* host, size_t size)
{
    const char* start = strstr(url, "://");
    start = start == NULL ? url : start + 3;
    const char* at = strchr(start, '@');
    const char* slash = strchr(start, '/');
    if (at != NULL && (slash == NULL || at < slash))
    {
        start = at + 1;
    }
    const char* end = NULL;
    if (*start == '[')
    {
        ++start;
        end = strchr(start, ']');
    }
    else
    {
        end = start + strcspn(start, ":/?#");
    }
    if (end == NULL || end == start || (size_t)(end - start) >= size)
    {
        return 0;
    }
    memcpy(host, start, (size_t)(end - start));
    host[end - start] = '\0';
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- registration_base -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the base of the Registration API of the registry at url, which the caller
// frees, or null when out of memory.
//
static char* registration_base(const char* url)
{
    size_t length = strlen(url);
    while (length > 0 && url[length - 1] == '/')
    {
        --length;
    }
    NmosBuffer base;
    memset(&base, 0, sizeof(base));
    NmosBuffer_Append(&base, url, length);
    DTNMOS_APPEND_LITERAL(&base, "/x-nmos/registration/v1.3/");
    if (base.failed)
    {
        NmosBuffer_Free(&base);
        return NULL;
    }
    return base.data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosNode* DtNmosNode_Alloc(void)
{
    return calloc(1, sizeof(DtNmosNode));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_CheckOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult NmosNode_CheckOpen(const DtNmosNode* node, const char* function)
{
    if (node == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "%s() needs a node.", function);
    }
    if (!node->open)
    {
        return NmosError_Fail(DTNMOS_E_STATE, "%s() needs an open node.", function);
    }
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Open(DtNmosNode* node, const DtNmosNodeConfig* config)
{
    if (node == NULL || config == NULL || config->Id.Text[0] == '\0' ||
        config->RegistrationUrl == NULL || config->RegistrationUrl[0] == '\0' ||
        config->Http == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "A node needs an ID, the URL of a registry and an HTTP function.");
    }
    if (node->open)
    {
        return NmosError_Fail(DTNMOS_E_STATE,
                              "The node is open already; close it first.");
    }
    // The first version of the config ends before the fields of moving to another
    // registry.
    const DtNmosResult sized = DTNMOS_CHECK_SIZE(
        config, DtNmosNodeConfig, offsetof(DtNmosNodeConfig, RegistryFailed));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    if (config->ApiVersion != NULL && strcmp(config->ApiVersion, "v1.3") != 0)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "IS-04 %s is not supported; dtnmos speaks v1.3.",
                              config->ApiVersion);
    }
    DtNmosNode* result = node;
    result->mutex = NmosOs_MutexCreate();
    result->id = config->Id;
    result->label = copy_text(config->Label);
    result->description = copy_text(config->Description);
    result->hostname = copy_text(config->Hostname);
    result->api_port = config->ApiPort;
    char host[256] = "";
    if (config->ApiHost != NULL && config->ApiHost[0] != '\0')
    {
        snprintf(host, sizeof(host), "%s", config->ApiHost);
    }
    else
    {
        char registry[256];
        if (!host_of_url(config->RegistrationUrl, registry, sizeof(registry)) ||
            !NmosOs_AddressToward(registry, host, sizeof(host)))
        {
            snprintf(host, sizeof(host), "127.0.0.1");
        }
    }
    result->api_host = copy_text(host);
    result->registration = registration_base(config->RegistrationUrl);
    result->http = config->Http;
    result->http_user = config->HttpUser;
    result->timeout_ms = config->TimeoutMs == 0 ? 5000 : config->TimeoutMs;
    result->heartbeat_ms = config->HeartbeatMs == 0 ? 5000 : config->HeartbeatMs;
    result->log = config->Log;
    result->log_user = config->LogUser;
    // A config of an older header ends before the fields of moving to another registry.
    if (config->Size >= offsetof(DtNmosNodeConfig, FailuresBeforeSwitch) +
                            sizeof(config->FailuresBeforeSwitch))
    {
        result->registry_failed = config->RegistryFailed;
        result->registry_failed_user = config->RegistryFailedUser;
        result->failures_before_switch = config->FailuresBeforeSwitch;
    }
    if (result->failures_before_switch == 0)
    {
        result->failures_before_switch = 3;
    }
    if (result->mutex == NULL || result->label == NULL || result->description == NULL ||
        result->hostname == NULL || result->api_host == NULL ||
        result->registration == NULL)
    {
        NmosNode_Release(result);
        return NmosError_FailMemory();
    }
    NmosOs_VersionNow(&result->last_version, result->version, sizeof(result->version));
    result->open = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void free_sender(NmosNodeSender* sender)
{
    free(sender->label);
    free(sender->description);
    free(sender->source_ip);
    NmosStore_Free(&sender->flow_store);
    NmosConnection_ClearSender(sender);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void free_receiver(NmosNodeReceiver* receiver)
{
    free(receiver->label);
    free(receiver->description);
    NmosConnection_ClearReceiver(receiver);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_Release(DtNmosNode* node)
{
    for (size_t i = 0; i < node->device_count; ++i)
    {
        free(node->devices[i].label);
        free(node->devices[i].description);
    }
    for (size_t i = 0; i < node->sender_count; ++i)
    {
        free_sender(&node->senders[i]);
    }
    for (size_t i = 0; i < node->receiver_count; ++i)
    {
        free_receiver(&node->receivers[i]);
    }
    free(node->devices);
    free(node->senders);
    free(node->receivers);
    free(node->removals);
    free(node->label);
    free(node->description);
    free(node->hostname);
    free(node->api_host);
    free(node->registration);
    NmosOs_MutexFree(node->mutex);
    memset(node, 0, sizeof(*node));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- registry_request -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Performs a request to the Registration API; path follows its base. Returns the status
// in status, or fails when no answer came.
//
static DtNmosResult registry_request(DtNmosNode* node, const char* method,
                                     const char* path, const char* body, int* status)
{
    NmosBuffer url;
    memset(&url, 0, sizeof(url));
    NmosBuffer_Printf(&url, "%s%s", node->registration, path);
    DtNmosHttpResponse* response = DtNmosHttpResponse_Alloc();
    if (url.failed || response == NULL)
    {
        NmosBuffer_Free(&url);
        DtNmosHttpResponse_Free(response);
        return NmosError_FailMemory();
    }
    DtNmosHttpRequest request;
    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Method = method;
    request.Url = url.data;
    request.TimeoutMs = node->timeout_ms;
    if (body != NULL)
    {
        request.ContentType = "application/json";
        request.Body = body;
        request.BodyLength = strlen(body);
    }
    NmosError_Clear();
    DtNmosResult result = node->http(node->http_user, &request, response);
    if (result == DTNMOS_OK)
    {
        *status = DtNmosHttpResponse_Status(response);
    }
    else if (DtNmos_GetLastError()[0] == '\0')
    {
        NmosError_Fail(result, "%s %s failed.", method, url.data);
    }
    NmosBuffer_Free(&url);
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- register_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Registers a resource of type with the JSON data; 200 and 201 are both success.
//
static DtNmosResult register_resource(DtNmosNode* node, const char* type,
                                      const char* data)
{
    NmosBuffer body;
    memset(&body, 0, sizeof(body));
    NmosBuffer_Printf(&body, "{\"type\": \"%s\", \"data\": %s}", type, data);
    if (body.failed)
    {
        NmosBuffer_Free(&body);
        return NmosError_FailMemory();
    }
    int status = 0;
    DtNmosResult result = registry_request(node, "POST", "resource", body.data, &status);
    NmosBuffer_Free(&body);
    if (result == DTNMOS_OK && status != 200 && status != 201)
    {
        result = NmosError_Fail(
            DTNMOS_E_HTTP, "The registry answered %d to registering a %s.", status, type);
    }
    if (result == DTNMOS_OK)
    {
        node_log(node, DTNMOS_LOG_DEBUG, "Registered a %s.", type);
    }
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- write_common -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the members that every resource of IS-04 has.
//
static void write_common(NmosBuffer* b, const DtNmosId* id, const char* version,
                         const char* label, const char* description)
{
    NmosBuffer_Printf(b, "\"id\": \"%s\", \"version\": \"%s\", \"label\": ", id->Text,
                      version);
    NmosJson_WriteString(b, label);
    DTNMOS_APPEND_LITERAL(b, ", \"description\": ");
    NmosJson_WriteString(b, description);
    DTNMOS_APPEND_LITERAL(b, ", \"tags\": {}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteBaseUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosNode_WriteBaseUrl(const DtNmosNode* node, NmosBuffer* b)
{
    const int ipv6 = strchr(node->api_host, ':') != NULL;
    NmosBuffer_Printf(b, "http://%s%s%s:%u", ipv6 ? "[" : "", node->api_host,
                      ipv6 ? "]" : "", (unsigned)node->api_port);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteSelf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteSelf(const DtNmosNode* node, NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &node->id, node->version, node->label, node->description);
    DTNMOS_APPEND_LITERAL(b, ", \"href\": \"");
    NmosNode_WriteBaseUrl(node, b);
    DTNMOS_APPEND_LITERAL(b, "/\", \"hostname\": ");
    NmosJson_WriteString(b, node->hostname);
    DTNMOS_APPEND_LITERAL(
        b, ", \"api\": {\"versions\": [\"v1.3\"], \"endpoints\": [{\"host\": ");
    NmosJson_WriteString(b, node->api_host);
    NmosBuffer_Printf(
        b,
        ", \"port\": %u, \"protocol\": \"http\"}]}, \"caps\": {}, \"services\": "
        "[], \"clocks\": [{\"name\": \"clk0\", \"ref_type\": \"internal\"}], "
        "\"interfaces\": []}",
        (unsigned)node->api_port);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteDevice(const DtNmosNode* node, const NmosNodeDevice* device,
                          NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &device->id, device->version, device->label, device->description);
    NmosBuffer_Printf(b,
                      ", \"type\": \"urn:x-nmos:device:generic\", \"node_id\": \"%s\", "
                      "\"senders\": [",
                      node->id.Text);
    int first = 1;
    for (size_t i = 0; i < node->sender_count; ++i)
    {
        if (strcmp(node->senders[i].device_id.Text, device->id.Text) == 0)
        {
            NmosBuffer_Printf(b, "%s\"%s\"", first ? "" : ", ", node->senders[i].id.Text);
            first = 0;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "], \"receivers\": [");
    first = 1;
    for (size_t i = 0; i < node->receiver_count; ++i)
    {
        if (strcmp(node->receivers[i].device_id.Text, device->id.Text) == 0)
        {
            NmosBuffer_Printf(b, "%s\"%s\"", first ? "" : ", ",
                              node->receivers[i].id.Text);
            first = 0;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "], \"controls\": [{\"href\": \"");
    NmosNode_WriteBaseUrl(node, b);
    DTNMOS_APPEND_LITERAL(b, "/x-nmos/connection/v1.1/\", \"type\": "
                             "\"urn:x-nmos:control:sr-ctrl/v1.1\"}]}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int is_video(const NmosNodeSender* sender)
{
    return sender->flow.Media == DTNMOS_MEDIA_VIDEO;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteSource(const NmosNodeSender* sender, NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &sender->source_id, sender->version, sender->label,
                 sender->description);
    NmosBuffer_Printf(b,
                      ", \"format\": \"urn:x-nmos:format:%s\", \"caps\": {}, "
                      "\"device_id\": \"%s\", \"parents\": [], \"clock_name\": \"clk0\"",
                      is_video(sender) ? "video" : "audio", sender->device_id.Text);
    if (is_video(sender))
    {
        const DtNmosVideoFormat* video = &sender->flow.Format.Video;
        NmosBuffer_Printf(
            b, ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}}",
            (unsigned)video->RateNumerator,
            (unsigned)(video->RateDenominator == 0 ? 1 : video->RateDenominator));
        return;
    }
    DTNMOS_APPEND_LITERAL(b, ", \"channels\": [");
    for (uint32_t c = 0; c < sender->flow.Format.Audio.Channels; ++c)
    {
        NmosBuffer_Printf(b, "%s{\"label\": \"Channel %u\"}", c == 0 ? "" : ", ",
                          (unsigned)(c + 1));
    }
    DTNMOS_APPEND_LITERAL(b, "]}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- or_default -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns text, or fallback when it is empty.
//
static const char* or_default(const char* text, const char* fallback)
{
    return text[0] != '\0' ? text : fallback;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteFlow(const NmosNodeSender* sender, NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &sender->flow_id, sender->version, sender->label,
                 sender->description);
    NmosBuffer_Printf(b,
                      ", \"format\": \"urn:x-nmos:format:%s\", \"source_id\": \"%s\", "
                      "\"device_id\": \"%s\", \"parents\": []",
                      is_video(sender) ? "video" : "audio", sender->source_id.Text,
                      sender->device_id.Text);
    if (is_video(sender))
    {
        const DtNmosVideoFormat* video = &sender->flow.Format.Video;
        const unsigned width = (unsigned)video->Width;
        const unsigned height = (unsigned)video->Height;
        const unsigned depth = (unsigned)(video->Depth == 0 ? 10 : video->Depth);
        NmosBuffer_Printf(
            b,
            ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}, "
            "\"frame_width\": "
            "%u, "
            "\"frame_height\": %u, \"colorspace\": \"%s\", \"interlace_mode\": \"%s\", "
            "\"transfer_characteristic\": \"%s\", \"media_type\": \"video/raw\", "
            "\"components\": "
            "[{\"name\": \"Y\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}, "
            "{\"name\": \"Cb\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}, "
            "{\"name\": \"Cr\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}]}",
            (unsigned)video->RateNumerator,
            (unsigned)(video->RateDenominator == 0 ? 1 : video->RateDenominator), width,
            height, or_default(video->Colorimetry, "BT709"),
            video->Interlaced ? "interlaced_tff" : "progressive",
            or_default(video->Tcs, "SDR"), width, height, depth, width / 2, height, depth,
            width / 2, height, depth);
        return;
    }
    const DtNmosAudioFormat* audio = &sender->flow.Format.Audio;
    const int l16 = strcmp(audio->Encoding, "L16") == 0;
    NmosBuffer_Printf(
        b,
        ", \"sample_rate\": {\"numerator\": %u}, \"media_type\": \"audio/%s\", "
        "\"bit_depth\": %d}",
        (unsigned)audio->SampleRate, l16 ? "L16" : "L24", l16 ? 16 : 24);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteSender(const DtNmosNode* node, const NmosNodeSender* sender,
                          NmosBuffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &sender->id, sender->version, sender->label, sender->description);
    NmosBuffer_Printf(
        b,
        ", \"flow_id\": \"%s\", \"transport\": \"urn:x-nmos:transport:%s\", "
        "\"device_id\": \"%s\", \"manifest_href\": \"",
        sender->flow_id.Text,
        NmosNode_IsMulticast(sender->flow.DestinationIp) ? "rtp.mcast" : "rtp.ucast",
        sender->device_id.Text);
    NmosNode_WriteBaseUrl(node, b);
    NmosBuffer_Printf(b,
                      "/x-nmos/connection/v1.1/single/senders/%s/transportfile\", "
                      "\"interface_bindings\": [], \"subscription\": {\"receiver_id\": ",
                      sender->id.Text);
    if (sender->receiver_id.Text[0] != '\0')
    {
        NmosBuffer_Printf(b, "\"%s\"", sender->receiver_id.Text);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    NmosBuffer_Printf(b, ", \"active\": %s}}", sender->master_enable ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosNode_WriteReceiver(const NmosNodeReceiver* receiver, NmosBuffer* b)
{
    const int video = receiver->media == DTNMOS_MEDIA_VIDEO;
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &receiver->id, receiver->version, receiver->label,
                 receiver->description);
    NmosBuffer_Printf(
        b,
        ", \"format\": \"urn:x-nmos:format:%s\", \"caps\": {\"media_types\": "
        "[%s]}, \"device_id\": \"%s\", \"transport\": "
        "\"urn:x-nmos:transport:rtp\", \"interface_bindings\": [], "
        "\"subscription\": {\"sender_id\": ",
        video ? "video" : "audio",
        video ? "\"video/raw\"" : "\"audio/L24\", \"audio/L16\"",
        receiver->device_id.Text);
    if (receiver->sender_id.Text[0] != '\0')
    {
        NmosBuffer_Printf(b, "\"%s\"", receiver->sender_id.Text);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    NmosBuffer_Printf(b, ", \"active\": %s}}",
                      receiver->master_enable ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_IsMulticast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int NmosNode_IsMulticast(const char* address)
{
    if (strchr(address, ':') != NULL)
    {
        return (address[0] == 'f' || address[0] == 'F') &&
               (address[1] == 'f' || address[1] == 'F');
    }
    const int first = atoi(address);
    return first >= 224 && first <= 239;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_WriteTransportFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosNode_WriteTransportFile(const NmosNodeSender* sender, NmosBuffer* text)
{
    DtNmosSession session;
    memset(&session, 0, sizeof(session));
    session.Size = sizeof(session);
    session.SessionId = sender->session_id;
    session.SessionVersion = sender->session_version;
    session.Name = sender->label;
    const char* origin = sender->source_ip[0] != '\0' ? sender->source_ip : "0.0.0.0";
    if (!NmosText_CopySpan(session.OriginIp, sizeof(session.OriginIp),
                           NmosSpan_Of(origin)))
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "The source address of the sender is longer than a "
                              "domain name may be.");
    }
    return NmosSdp_Write(&session, &sender->flow, 1, text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- new_version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void new_version(DtNmosNode* node, char* version, size_t size)
{
    NmosOs_VersionNow(&node->last_version, version, size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_FindDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosNodeDevice* NmosNode_FindDevice(DtNmosNode* node, const DtNmosId* id)
{
    for (size_t i = 0; i < node->device_count; ++i)
    {
        if (strcmp(node->devices[i].id.Text, id->Text) == 0)
        {
            return &node->devices[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_FindSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosNodeSender* NmosNode_FindSender(DtNmosNode* node, const DtNmosId* id)
{
    for (size_t i = 0; i < node->sender_count; ++i)
    {
        if (strcmp(node->senders[i].id.Text, id->Text) == 0)
        {
            return &node->senders[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_FindReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
NmosNodeReceiver* NmosNode_FindReceiver(DtNmosNode* node, const DtNmosId* id)
{
    for (size_t i = 0; i < node->receiver_count; ++i)
    {
        if (strcmp(node->receivers[i].id.Text, id->Text) == 0)
        {
            return &node->receivers[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- id_taken -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int id_taken(DtNmosNode* node, const DtNmosId* id)
{
    return strcmp(node->id.Text, id->Text) == 0 ||
           NmosNode_FindDevice(node, id) != NULL ||
           NmosNode_FindSender(node, id) != NULL ||
           NmosNode_FindReceiver(node, id) != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- grow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Grows an array of count elements of size bytes to hold one more.
//
static int grow(void** array, size_t* capacity, size_t count, size_t size)
{
    if (count < *capacity)
    {
        return 1;
    }
    const size_t grown = *capacity == 0 ? 4 : *capacity * 2;
    void* larger = realloc(*array, grown * size);
    if (larger == NULL)
    {
        return 0;
    }
    *array = larger;
    *capacity = grown;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- touch_device -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The device of a sender or receiver changes with it, as it lists them.
//
static void touch_device(DtNmosNode* node, const DtNmosId* id)
{
    NmosNodeDevice* device = NmosNode_FindDevice(node, id);
    if (device != NULL)
    {
        new_version(node, device->version, sizeof(device->version));
        device->registered = 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddDevice(DtNmosNode* node, const DtNmosDeviceConfig* device)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_AddDevice");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (device == NULL || device->Id.Text[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "A device needs an ID.");
    }
    const DtNmosResult sized =
        DTNMOS_CHECK_SIZE(device, DtNmosDeviceConfig, sizeof(DtNmosDeviceConfig));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    NmosNode_Lock(node);
    DtNmosResult result = DTNMOS_OK;
    if (id_taken(node, &device->Id))
    {
        result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                                device->Id.Text);
    }
    else if (!grow((void**)&node->devices, &node->device_capacity, node->device_count,
                   sizeof(*node->devices)))
    {
        result = NmosError_FailMemory();
    }
    else
    {
        NmosNodeDevice* added = &node->devices[node->device_count];
        memset(added, 0, sizeof(*added));
        added->id = device->Id;
        added->label = copy_text(device->Label);
        added->description = copy_text(device->Description);
        if (added->label == NULL || added->description == NULL)
        {
            free(added->label);
            free(added->description);
            result = NmosError_FailMemory();
        }
        else
        {
            new_version(node, added->version, sizeof(added->version));
            ++node->device_count;
        }
    }
    NmosNode_Unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- derived_id -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes the ID of a resource that a sender brings along, from the ID of the sender.
//
static void derived_id(const DtNmosId* sender, const char* what, DtNmosId* id)
{
    if (DtNmosId_FromName(sender, what, id) != DTNMOS_OK)
    {
        memset(id, 0, sizeof(*id));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddSender(DtNmosNode* node, const DtNmosSenderConfig* sender,
                                  DtNmosSenderActivateFunc activate, void* user)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_AddSender");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (sender == NULL || sender->Id.Text[0] == '\0' || sender->Flow == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A sender needs an ID and a flow.");
    }
    const DtNmosResult sized =
        DTNMOS_CHECK_SIZE(sender, DtNmosSenderConfig, sizeof(DtNmosSenderConfig));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    const DtNmosResult flow_sized =
        DTNMOS_CHECK_SIZE(sender->Flow, DtNmosFlow, sizeof(DtNmosFlow));
    if (flow_sized != DTNMOS_OK)
    {
        return flow_sized;
    }
    if (sender->Flow->Media != DTNMOS_MEDIA_VIDEO &&
        sender->Flow->Media != DTNMOS_MEDIA_AUDIO)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A sender of the node sends video or audio, not %s.",
                              DtNmosMedia_Name(sender->Flow->Media));
    }
    NmosNode_Lock(node);
    DtNmosResult result = DTNMOS_OK;
    if (id_taken(node, &sender->Id))
    {
        result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                                sender->Id.Text);
    }
    else if (NmosNode_FindDevice(node, &sender->DeviceId) == NULL)
    {
        result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has no device %s.",
                                sender->DeviceId.Text);
    }
    else if (!grow((void**)&node->senders, &node->sender_capacity, node->sender_count,
                   sizeof(*node->senders)))
    {
        result = NmosError_FailMemory();
    }
    else
    {
        NmosNodeSender* added = &node->senders[node->sender_count];
        memset(added, 0, sizeof(*added));
        added->id = sender->Id;
        added->device_id = sender->DeviceId;
        derived_id(&sender->Id, "source", &added->source_id);
        derived_id(&sender->Id, "flow", &added->flow_id);
        added->label = copy_text(sender->Label);
        added->description = copy_text(sender->Description);
        added->source_ip = copy_text(sender->SourceIp);
        added->activate = activate;
        added->user = user;
        added->master_enable = 1;
        added->session_id = node->last_version / 1000000000u;
        added->session_version = 1;
        if (added->label == NULL || added->description == NULL ||
            added->source_ip == NULL ||
            NmosFlow_Copy(&added->flow, &added->flow_store, sender->Flow) != DTNMOS_OK ||
            NmosConnection_InitSender(added) != DTNMOS_OK)
        {
            free_sender(added);
            result = NmosError_FailMemory();
        }
        else
        {
            new_version(node, added->version, sizeof(added->version));
            ++node->sender_count;
            touch_device(node, &sender->DeviceId);
        }
    }
    NmosNode_Unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddReceiver(DtNmosNode* node,
                                    const DtNmosReceiverConfig* receiver,
                                    DtNmosReceiverActivateFunc activate, void* user)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_AddReceiver");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (receiver == NULL || receiver->Id.Text[0] == '\0')
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "A receiver needs an ID.");
    }
    const DtNmosResult sized =
        DTNMOS_CHECK_SIZE(receiver, DtNmosReceiverConfig, sizeof(DtNmosReceiverConfig));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    if (receiver->Media != DTNMOS_MEDIA_VIDEO && receiver->Media != DTNMOS_MEDIA_AUDIO)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "A receiver of the node receives video or audio, not %s.",
                              DtNmosMedia_Name(receiver->Media));
    }
    NmosNode_Lock(node);
    DtNmosResult result = DTNMOS_OK;
    if (id_taken(node, &receiver->Id))
    {
        result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                                receiver->Id.Text);
    }
    else if (NmosNode_FindDevice(node, &receiver->DeviceId) == NULL)
    {
        result = NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT, "The node has no device %s.",
                                receiver->DeviceId.Text);
    }
    else if (!grow((void**)&node->receivers, &node->receiver_capacity,
                   node->receiver_count, sizeof(*node->receivers)))
    {
        result = NmosError_FailMemory();
    }
    else
    {
        NmosNodeReceiver* added = &node->receivers[node->receiver_count];
        memset(added, 0, sizeof(*added));
        added->id = receiver->Id;
        added->device_id = receiver->DeviceId;
        added->media = receiver->Media;
        added->label = copy_text(receiver->Label);
        added->description = copy_text(receiver->Description);
        added->activate = activate;
        added->user = user;
        if (added->label == NULL || added->description == NULL ||
            NmosConnection_InitReceiver(added) != DTNMOS_OK)
        {
            free_receiver(added);
            result = NmosError_FailMemory();
        }
        else
        {
            new_version(node, added->version, sizeof(added->version));
            ++node->receiver_count;
            touch_device(node, &receiver->DeviceId);
        }
    }
    NmosNode_Unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- schedule_removal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds a resource of type to delete from the registry, when it was registered.
//
static int schedule_removal(DtNmosNode* node, const char* type, const DtNmosId* id)
{
    if (!grow((void**)&node->removals, &node->removal_capacity, node->removal_count,
              sizeof(*node->removals)))
    {
        return 0;
    }
    NmosNodeRemoval* removal = &node->removals[node->removal_count++];
    snprintf(removal->type, sizeof(removal->type), "%s", type);
    removal->id = *id;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- remove_sender_at -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void remove_sender_at(DtNmosNode* node, size_t index)
{
    NmosNodeSender* sender = &node->senders[index];
    if (sender->was_registered)
    {
        schedule_removal(node, "senders", &sender->id);
        schedule_removal(node, "flows", &sender->flow_id);
        schedule_removal(node, "sources", &sender->source_id);
    }
    touch_device(node, &sender->device_id);
    free_sender(sender);
    memmove(sender, sender + 1, (node->sender_count - index - 1) * sizeof(*sender));
    --node->sender_count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- remove_receiver_at -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void remove_receiver_at(DtNmosNode* node, size_t index)
{
    NmosNodeReceiver* receiver = &node->receivers[index];
    if (receiver->was_registered)
    {
        schedule_removal(node, "receivers", &receiver->id);
    }
    touch_device(node, &receiver->device_id);
    free_receiver(receiver);
    memmove(receiver, receiver + 1,
            (node->receiver_count - index - 1) * sizeof(*receiver));
    --node->receiver_count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Remove -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Remove(DtNmosNode* node, const DtNmosId* id)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_Remove");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (id == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosNode_Remove() needs an ID.");
    }
    NmosNode_Lock(node);
    DtNmosResult result = DTNMOS_OK;
    NmosNodeDevice* device = NmosNode_FindDevice(node, id);
    if (device != NULL)
    {
        for (size_t i = node->sender_count; i > 0; --i)
        {
            if (strcmp(node->senders[i - 1].device_id.Text, id->Text) == 0)
            {
                remove_sender_at(node, i - 1);
            }
        }
        for (size_t i = node->receiver_count; i > 0; --i)
        {
            if (strcmp(node->receivers[i - 1].device_id.Text, id->Text) == 0)
            {
                remove_receiver_at(node, i - 1);
            }
        }
        device = NmosNode_FindDevice(node, id);
        if (device->registered || device->was_registered)
        {
            schedule_removal(node, "devices", &device->id);
        }
        free(device->label);
        free(device->description);
        const size_t index = (size_t)(device - node->devices);
        memmove(device, device + 1, (node->device_count - index - 1) * sizeof(*device));
        --node->device_count;
    }
    else if (NmosNode_FindSender(node, id) != NULL)
    {
        remove_sender_at(node, (size_t)(NmosNode_FindSender(node, id) - node->senders));
    }
    else if (NmosNode_FindReceiver(node, id) != NULL)
    {
        remove_receiver_at(node,
                           (size_t)(NmosNode_FindReceiver(node, id) - node->receivers));
    }
    else
    {
        result = NmosError_Fail(DTNMOS_E_NOT_FOUND, "The node has no %s.", id->Text);
    }
    NmosNode_Unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_UpdateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_UpdateSender(DtNmosNode* node, const DtNmosId* id,
                                     const DtNmosFlow* flow)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_UpdateSender");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (id == NULL || flow == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosNode_UpdateSender() needs an ID and a flow.");
    }
    const DtNmosResult sized = DTNMOS_CHECK_SIZE(flow, DtNmosFlow, sizeof(DtNmosFlow));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    NmosNode_Lock(node);
    DtNmosResult result = DTNMOS_OK;
    NmosNodeSender* sender = NmosNode_FindSender(node, id);
    if (sender == NULL)
    {
        result =
            NmosError_Fail(DTNMOS_E_NOT_FOUND, "The node has no sender %s.", id->Text);
    }
    else if (flow->Media != sender->flow.Media)
    {
        result = NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT, "Sender %s sends %s and cannot send %s.", id->Text,
            DtNmosMedia_Name(sender->flow.Media), DtNmosMedia_Name(flow->Media));
    }
    else
    {
        // The new flow gets an owner of its own, which replaces the old one only
        // once all of it was copied.
        NmosStore store;
        memset(&store, 0, sizeof(store));
        result = NmosFlow_Copy(&sender->flow, &store, flow);
        if (result == DTNMOS_OK)
        {
            NmosStore_Free(&sender->flow_store);
            sender->flow_store = store;
        }
        else
        {
            NmosStore_Free(&store);
        }
    }
    if (result == DTNMOS_OK && sender != NULL)
    {
        new_version(node, sender->version, sizeof(sender->version));
        ++sender->session_version;
        sender->registered = 0;
    }
    NmosNode_Unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- forget_registration -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Marks everything unregistered, as when the registry has lost the node.
//
static void forget_registration(DtNmosNode* node)
{
    node->node_registered = 0;
    for (size_t i = 0; i < node->device_count; ++i)
    {
        node->devices[i].registered = 0;
    }
    for (size_t i = 0; i < node->sender_count; ++i)
    {
        node->senders[i].registered = 0;
    }
    for (size_t i = 0; i < node->receiver_count; ++i)
    {
        node->receivers[i].registered = 0;
    }
}

// One registration the poll makes: the resources it posts, rendered under the lock, and
// the version it stands for.
typedef struct NmosPending
{
    int kind; // 0 node, 1 device, 2 sender with source and flow, 3 receiver
    DtNmosId id;
    char version[32];
    char* bodies[3];
    const char* types[3];
    size_t count;
} NmosPending;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_pending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void free_pending(NmosPending* p)
{
    for (size_t i = 0; i < p->count; ++i)
    {
        free(p->bodies[i]);
    }
    memset(p, 0, sizeof(*p));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- next_pending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Renders into p the first resource that is not registered, parents before children;
// returns 0 when all are registered.
//
static int next_pending(DtNmosNode* node, NmosPending* p)
{
    memset(p, 0, sizeof(*p));
    NmosBuffer b[3];
    memset(b, 0, sizeof(b));
    if (node->closing)
    {
        return 0;
    }
    if (!node->node_registered)
    {
        p->kind = 0;
        p->id = node->id;
        snprintf(p->version, sizeof(p->version), "%s", node->version);
        NmosNode_WriteSelf(node, &b[0]);
        p->types[0] = "node";
        p->count = 1;
    }
    else
    {
        for (size_t i = 0; i < node->device_count && p->count == 0; ++i)
        {
            if (!node->devices[i].registered)
            {
                p->kind = 1;
                p->id = node->devices[i].id;
                snprintf(p->version, sizeof(p->version), "%s", node->devices[i].version);
                NmosNode_WriteDevice(node, &node->devices[i], &b[0]);
                p->types[0] = "device";
                p->count = 1;
            }
        }
        for (size_t i = 0; i < node->sender_count && p->count == 0; ++i)
        {
            const NmosNodeSender* sender = &node->senders[i];
            const NmosNodeDevice* device = NmosNode_FindDevice(node, &sender->device_id);
            if (!sender->registered && device != NULL && device->registered)
            {
                p->kind = 2;
                p->id = sender->id;
                snprintf(p->version, sizeof(p->version), "%s", sender->version);
                NmosNode_WriteSource(sender, &b[0]);
                NmosNode_WriteFlow(sender, &b[1]);
                NmosNode_WriteSender(node, sender, &b[2]);
                p->types[0] = "source";
                p->types[1] = "flow";
                p->types[2] = "sender";
                p->count = 3;
            }
        }
        for (size_t i = 0; i < node->receiver_count && p->count == 0; ++i)
        {
            const NmosNodeReceiver* receiver = &node->receivers[i];
            const NmosNodeDevice* device =
                NmosNode_FindDevice(node, &receiver->device_id);
            if (!receiver->registered && device != NULL && device->registered)
            {
                p->kind = 3;
                p->id = receiver->id;
                snprintf(p->version, sizeof(p->version), "%s", receiver->version);
                NmosNode_WriteReceiver(receiver, &b[0]);
                p->types[0] = "receiver";
                p->count = 1;
            }
        }
    }
    for (size_t i = 0; i < p->count; ++i)
    {
        p->bodies[i] = b[i].failed ? NULL : b[i].data;
        if (b[i].failed)
        {
            NmosBuffer_Free(&b[i]);
        }
    }
    return p->count > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- mark_registered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Marks what p registered, unless it changed meanwhile.
//
static void mark_registered(DtNmosNode* node, const NmosPending* p)
{
    if (p->kind == 0 && strcmp(node->version, p->version) == 0)
    {
        node->node_registered = 1;
    }
    NmosNodeDevice* device = p->kind == 1 ? NmosNode_FindDevice(node, &p->id) : NULL;
    if (device != NULL && strcmp(device->version, p->version) == 0)
    {
        device->registered = 1;
        device->was_registered = 1;
    }
    NmosNodeSender* sender = p->kind == 2 ? NmosNode_FindSender(node, &p->id) : NULL;
    if (sender != NULL && strcmp(sender->version, p->version) == 0)
    {
        sender->registered = 1;
        sender->was_registered = 1;
    }
    NmosNodeReceiver* receiver =
        p->kind == 3 ? NmosNode_FindReceiver(node, &p->id) : NULL;
    if (receiver != NULL && strcmp(receiver->version, p->version) == 0)
    {
        receiver->registered = 1;
        receiver->was_registered = 1;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_IsRegistered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtNmosNode_IsRegistered(const DtNmosNode* node)
{
    if (node == NULL || !node->open)
    {
        return 0;
    }
    DtNmosNode* mutable_node = (DtNmosNode*)node;
    NmosNode_Lock(mutable_node);
    int all = node->node_registered && node->removal_count == 0;
    for (size_t i = 0; all && i < node->device_count; ++i)
    {
        all = node->devices[i].registered;
    }
    for (size_t i = 0; all && i < node->sender_count; ++i)
    {
        all = node->senders[i].registered;
    }
    for (size_t i = 0; all && i < node->receiver_count; ++i)
    {
        all = node->receivers[i].registered;
    }
    NmosNode_Unlock(mutable_node);
    return all;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Poll(DtNmosNode* node, uint32_t* next_ms)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_Poll");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    DtNmosResult result = DTNMOS_OK;
    // Deletions first, children before parents as they were scheduled.
    for (;;)
    {
        NmosNode_Lock(node);
        if (node->removal_count == 0)
        {
            NmosNode_Unlock(node);
            break;
        }
        const NmosNodeRemoval removal = node->removals[0];
        NmosNode_Unlock(node);
        char path[96];
        snprintf(path, sizeof(path), "resource/%s/%s", removal.type, removal.id.Text);
        int status = 0;
        result = registry_request(node, "DELETE", path, NULL, &status);
        if (result != DTNMOS_OK)
        {
            break;
        }
        NmosNode_Lock(node);
        if (node->removal_count > 0 &&
            strcmp(node->removals[0].id.Text, removal.id.Text) == 0)
        {
            memmove(node->removals, node->removals + 1,
                    (node->removal_count - 1) * sizeof(*node->removals));
            --node->removal_count;
        }
        NmosNode_Unlock(node);
    }
    // Registrations, one resource at a time, each rendered anew under the lock.
    while (result == DTNMOS_OK)
    {
        NmosPending p;
        NmosNode_Lock(node);
        const int any = next_pending(node, &p);
        NmosNode_Unlock(node);
        if (!any)
        {
            break;
        }
        for (size_t i = 0; result == DTNMOS_OK && i < p.count; ++i)
        {
            result = p.bodies[i] == NULL
                         ? NmosError_FailMemory()
                         : register_resource(node, p.types[i], p.bodies[i]);
        }
        if (result == DTNMOS_OK)
        {
            NmosNode_Lock(node);
            mark_registered(node, &p);
            if (p.kind == 0)
            {
                node->next_heartbeat_ms = NmosOs_MonotonicMs() + node->heartbeat_ms;
            }
            NmosNode_Unlock(node);
        }
        free_pending(&p);
    }
    // The heartbeat, when it is due.
    NmosNode_Lock(node);
    const int beat = result == DTNMOS_OK && node->node_registered &&
                     NmosOs_MonotonicMs() >= node->next_heartbeat_ms;
    NmosNode_Unlock(node);
    if (beat)
    {
        char path[96];
        snprintf(path, sizeof(path), "health/nodes/%s", node->id.Text);
        int status = 0;
        result = registry_request(node, "POST", path, NULL, &status);
        NmosNode_Lock(node);
        if (result == DTNMOS_OK && status == 404)
        {
            node_log(node, DTNMOS_LOG_WARNING,
                     "The registry has lost node %s; it registers again.", node->id.Text);
            forget_registration(node);
        }
        else if (result == DTNMOS_OK && status != 200)
        {
            result = NmosError_Fail(DTNMOS_E_HTTP,
                                    "The registry answered %d to a heartbeat.", status);
        }
        node->next_heartbeat_ms = NmosOs_MonotonicMs() + node->heartbeat_ms;
        NmosNode_Unlock(node);
    }
    // A registry that fails polls in a row makes way for another one, when the caller
    // gives one. The URL is swapped here, on the thread that reads it.
    if (result == DTNMOS_OK)
    {
        node->failures = 0;
    }
    else if (++node->failures >= node->failures_before_switch &&
             node->registry_failed != NULL)
    {
        char next[DTNMOS_MAX_URL_SIZE] = "";
        char* base = NULL;
        if (node->registry_failed(node->registry_failed_user, node->failures, next,
                                  sizeof(next)) &&
            memchr(next, '\0', sizeof(next)) != NULL && next[0] != '\0')
        {
            base = registration_base(next);
        }
        if (base != NULL)
        {
            node_log(node, DTNMOS_LOG_WARNING,
                     "The registry failed %u polls in a row; the node registers with %s.",
                     (unsigned)node->failures, next);
            NmosNode_Lock(node);
            free(node->registration);
            node->registration = base;
            forget_registration(node);
            NmosNode_Unlock(node);
            node->failures = 0;
        }
    }
    if (next_ms != NULL)
    {
        NmosNode_Lock(node);
        const uint64_t now = NmosOs_MonotonicMs();
        uint32_t wait = result != DTNMOS_OK || !node->node_registered ? 1000u : 0u;
        if (result == DTNMOS_OK && node->node_registered)
        {
            wait = node->next_heartbeat_ms > now
                       ? (uint32_t)(node->next_heartbeat_ms - now)
                       : 0u;
        }
        NmosNode_Unlock(node);
        *next_ms = wait;
    }
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- unregister_all -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Deletes from the registry everything the node registered, children before parents.
//
static void unregister_all(DtNmosNode* node)
{
    NmosNode_Lock(node);
    for (size_t i = node->sender_count; i > 0; --i)
    {
        remove_sender_at(node, i - 1);
    }
    for (size_t i = node->receiver_count; i > 0; --i)
    {
        remove_receiver_at(node, i - 1);
    }
    for (size_t i = 0; i < node->device_count; ++i)
    {
        if (node->devices[i].was_registered)
        {
            schedule_removal(node, "devices", &node->devices[i].id);
        }
    }
    const int registered = node->node_registered;
    node->node_registered = 0;
    node->closing = 1;
    if (registered)
    {
        schedule_removal(node, "nodes", &node->id);
    }
    NmosNode_Unlock(node);
    DtNmosNode_Poll(node, NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_Close(DtNmosNode* node)
{
    const DtNmosResult result = NmosNode_CheckOpen(node, "DtNmosNode_Close");
    if (result != DTNMOS_OK)
    {
        return result;
    }
    NmosServer_Stop(node);
    unregister_all(node);
    NmosNode_Release(node);
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNmosNode_Free(DtNmosNode* node)
{
    if (node == NULL)
    {
        return;
    }
    if (node->open)
    {
        DtNmosNode_Close(node);
    }
    free(node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosNode_Freep(DtNmosNode** node)
{
    if (node != NULL)
    {
        DtNmosNode_Free(*node);
        *node = NULL;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_ApiPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint16_t DtNmosNode_ApiPort(const DtNmosNode* node)
{
    return node == NULL || !node->open ? 0 : node->api_port;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_ApiUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_ApiUrl(const DtNmosNode* node, char* buffer, size_t* size)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_ApiUrl");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (size == NULL)
    {
        return NmosError_Fail(DTNMOS_E_INVALID_ARGUMENT,
                              "DtNmosNode_ApiUrl() needs a size.");
    }
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosNode_WriteBaseUrl(node, &b);
    const DtNmosResult result = b.failed
                                    ? NmosError_FailMemory()
                                    : NmosText_CopyText(buffer, size, b.data, b.length);
    NmosBuffer_Free(&b);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_AnswerError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers with an error of the APIs of NMOS, a JSON object with its code and message.
//
void NmosNode_AnswerError(DtNmosHttpResponse* response, int status, const char* message)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosBuffer_Printf(&b, "{\"code\": %d, \"error\": ", status);
    NmosJson_WriteString(&b, message);
    DTNMOS_APPEND_LITERAL(&b, ", \"debug\": null}");
    DtNmosHttpResponse_SetStatus(response, status);
    if (!b.failed)
    {
        DtNmosHttpResponse_SetBody(response, "application/json", b.data, b.length);
    }
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosNode_AnswerJson -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers with the JSON of b, or with an error when it failed.
//
void NmosNode_AnswerJson(DtNmosHttpResponse* response, NmosBuffer* b)
{
    if (b->failed)
    {
        NmosNode_AnswerError(response, 500, "Out of memory.");
        return;
    }
    DtNmosHttpResponse_SetStatus(response, 200);
    DtNmosHttpResponse_SetBody(response, "application/json", b->data, b->length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_node_api -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers the Node API; segments follow /x-nmos/node/v1.3.
//
static void answer_node_api(DtNmosNode* node, char** segments, size_t count,
                            DtNmosHttpResponse* response)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    static const char* const collections[] = {"sources", "flows", "devices", "senders",
                                              "receivers"};
    if (count == 0)
    {
        NmosBuffer_Printf(&b, "[\"self/\", \"sources/\", \"flows/\", \"devices/\", "
                              "\"senders/\", \"receivers/\"]");
        NmosNode_AnswerJson(response, &b);
        NmosBuffer_Free(&b);
        return;
    }
    if (count == 1 && strcmp(segments[0], "self") == 0)
    {
        NmosNode_WriteSelf(node, &b);
        NmosNode_AnswerJson(response, &b);
        NmosBuffer_Free(&b);
        return;
    }
    int collection = -1;
    for (int i = 0; i < 5; ++i)
    {
        if (strcmp(segments[0], collections[i]) == 0)
        {
            collection = i;
        }
    }
    if (collection < 0 || count > 2)
    {
        NmosNode_AnswerError(response, 404, "The Node API has no such resource.");
        return;
    }
    // Each collection is written whole, or only the member whose ID is segments[1].
    const char* wanted = count == 2 ? segments[1] : NULL;
    int written = 0;
    if (wanted == NULL)
    {
        DTNMOS_APPEND_LITERAL(&b, "[");
    }
#define DTNMOS_WRITE_MEMBER(member_id, write)                                            \
    if (wanted == NULL || strcmp((member_id)->Text, wanted) == 0)                        \
    {                                                                                    \
        if (wanted == NULL && written > 0)                                               \
        {                                                                                \
            DTNMOS_APPEND_LITERAL(&b, ", ");                                             \
        }                                                                                \
        write;                                                                           \
        ++written;                                                                       \
    }
    switch (collection)
    {
    case 0:
        for (size_t i = 0; i < node->sender_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->senders[i].source_id,
                                NmosNode_WriteSource(&node->senders[i], &b))
        }
        break;
    case 1:
        for (size_t i = 0; i < node->sender_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->senders[i].flow_id,
                                NmosNode_WriteFlow(&node->senders[i], &b))
        }
        break;
    case 2:
        for (size_t i = 0; i < node->device_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->devices[i].id,
                                NmosNode_WriteDevice(node, &node->devices[i], &b))
        }
        break;
    case 3:
        for (size_t i = 0; i < node->sender_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->senders[i].id,
                                NmosNode_WriteSender(node, &node->senders[i], &b))
        }
        break;
    default:
        for (size_t i = 0; i < node->receiver_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->receivers[i].id,
                                NmosNode_WriteReceiver(&node->receivers[i], &b))
        }
        break;
    }
#undef DTNMOS_WRITE_MEMBER
    if (wanted != NULL && written == 0)
    {
        NmosBuffer_Free(&b);
        NmosNode_AnswerError(response, 404, "The node has no resource of that ID.");
        return;
    }
    if (wanted == NULL)
    {
        DTNMOS_APPEND_LITERAL(&b, "]");
    }
    NmosNode_AnswerJson(response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_names -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers a list of the names of the next level of an API.
//
static void answer_names(DtNmosHttpResponse* response, const char* names)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosBuffer_Append(&b, names, strlen(names));
    NmosNode_AnswerJson(response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Handle(DtNmosNode* node, const DtNmosHttpRequest* request,
                               DtNmosHttpResponse* response)
{
    const DtNmosResult open = NmosNode_CheckOpen(node, "DtNmosNode_Handle");
    if (open != DTNMOS_OK)
    {
        return open;
    }
    if (request == NULL || request->Url == NULL || request->Method == NULL ||
        response == NULL)
    {
        return NmosError_Fail(
            DTNMOS_E_INVALID_ARGUMENT,
            "DtNmosNode_Handle() needs a node, a request and a response.");
    }
    const DtNmosResult sized =
        DTNMOS_CHECK_SIZE(request, DtNmosHttpRequest, sizeof(DtNmosHttpRequest));
    if (sized != DTNMOS_OK)
    {
        return sized;
    }
    // The path without its query, split into its segments.
    char path[512];
    snprintf(path, sizeof(path), "%s", request->Url);
    path[strcspn(path, "?#")] = '\0';
    char* segments[16];
    size_t count = 0;
    for (char* part = strtok(path, "/"); part != NULL && count < 16;
         part = strtok(NULL, "/"))
    {
        segments[count++] = part;
    }
    const int get =
        strcmp(request->Method, "GET") == 0 || strcmp(request->Method, "HEAD") == 0;
    if (count >= 3 && strcmp(segments[0], "x-nmos") == 0 &&
        strcmp(segments[1], "connection") == 0 && strcmp(segments[2], "v1.1") == 0)
    {
        return NmosConnection_Handle(node, request, segments + 3, count - 3, response);
    }
    if (!get)
    {
        NmosNode_AnswerError(response, 405, "The Node API answers GET only.");
        return DTNMOS_OK;
    }
    NmosNode_Lock(node);
    if (count == 0)
    {
        answer_names(response, "[\"x-nmos/\"]");
    }
    else if (count == 1 && strcmp(segments[0], "x-nmos") == 0)
    {
        answer_names(response, "[\"node/\", \"connection/\"]");
    }
    else if (count == 2 && strcmp(segments[0], "x-nmos") == 0 &&
             strcmp(segments[1], "node") == 0)
    {
        answer_names(response, "[\"v1.3/\"]");
    }
    else if (count == 2 && strcmp(segments[0], "x-nmos") == 0 &&
             strcmp(segments[1], "connection") == 0)
    {
        answer_names(response, "[\"v1.1/\"]");
    }
    else if (count >= 3 && strcmp(segments[0], "x-nmos") == 0 &&
             strcmp(segments[1], "node") == 0 && strcmp(segments[2], "v1.3") == 0)
    {
        answer_node_api(node, segments + 3, count - 3, response);
    }
    else
    {
        NmosNode_AnswerError(response, 404, "The node has no such API.");
    }
    NmosNode_Unlock(node);
    return DTNMOS_OK;
}
