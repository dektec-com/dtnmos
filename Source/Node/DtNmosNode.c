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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_lock(DtNmosNode* node)
{
    dtnmos_mutex_lock(node->mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_unlock(DtNmosNode* node)
{
    dtnmos_mutex_unlock(node->mutex);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
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
    dtnmos_buffer base;
    memset(&base, 0, sizeof(base));
    dtnmos_buffer_append(&base, url, length);
    DTNMOS_APPEND_LITERAL(&base, "/x-nmos/registration/v1.3/");
    if (base.failed)
    {
        dtnmos_buffer_free(&base);
        return NULL;
    }
    return base.data;
}

DtNmosResult DtNmosNode_Create(const DtNmosNodeConfig* config, DtNmosNode** node,
                               DtNmosError* error)
{
    if (node == NULL || config == NULL || config->Id.Text[0] == '\0' ||
        config->RegistrationUrl == NULL || config->RegistrationUrl[0] == '\0' ||
        config->Http == NULL)
    {
        return dtnmos_fail(
            error, DTNMOS_E_INVALID_ARGUMENT,
            "A node needs an ID, the URL of a registry and an HTTP function.");
    }
    *node = NULL;
    if (config->ApiVersion != NULL && strcmp(config->ApiVersion, "v1.3") != 0)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "IS-04 %s is not supported; dtnmos speaks v1.3.",
                           config->ApiVersion);
    }
    DtNmosNode* result = calloc(1, sizeof(*result));
    if (result == NULL)
    {
        return dtnmos_fail_memory(error);
    }
    result->mutex = dtnmos_mutex_create();
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
            !dtnmos_address_toward(registry, host, sizeof(host)))
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
        dtnmos_node_free(result);
        return dtnmos_fail_memory(error);
    }
    dtnmos_version_now(&result->last_version, result->version, sizeof(result->version));
    *node = result;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void free_sender(node_sender* sender)
{
    free(sender->label);
    free(sender->description);
    free(sender->source_ip);
    DtNmosFlow_Clear(&sender->flow);
    dtnmos_connection_clear_sender(sender);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void free_receiver(node_receiver* receiver)
{
    free(receiver->label);
    free(receiver->description);
    dtnmos_connection_clear_receiver(receiver);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_free(DtNmosNode* node)
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
    dtnmos_mutex_free(node->mutex);
    free(node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- registry_request -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Performs a request to the Registration API; path follows its base. Returns the status
// in status, or fails when no answer came.
//
static DtNmosResult registry_request(DtNmosNode* node, const char* method,
                                     const char* path, const char* body, int* status,
                                     DtNmosError* error)
{
    dtnmos_buffer url;
    memset(&url, 0, sizeof(url));
    dtnmos_buffer_printf(&url, "%s%s", node->registration, path);
    DtNmosHttpResponse* response = DtNmosHttpResponse_Create();
    if (url.failed || response == NULL)
    {
        dtnmos_buffer_free(&url);
        DtNmosHttpResponse_Free(response);
        return dtnmos_fail_memory(error);
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
    if (error != NULL)
    {
        error->Message[0] = '\0';
    }
    DtNmosResult result = node->http(node->http_user, &request, response, error);
    if (result == DTNMOS_OK)
    {
        *status = DtNmosHttpResponse_Status(response);
    }
    else if (error != NULL && error->Message[0] == '\0')
    {
        dtnmos_fail(error, result, "%s %s failed.", method, url.data);
    }
    dtnmos_buffer_free(&url);
    DtNmosHttpResponse_Free(response);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- register_resource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Registers a resource of type with the JSON data; 200 and 201 are both success.
//
static DtNmosResult register_resource(DtNmosNode* node, const char* type,
                                      const char* data, DtNmosError* error)
{
    dtnmos_buffer body;
    memset(&body, 0, sizeof(body));
    dtnmos_buffer_printf(&body, "{\"type\": \"%s\", \"data\": %s}", type, data);
    if (body.failed)
    {
        dtnmos_buffer_free(&body);
        return dtnmos_fail_memory(error);
    }
    int status = 0;
    DtNmosResult result =
        registry_request(node, "POST", "resource", body.data, &status, error);
    dtnmos_buffer_free(&body);
    if (result == DTNMOS_OK && status != 200 && status != 201)
    {
        result =
            dtnmos_fail(error, DTNMOS_E_HTTP,
                        "The registry answered %d to registering a %s.", status, type);
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
static void write_common(dtnmos_buffer* b, const DtNmosId* id, const char* version,
                         const char* label, const char* description)
{
    dtnmos_buffer_printf(b, "\"id\": \"%s\", \"version\": \"%s\", \"label\": ", id->Text,
                         version);
    dtnmos_json_write_string(b, label);
    DTNMOS_APPEND_LITERAL(b, ", \"description\": ");
    dtnmos_json_write_string(b, description);
    DTNMOS_APPEND_LITERAL(b, ", \"tags\": {}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_base_url -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_base_url(const DtNmosNode* node, dtnmos_buffer* b)
{
    const int ipv6 = strchr(node->api_host, ':') != NULL;
    dtnmos_buffer_printf(b, "http://%s%s%s:%u", ipv6 ? "[" : "", node->api_host,
                         ipv6 ? "]" : "", (unsigned)node->api_port);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_self -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_self(const DtNmosNode* node, dtnmos_buffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &node->id, node->version, node->label, node->description);
    DTNMOS_APPEND_LITERAL(b, ", \"href\": \"");
    dtnmos_node_write_base_url(node, b);
    DTNMOS_APPEND_LITERAL(b, "/\", \"hostname\": ");
    dtnmos_json_write_string(b, node->hostname);
    DTNMOS_APPEND_LITERAL(
        b, ", \"api\": {\"versions\": [\"v1.3\"], \"endpoints\": [{\"host\": ");
    dtnmos_json_write_string(b, node->api_host);
    dtnmos_buffer_printf(
        b,
        ", \"port\": %u, \"protocol\": \"http\"}]}, \"caps\": {}, \"services\": "
        "[], \"clocks\": [{\"name\": \"clk0\", \"ref_type\": \"internal\"}], "
        "\"interfaces\": []}",
        (unsigned)node->api_port);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_device -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_device(const DtNmosNode* node, const node_device* device,
                              dtnmos_buffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &device->id, device->version, device->label, device->description);
    dtnmos_buffer_printf(
        b,
        ", \"type\": \"urn:x-nmos:device:generic\", \"node_id\": \"%s\", "
        "\"senders\": [",
        node->id.Text);
    int first = 1;
    for (size_t i = 0; i < node->sender_count; ++i)
    {
        if (strcmp(node->senders[i].device_id.Text, device->id.Text) == 0)
        {
            dtnmos_buffer_printf(b, "%s\"%s\"", first ? "" : ", ",
                                 node->senders[i].id.Text);
            first = 0;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "], \"receivers\": [");
    first = 1;
    for (size_t i = 0; i < node->receiver_count; ++i)
    {
        if (strcmp(node->receivers[i].device_id.Text, device->id.Text) == 0)
        {
            dtnmos_buffer_printf(b, "%s\"%s\"", first ? "" : ", ",
                                 node->receivers[i].id.Text);
            first = 0;
        }
    }
    DTNMOS_APPEND_LITERAL(b, "], \"controls\": [{\"href\": \"");
    dtnmos_node_write_base_url(node, b);
    DTNMOS_APPEND_LITERAL(b, "/x-nmos/connection/v1.1/\", \"type\": "
                             "\"urn:x-nmos:control:sr-ctrl/v1.1\"}]}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- is_video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int is_video(const node_sender* sender)
{
    return sender->flow.Media == DTNMOS_MEDIA_VIDEO;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_source -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_source(const node_sender* sender, dtnmos_buffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &sender->source_id, sender->version, sender->label,
                 sender->description);
    dtnmos_buffer_printf(
        b,
        ", \"format\": \"urn:x-nmos:format:%s\", \"caps\": {}, "
        "\"device_id\": \"%s\", \"parents\": [], \"clock_name\": \"clk0\"",
        is_video(sender) ? "video" : "audio", sender->device_id.Text);
    if (is_video(sender))
    {
        const DtNmosVideoFormat* video = &sender->flow.Format.Video;
        dtnmos_buffer_printf(
            b, ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}}",
            (unsigned)video->RateNumerator,
            (unsigned)(video->RateDenominator == 0 ? 1 : video->RateDenominator));
        return;
    }
    DTNMOS_APPEND_LITERAL(b, ", \"channels\": [");
    for (uint32_t c = 0; c < sender->flow.Format.Audio.Channels; ++c)
    {
        dtnmos_buffer_printf(b, "%s{\"label\": \"Channel %u\"}", c == 0 ? "" : ", ",
                             (unsigned)(c + 1));
    }
    DTNMOS_APPEND_LITERAL(b, "]}");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- or_default -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns text, or fallback when it is empty.
//
static const char* or_default(const DtNmosString* text, const char* fallback)
{
    return DtNmosString_Length(text) > 0 ? DtNmosString_Get(text) : fallback;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_flow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_flow(const node_sender* sender, dtnmos_buffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &sender->flow_id, sender->version, sender->label,
                 sender->description);
    dtnmos_buffer_printf(b,
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
        dtnmos_buffer_printf(
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
            height, or_default(&video->Colorimetry, "BT709"),
            video->Interlaced ? "interlaced_tff" : "progressive",
            or_default(&video->Tcs, "SDR"), width, height, depth, width / 2, height,
            depth, width / 2, height, depth);
        return;
    }
    const DtNmosAudioFormat* audio = &sender->flow.Format.Audio;
    const int l16 = strcmp(DtNmosString_Get(&audio->Encoding), "L16") == 0;
    dtnmos_buffer_printf(
        b,
        ", \"sample_rate\": {\"numerator\": %u}, \"media_type\": \"audio/%s\", "
        "\"bit_depth\": %d}",
        (unsigned)audio->SampleRate, l16 ? "L16" : "L24", l16 ? 16 : 24);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_sender(const DtNmosNode* node, const node_sender* sender,
                              dtnmos_buffer* b)
{
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &sender->id, sender->version, sender->label, sender->description);
    dtnmos_buffer_printf(
        b,
        ", \"flow_id\": \"%s\", \"transport\": \"urn:x-nmos:transport:%s\", "
        "\"device_id\": \"%s\", \"manifest_href\": \"",
        sender->flow_id.Text,
        dtnmos_is_multicast(DtNmosString_Get(&sender->flow.DestinationIp)) ? "rtp.mcast"
                                                                           : "rtp.ucast",
        sender->device_id.Text);
    dtnmos_node_write_base_url(node, b);
    dtnmos_buffer_printf(
        b,
        "/x-nmos/connection/v1.1/single/senders/%s/transportfile\", "
        "\"interface_bindings\": [], \"subscription\": {\"receiver_id\": ",
        sender->id.Text);
    if (sender->receiver_id.Text[0] != '\0')
    {
        dtnmos_buffer_printf(b, "\"%s\"", sender->receiver_id.Text);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    dtnmos_buffer_printf(b, ", \"active\": %s}}",
                         sender->master_enable ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void dtnmos_node_write_receiver(const node_receiver* receiver, dtnmos_buffer* b)
{
    const int video = receiver->media == DTNMOS_MEDIA_VIDEO;
    DTNMOS_APPEND_LITERAL(b, "{");
    write_common(b, &receiver->id, receiver->version, receiver->label,
                 receiver->description);
    dtnmos_buffer_printf(
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
        dtnmos_buffer_printf(b, "\"%s\"", receiver->sender_id.Text);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    dtnmos_buffer_printf(b, ", \"active\": %s}}",
                         receiver->master_enable ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_is_multicast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int dtnmos_is_multicast(const char* address)
{
    if (strchr(address, ':') != NULL)
    {
        return (address[0] == 'f' || address[0] == 'F') &&
               (address[1] == 'f' || address[1] == 'F');
    }
    const int first = atoi(address);
    return first >= 224 && first <= 239;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_write_transport_file -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult dtnmos_node_write_transport_file(const node_sender* sender,
                                              DtNmosString* text, DtNmosError* error)
{
    DtNmosSession session;
    memset(&session, 0, sizeof(session));
    session.Size = sizeof(session);
    session.SessionId = sender->session_id;
    session.SessionVersion = sender->session_version;
    if (DtNmosString_SetText(&session.Name, sender->label) != DTNMOS_OK ||
        DtNmosString_SetText(&session.OriginIp, sender->source_ip[0] != '\0'
                                                    ? sender->source_ip
                                                    : "0.0.0.0") != DTNMOS_OK)
    {
        DtNmosSession_Clear(&session);
        return dtnmos_fail_memory(error);
    }
    const DtNmosResult result = DtNmosSdp_Write(&session, &sender->flow, 1, text, error);
    DtNmosSession_Clear(&session);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- new_version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void new_version(DtNmosNode* node, char* version, size_t size)
{
    dtnmos_version_now(&node->last_version, version, size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_find_device -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
node_device* dtnmos_node_find_device(DtNmosNode* node, const DtNmosId* id)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_find_sender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
node_sender* dtnmos_node_find_sender(DtNmosNode* node, const DtNmosId* id)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_find_receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
node_receiver* dtnmos_node_find_receiver(DtNmosNode* node, const DtNmosId* id)
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
           dtnmos_node_find_device(node, id) != NULL ||
           dtnmos_node_find_sender(node, id) != NULL ||
           dtnmos_node_find_receiver(node, id) != NULL;
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
    node_device* device = dtnmos_node_find_device(node, id);
    if (device != NULL)
    {
        new_version(node, device->version, sizeof(device->version));
        device->registered = 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddDevice(DtNmosNode* node, const DtNmosDeviceConfig* device,
                                  DtNmosError* error)
{
    if (node == NULL || device == NULL || device->Id.Text[0] == '\0')
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "A device needs an ID.");
    }
    dtnmos_node_lock(node);
    DtNmosResult result = DTNMOS_OK;
    if (id_taken(node, &device->Id))
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                             device->Id.Text);
    }
    else if (!grow((void**)&node->devices, &node->device_capacity, node->device_count,
                   sizeof(*node->devices)))
    {
        result = dtnmos_fail_memory(error);
    }
    else
    {
        node_device* added = &node->devices[node->device_count];
        memset(added, 0, sizeof(*added));
        added->id = device->Id;
        added->label = copy_text(device->Label);
        added->description = copy_text(device->Description);
        if (added->label == NULL || added->description == NULL)
        {
            free(added->label);
            free(added->description);
            result = dtnmos_fail_memory(error);
        }
        else
        {
            new_version(node, added->version, sizeof(added->version));
            ++node->device_count;
        }
    }
    dtnmos_node_unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- derived_id -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes the ID of a resource that a sender brings along, from the ID of the sender.
//
static void derived_id(const DtNmosId* sender, const char* what, DtNmosId* id)
{
    if (DtNmosId_FromName(sender, what, id, NULL) != DTNMOS_OK)
    {
        memset(id, 0, sizeof(*id));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddSender(DtNmosNode* node, const DtNmosSenderConfig* sender,
                                  DtNmosSenderActivateFunc activate, void* user,
                                  DtNmosError* error)
{
    if (node == NULL || sender == NULL || sender->Id.Text[0] == '\0' ||
        sender->Flow == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "A sender needs an ID and a flow.");
    }
    if (sender->Flow->Media != DTNMOS_MEDIA_VIDEO &&
        sender->Flow->Media != DTNMOS_MEDIA_AUDIO)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "A sender of the node sends video or audio, not %s.",
                           DtNmosMedia_Name(sender->Flow->Media));
    }
    dtnmos_node_lock(node);
    DtNmosResult result = DTNMOS_OK;
    if (id_taken(node, &sender->Id))
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                             sender->Id.Text);
    }
    else if (dtnmos_node_find_device(node, &sender->DeviceId) == NULL)
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                             "The node has no device %s.", sender->DeviceId.Text);
    }
    else if (!grow((void**)&node->senders, &node->sender_capacity, node->sender_count,
                   sizeof(*node->senders)))
    {
        result = dtnmos_fail_memory(error);
    }
    else
    {
        node_sender* added = &node->senders[node->sender_count];
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
            DtNmosFlow_Copy(&added->flow, sender->Flow) != DTNMOS_OK ||
            dtnmos_connection_init_sender(added) != DTNMOS_OK)
        {
            free_sender(added);
            result = dtnmos_fail_memory(error);
        }
        else
        {
            new_version(node, added->version, sizeof(added->version));
            ++node->sender_count;
            touch_device(node, &sender->DeviceId);
        }
    }
    dtnmos_node_unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_AddReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosResult DtNmosNode_AddReceiver(DtNmosNode* node,
                                    const DtNmosReceiverConfig* receiver,
                                    DtNmosReceiverActivateFunc activate, void* user,
                                    DtNmosError* error)
{
    if (node == NULL || receiver == NULL || receiver->Id.Text[0] == '\0')
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "A receiver needs an ID.");
    }
    if (receiver->Media != DTNMOS_MEDIA_VIDEO && receiver->Media != DTNMOS_MEDIA_AUDIO)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "A receiver of the node receives video or audio, not %s.",
                           DtNmosMedia_Name(receiver->Media));
    }
    dtnmos_node_lock(node);
    DtNmosResult result = DTNMOS_OK;
    if (id_taken(node, &receiver->Id))
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                             receiver->Id.Text);
    }
    else if (dtnmos_node_find_device(node, &receiver->DeviceId) == NULL)
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                             "The node has no device %s.", receiver->DeviceId.Text);
    }
    else if (!grow((void**)&node->receivers, &node->receiver_capacity,
                   node->receiver_count, sizeof(*node->receivers)))
    {
        result = dtnmos_fail_memory(error);
    }
    else
    {
        node_receiver* added = &node->receivers[node->receiver_count];
        memset(added, 0, sizeof(*added));
        added->id = receiver->Id;
        added->device_id = receiver->DeviceId;
        added->media = receiver->Media;
        added->label = copy_text(receiver->Label);
        added->description = copy_text(receiver->Description);
        added->activate = activate;
        added->user = user;
        if (added->label == NULL || added->description == NULL ||
            dtnmos_connection_init_receiver(added) != DTNMOS_OK)
        {
            free_receiver(added);
            result = dtnmos_fail_memory(error);
        }
        else
        {
            new_version(node, added->version, sizeof(added->version));
            ++node->receiver_count;
            touch_device(node, &receiver->DeviceId);
        }
    }
    dtnmos_node_unlock(node);
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
    node_removal* removal = &node->removals[node->removal_count++];
    snprintf(removal->type, sizeof(removal->type), "%s", type);
    removal->id = *id;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- remove_sender_at -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void remove_sender_at(DtNmosNode* node, size_t index)
{
    node_sender* sender = &node->senders[index];
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
    node_receiver* receiver = &node->receivers[index];
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
DtNmosResult DtNmosNode_Remove(DtNmosNode* node, const DtNmosId* id, DtNmosError* error)
{
    if (node == NULL || id == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosNode_Remove() needs an ID.");
    }
    dtnmos_node_lock(node);
    DtNmosResult result = DTNMOS_OK;
    node_device* device = dtnmos_node_find_device(node, id);
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
        device = dtnmos_node_find_device(node, id);
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
    else if (dtnmos_node_find_sender(node, id) != NULL)
    {
        remove_sender_at(node,
                         (size_t)(dtnmos_node_find_sender(node, id) - node->senders));
    }
    else if (dtnmos_node_find_receiver(node, id) != NULL)
    {
        remove_receiver_at(
            node, (size_t)(dtnmos_node_find_receiver(node, id) - node->receivers));
    }
    else
    {
        result = dtnmos_fail(error, DTNMOS_E_NOT_FOUND, "The node has no %s.", id->Text);
    }
    dtnmos_node_unlock(node);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_UpdateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_UpdateSender(DtNmosNode* node, const DtNmosId* id,
                                     const DtNmosFlow* flow, DtNmosError* error)
{
    if (node == NULL || id == NULL || flow == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosNode_UpdateSender() needs an ID and a flow.");
    }
    dtnmos_node_lock(node);
    DtNmosResult result = DTNMOS_OK;
    node_sender* sender = dtnmos_node_find_sender(node, id);
    if (sender == NULL)
    {
        result = dtnmos_fail(error, DTNMOS_E_NOT_FOUND, "The node has no sender %s.",
                             id->Text);
    }
    else if (flow->Media != sender->flow.Media)
    {
        result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                             "Sender %s sends %s and cannot send %s.", id->Text,
                             DtNmosMedia_Name(sender->flow.Media),
                             DtNmosMedia_Name(flow->Media));
    }
    else if (DtNmosFlow_Copy(&sender->flow, flow) != DTNMOS_OK)
    {
        result = dtnmos_fail_memory(error);
    }
    else
    {
        new_version(node, sender->version, sizeof(sender->version));
        ++sender->session_version;
        sender->registered = 0;
    }
    dtnmos_node_unlock(node);
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
typedef struct pending
{
    int kind; // 0 node, 1 device, 2 sender with source and flow, 3 receiver
    DtNmosId id;
    char version[32];
    char* bodies[3];
    const char* types[3];
    size_t count;
} pending;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- free_pending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void free_pending(pending* p)
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
static int next_pending(DtNmosNode* node, pending* p)
{
    memset(p, 0, sizeof(*p));
    dtnmos_buffer b[3];
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
        dtnmos_node_write_self(node, &b[0]);
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
                dtnmos_node_write_device(node, &node->devices[i], &b[0]);
                p->types[0] = "device";
                p->count = 1;
            }
        }
        for (size_t i = 0; i < node->sender_count && p->count == 0; ++i)
        {
            const node_sender* sender = &node->senders[i];
            const node_device* device = dtnmos_node_find_device(node, &sender->device_id);
            if (!sender->registered && device != NULL && device->registered)
            {
                p->kind = 2;
                p->id = sender->id;
                snprintf(p->version, sizeof(p->version), "%s", sender->version);
                dtnmos_node_write_source(sender, &b[0]);
                dtnmos_node_write_flow(sender, &b[1]);
                dtnmos_node_write_sender(node, sender, &b[2]);
                p->types[0] = "source";
                p->types[1] = "flow";
                p->types[2] = "sender";
                p->count = 3;
            }
        }
        for (size_t i = 0; i < node->receiver_count && p->count == 0; ++i)
        {
            const node_receiver* receiver = &node->receivers[i];
            const node_device* device =
                dtnmos_node_find_device(node, &receiver->device_id);
            if (!receiver->registered && device != NULL && device->registered)
            {
                p->kind = 3;
                p->id = receiver->id;
                snprintf(p->version, sizeof(p->version), "%s", receiver->version);
                dtnmos_node_write_receiver(receiver, &b[0]);
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
            dtnmos_buffer_free(&b[i]);
        }
    }
    return p->count > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- mark_registered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Marks what p registered, unless it changed meanwhile.
//
static void mark_registered(DtNmosNode* node, const pending* p)
{
    if (p->kind == 0 && strcmp(node->version, p->version) == 0)
    {
        node->node_registered = 1;
    }
    node_device* device = p->kind == 1 ? dtnmos_node_find_device(node, &p->id) : NULL;
    if (device != NULL && strcmp(device->version, p->version) == 0)
    {
        device->registered = 1;
        device->was_registered = 1;
    }
    node_sender* sender = p->kind == 2 ? dtnmos_node_find_sender(node, &p->id) : NULL;
    if (sender != NULL && strcmp(sender->version, p->version) == 0)
    {
        sender->registered = 1;
        sender->was_registered = 1;
    }
    node_receiver* receiver =
        p->kind == 3 ? dtnmos_node_find_receiver(node, &p->id) : NULL;
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
    if (node == NULL)
    {
        return 0;
    }
    DtNmosNode* mutable_node = (DtNmosNode*)node;
    dtnmos_node_lock(mutable_node);
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
    dtnmos_node_unlock(mutable_node);
    return all;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Poll(DtNmosNode* node, uint32_t* next_ms, DtNmosError* error)
{
    if (node == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosNode_Poll() needs a node.");
    }
    DtNmosResult result = DTNMOS_OK;
    // Deletions first, children before parents as they were scheduled.
    for (;;)
    {
        dtnmos_node_lock(node);
        if (node->removal_count == 0)
        {
            dtnmos_node_unlock(node);
            break;
        }
        const node_removal removal = node->removals[0];
        dtnmos_node_unlock(node);
        char path[96];
        snprintf(path, sizeof(path), "resource/%s/%s", removal.type, removal.id.Text);
        int status = 0;
        result = registry_request(node, "DELETE", path, NULL, &status, error);
        if (result != DTNMOS_OK)
        {
            break;
        }
        dtnmos_node_lock(node);
        if (node->removal_count > 0 &&
            strcmp(node->removals[0].id.Text, removal.id.Text) == 0)
        {
            memmove(node->removals, node->removals + 1,
                    (node->removal_count - 1) * sizeof(*node->removals));
            --node->removal_count;
        }
        dtnmos_node_unlock(node);
    }
    // Registrations, one resource at a time, each rendered anew under the lock.
    while (result == DTNMOS_OK)
    {
        pending p;
        dtnmos_node_lock(node);
        const int any = next_pending(node, &p);
        dtnmos_node_unlock(node);
        if (!any)
        {
            break;
        }
        for (size_t i = 0; result == DTNMOS_OK && i < p.count; ++i)
        {
            result = p.bodies[i] == NULL
                         ? dtnmos_fail_memory(error)
                         : register_resource(node, p.types[i], p.bodies[i], error);
        }
        if (result == DTNMOS_OK)
        {
            dtnmos_node_lock(node);
            mark_registered(node, &p);
            if (p.kind == 0)
            {
                node->next_heartbeat_ms = dtnmos_monotonic_ms() + node->heartbeat_ms;
            }
            dtnmos_node_unlock(node);
        }
        free_pending(&p);
    }
    // The heartbeat, when it is due.
    dtnmos_node_lock(node);
    const int beat = result == DTNMOS_OK && node->node_registered &&
                     dtnmos_monotonic_ms() >= node->next_heartbeat_ms;
    dtnmos_node_unlock(node);
    if (beat)
    {
        char path[96];
        snprintf(path, sizeof(path), "health/nodes/%s", node->id.Text);
        int status = 0;
        result = registry_request(node, "POST", path, NULL, &status, error);
        dtnmos_node_lock(node);
        if (result == DTNMOS_OK && status == 404)
        {
            node_log(node, DTNMOS_LOG_WARNING,
                     "The registry has lost node %s; it registers again.", node->id.Text);
            forget_registration(node);
        }
        else if (result == DTNMOS_OK && status != 200)
        {
            result = dtnmos_fail(error, DTNMOS_E_HTTP,
                                 "The registry answered %d to a heartbeat.", status);
        }
        node->next_heartbeat_ms = dtnmos_monotonic_ms() + node->heartbeat_ms;
        dtnmos_node_unlock(node);
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
        DtNmosString next = {0};
        char* base = NULL;
        if (node->registry_failed(node->registry_failed_user, node->failures, &next) &&
            DtNmosString_Length(&next) > 0)
        {
            base = registration_base(DtNmosString_Get(&next));
        }
        if (base != NULL)
        {
            node_log(node, DTNMOS_LOG_WARNING,
                     "The registry failed %u polls in a row; the node registers with %s.",
                     (unsigned)node->failures, DtNmosString_Get(&next));
            dtnmos_node_lock(node);
            free(node->registration);
            node->registration = base;
            forget_registration(node);
            dtnmos_node_unlock(node);
            node->failures = 0;
        }
        DtNmosString_Clear(&next);
    }
    if (next_ms != NULL)
    {
        dtnmos_node_lock(node);
        const uint64_t now = dtnmos_monotonic_ms();
        uint32_t wait = result != DTNMOS_OK || !node->node_registered ? 1000u : 0u;
        if (result == DTNMOS_OK && node->node_registered)
        {
            wait = node->next_heartbeat_ms > now
                       ? (uint32_t)(node->next_heartbeat_ms - now)
                       : 0u;
        }
        dtnmos_node_unlock(node);
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
    dtnmos_node_lock(node);
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
    dtnmos_node_unlock(node);
    DtNmosError ignored;
    DtNmosNode_Poll(node, NULL, &ignored);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNmosNode_Destroy(DtNmosNode* node)
{
    if (node == NULL)
    {
        return;
    }
    dtnmos_server_stop(node);
    unregister_all(node);
    dtnmos_node_free(node);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_ApiPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint16_t DtNmosNode_ApiPort(const DtNmosNode* node)
{
    return node == NULL ? 0 : node->api_port;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_ApiUrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_ApiUrl(const DtNmosNode* node, DtNmosString* url)
{
    if (node == NULL || url == NULL)
    {
        return DTNMOS_E_INVALID_ARGUMENT;
    }
    dtnmos_buffer b;
    memset(&b, 0, sizeof(b));
    dtnmos_node_write_base_url(node, &b);
    const DtNmosResult result =
        b.failed ? DTNMOS_E_NO_MEMORY : DtNmosString_Set(url, b.data, b.length);
    dtnmos_buffer_free(&b);
    return result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_answer_error -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers with an error of the APIs of NMOS, a JSON object with its code and message.
//
void dtnmos_node_answer_error(DtNmosHttpResponse* response, int status,
                              const char* message)
{
    dtnmos_buffer b;
    memset(&b, 0, sizeof(b));
    dtnmos_buffer_printf(&b, "{\"code\": %d, \"error\": ", status);
    dtnmos_json_write_string(&b, message);
    DTNMOS_APPEND_LITERAL(&b, ", \"debug\": null}");
    DtNmosHttpResponse_SetStatus(response, status);
    if (!b.failed)
    {
        DtNmosHttpResponse_SetBody(response, "application/json", b.data, b.length);
    }
    dtnmos_buffer_free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- dtnmos_node_answer_json -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers with the JSON of b, or with an error when it failed.
//
void dtnmos_node_answer_json(DtNmosHttpResponse* response, dtnmos_buffer* b)
{
    if (b->failed)
    {
        dtnmos_node_answer_error(response, 500, "Out of memory.");
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
    dtnmos_buffer b;
    memset(&b, 0, sizeof(b));
    static const char* const collections[] = {"sources", "flows", "devices", "senders",
                                              "receivers"};
    if (count == 0)
    {
        dtnmos_buffer_printf(&b, "[\"self/\", \"sources/\", \"flows/\", \"devices/\", "
                                 "\"senders/\", \"receivers/\"]");
        dtnmos_node_answer_json(response, &b);
        dtnmos_buffer_free(&b);
        return;
    }
    if (count == 1 && strcmp(segments[0], "self") == 0)
    {
        dtnmos_node_write_self(node, &b);
        dtnmos_node_answer_json(response, &b);
        dtnmos_buffer_free(&b);
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
        dtnmos_node_answer_error(response, 404, "The Node API has no such resource.");
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
                                dtnmos_node_write_source(&node->senders[i], &b))
        }
        break;
    case 1:
        for (size_t i = 0; i < node->sender_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->senders[i].flow_id,
                                dtnmos_node_write_flow(&node->senders[i], &b))
        }
        break;
    case 2:
        for (size_t i = 0; i < node->device_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->devices[i].id,
                                dtnmos_node_write_device(node, &node->devices[i], &b))
        }
        break;
    case 3:
        for (size_t i = 0; i < node->sender_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->senders[i].id,
                                dtnmos_node_write_sender(node, &node->senders[i], &b))
        }
        break;
    default:
        for (size_t i = 0; i < node->receiver_count; ++i)
        {
            DTNMOS_WRITE_MEMBER(&node->receivers[i].id,
                                dtnmos_node_write_receiver(&node->receivers[i], &b))
        }
        break;
    }
#undef DTNMOS_WRITE_MEMBER
    if (wanted != NULL && written == 0)
    {
        dtnmos_buffer_free(&b);
        dtnmos_node_answer_error(response, 404, "The node has no resource of that ID.");
        return;
    }
    if (wanted == NULL)
    {
        DTNMOS_APPEND_LITERAL(&b, "]");
    }
    dtnmos_node_answer_json(response, &b);
    dtnmos_buffer_free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- answer_names -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers a list of the names of the next level of an API.
//
static void answer_names(DtNmosHttpResponse* response, const char* names)
{
    dtnmos_buffer b;
    memset(&b, 0, sizeof(b));
    dtnmos_buffer_append(&b, names, strlen(names));
    dtnmos_node_answer_json(response, &b);
    dtnmos_buffer_free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosNode_Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult DtNmosNode_Handle(DtNmosNode* node, const DtNmosHttpRequest* request,
                               DtNmosHttpResponse* response, DtNmosError* error)
{
    if (node == NULL || request == NULL || request->Url == NULL ||
        request->Method == NULL || response == NULL)
    {
        return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                           "DtNmosNode_Handle() needs a node, a request and a response.");
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
        return dtnmos_connection_handle(node, request, segments + 3, count - 3, response,
                                        error);
    }
    if (!get)
    {
        dtnmos_node_answer_error(response, 405, "The Node API answers GET only.");
        return DTNMOS_OK;
    }
    dtnmos_node_lock(node);
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
        dtnmos_node_answer_error(response, 404, "The node has no such API.");
    }
    dtnmos_node_unlock(node);
    return DTNMOS_OK;
}
