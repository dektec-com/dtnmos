// SPDX-License-Identifier: BSD-3-Clause
//
// The NMOS node: its resources as IS-04 v1.3 describes them, their registration with the
// Registration API of a registry, the heartbeats that keep them there, and the answers of
// the Node API and the transport files of the senders. The Connection API is in
// connection.c. A mutex guards the node; requests to the registry are made without it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "node-internal.h"
#include "platform.h"

void dtnmos_node_lock(dtnmos_node* node)
{
  dtnmos_mutex_lock(node->mutex);
}

void dtnmos_node_unlock(dtnmos_node* node)
{
  dtnmos_mutex_unlock(node->mutex);
}

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

static void node_log(dtnmos_node* node, dtnmos_log_level level, const char* format, ...)
    DTNMOS_PRINTF(3, 4);

static void node_log(dtnmos_node* node, dtnmos_log_level level, const char* format, ...)
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

// Writes the host of an URL, without brackets around an IPv6 address, into host.
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

dtnmos_result dtnmos_node_create(const dtnmos_node_config* config, dtnmos_node** node,
                                 dtnmos_error* error)
{
  if (node == NULL || config == NULL || config->id.text[0] == '\0' ||
      config->registration_url == NULL || config->registration_url[0] == '\0' ||
      config->http == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "A node needs an ID, the URL of a registry and an HTTP function.");
  }
  *node = NULL;
  if (config->api_version != NULL && strcmp(config->api_version, "v1.3") != 0)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "IS-04 %s is not supported; dtnmos speaks v1.3.",
                       config->api_version);
  }
  dtnmos_node* result = calloc(1, sizeof(*result));
  if (result == NULL)
  {
    return dtnmos_fail_memory(error);
  }
  result->mutex = dtnmos_mutex_create();
  result->id = config->id;
  result->label = copy_text(config->label);
  result->description = copy_text(config->description);
  result->hostname = copy_text(config->hostname);
  result->api_port = config->api_port;
  char host[256] = "";
  if (config->api_host != NULL && config->api_host[0] != '\0')
  {
    snprintf(host, sizeof(host), "%s", config->api_host);
  }
  else
  {
    char registry[256];
    if (!host_of_url(config->registration_url, registry, sizeof(registry)) ||
        !dtnmos_address_toward(registry, host, sizeof(host)))
    {
      snprintf(host, sizeof(host), "127.0.0.1");
    }
  }
  result->api_host = copy_text(host);
  size_t length = strlen(config->registration_url);
  while (length > 0 && config->registration_url[length - 1] == '/')
  {
    --length;
  }
  dtnmos_buffer base;
  memset(&base, 0, sizeof(base));
  dtnmos_buffer_append(&base, config->registration_url, length);
  DTNMOS_APPEND_LITERAL(&base, "/x-nmos/registration/v1.3/");
  result->registration = base.data;
  result->http = config->http;
  result->http_user = config->http_user;
  result->timeout_ms = config->timeout_ms == 0 ? 5000 : config->timeout_ms;
  result->heartbeat_ms = config->heartbeat_ms == 0 ? 5000 : config->heartbeat_ms;
  result->log = config->log;
  result->log_user = config->log_user;
  if (result->mutex == NULL || result->label == NULL || result->description == NULL ||
      result->hostname == NULL || result->api_host == NULL || base.failed)
  {
    dtnmos_buffer_free(&base);
    result->registration = NULL;
    dtnmos_node_free(result);
    return dtnmos_fail_memory(error);
  }
  dtnmos_version_now(&result->last_version, result->version, sizeof(result->version));
  *node = result;
  return DTNMOS_OK;
}

static void free_sender(node_sender* sender)
{
  free(sender->label);
  free(sender->description);
  free(sender->source_ip);
  dtnmos_flow_clear(&sender->flow);
  dtnmos_connection_clear_sender(sender);
}

static void free_receiver(node_receiver* receiver)
{
  free(receiver->label);
  free(receiver->description);
  dtnmos_connection_clear_receiver(receiver);
}

void dtnmos_node_free(dtnmos_node* node)
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

// Performs a request to the Registration API; path follows its base. Returns the status
// in status, or fails when no answer came.
static dtnmos_result registry_request(dtnmos_node* node, const char* method,
                                      const char* path, const char* body, int* status,
                                      dtnmos_error* error)
{
  dtnmos_buffer url;
  memset(&url, 0, sizeof(url));
  dtnmos_buffer_printf(&url, "%s%s", node->registration, path);
  dtnmos_http_response* response = dtnmos_http_response_create();
  if (url.failed || response == NULL)
  {
    dtnmos_buffer_free(&url);
    dtnmos_http_response_free(response);
    return dtnmos_fail_memory(error);
  }
  dtnmos_http_request request;
  memset(&request, 0, sizeof(request));
  request.size = sizeof(request);
  request.method = method;
  request.url = url.data;
  request.timeout_ms = node->timeout_ms;
  if (body != NULL)
  {
    request.content_type = "application/json";
    request.body = body;
    request.body_length = strlen(body);
  }
  if (error != NULL)
  {
    error->message[0] = '\0';
  }
  dtnmos_result result = node->http(node->http_user, &request, response, error);
  if (result == DTNMOS_OK)
  {
    *status = dtnmos_http_response_status(response);
  }
  else if (error != NULL && error->message[0] == '\0')
  {
    dtnmos_fail(error, result, "%s %s failed.", method, url.data);
  }
  dtnmos_buffer_free(&url);
  dtnmos_http_response_free(response);
  return result;
}

// Registers a resource of type with the JSON data; 200 and 201 are both success.
static dtnmos_result register_resource(dtnmos_node* node, const char* type,
                                       const char* data, dtnmos_error* error)
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
  dtnmos_result result =
      registry_request(node, "POST", "resource", body.data, &status, error);
  dtnmos_buffer_free(&body);
  if (result == DTNMOS_OK && status != 200 && status != 201)
  {
    result = dtnmos_fail(error, DTNMOS_E_HTTP,
                         "The registry answered %d to registering a %s.", status, type);
  }
  if (result == DTNMOS_OK)
  {
    node_log(node, DTNMOS_LOG_DEBUG, "Registered a %s.", type);
  }
  return result;
}

// Writes the members that every resource of IS-04 has.
static void write_common(dtnmos_buffer* b, const dtnmos_id* id, const char* version,
                         const char* label, const char* description)
{
  dtnmos_buffer_printf(b, "\"id\": \"%s\", \"version\": \"%s\", \"label\": ", id->text,
                       version);
  dtnmos_json_write_string(b, label);
  DTNMOS_APPEND_LITERAL(b, ", \"description\": ");
  dtnmos_json_write_string(b, description);
  DTNMOS_APPEND_LITERAL(b, ", \"tags\": {}");
}

void dtnmos_node_write_base_url(const dtnmos_node* node, dtnmos_buffer* b)
{
  const int ipv6 = strchr(node->api_host, ':') != NULL;
  dtnmos_buffer_printf(b, "http://%s%s%s:%u", ipv6 ? "[" : "", node->api_host,
                       ipv6 ? "]" : "", (unsigned)node->api_port);
}

void dtnmos_node_write_self(const dtnmos_node* node, dtnmos_buffer* b)
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

void dtnmos_node_write_device(const dtnmos_node* node, const node_device* device,
                              dtnmos_buffer* b)
{
  DTNMOS_APPEND_LITERAL(b, "{");
  write_common(b, &device->id, device->version, device->label, device->description);
  dtnmos_buffer_printf(b,
                       ", \"type\": \"urn:x-nmos:device:generic\", \"node_id\": \"%s\", "
                       "\"senders\": [",
                       node->id.text);
  int first = 1;
  for (size_t i = 0; i < node->sender_count; ++i)
  {
    if (strcmp(node->senders[i].device_id.text, device->id.text) == 0)
    {
      dtnmos_buffer_printf(b, "%s\"%s\"", first ? "" : ", ", node->senders[i].id.text);
      first = 0;
    }
  }
  DTNMOS_APPEND_LITERAL(b, "], \"receivers\": [");
  first = 1;
  for (size_t i = 0; i < node->receiver_count; ++i)
  {
    if (strcmp(node->receivers[i].device_id.text, device->id.text) == 0)
    {
      dtnmos_buffer_printf(b, "%s\"%s\"", first ? "" : ", ", node->receivers[i].id.text);
      first = 0;
    }
  }
  DTNMOS_APPEND_LITERAL(b, "], \"controls\": [{\"href\": \"");
  dtnmos_node_write_base_url(node, b);
  DTNMOS_APPEND_LITERAL(b,
                        "/x-nmos/connection/v1.1/\", \"type\": "
                        "\"urn:x-nmos:control:sr-ctrl/v1.1\"}]}");
}

static int is_video(const node_sender* sender)
{
  return sender->flow.media == DTNMOS_MEDIA_VIDEO;
}

void dtnmos_node_write_source(const node_sender* sender, dtnmos_buffer* b)
{
  DTNMOS_APPEND_LITERAL(b, "{");
  write_common(b, &sender->source_id, sender->version, sender->label,
               sender->description);
  dtnmos_buffer_printf(b,
                       ", \"format\": \"urn:x-nmos:format:%s\", \"caps\": {}, "
                       "\"device_id\": \"%s\", \"parents\": [], \"clock_name\": \"clk0\"",
                       is_video(sender) ? "video" : "audio", sender->device_id.text);
  if (is_video(sender))
  {
    const dtnmos_video_format* video = &sender->flow.format.video;
    dtnmos_buffer_printf(
        b, ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}}",
        (unsigned)video->rate_numerator,
        (unsigned)(video->rate_denominator == 0 ? 1 : video->rate_denominator));
    return;
  }
  DTNMOS_APPEND_LITERAL(b, ", \"channels\": [");
  for (uint32_t c = 0; c < sender->flow.format.audio.channels; ++c)
  {
    dtnmos_buffer_printf(b, "%s{\"label\": \"Channel %u\"}", c == 0 ? "" : ", ",
                         (unsigned)(c + 1));
  }
  DTNMOS_APPEND_LITERAL(b, "]}");
}

// Returns text, or fallback when it is empty.
static const char* or_default(const dtnmos_string* text, const char* fallback)
{
  return dtnmos_string_length(text) > 0 ? dtnmos_string_get(text) : fallback;
}

void dtnmos_node_write_flow(const node_sender* sender, dtnmos_buffer* b)
{
  DTNMOS_APPEND_LITERAL(b, "{");
  write_common(b, &sender->flow_id, sender->version, sender->label, sender->description);
  dtnmos_buffer_printf(b,
                       ", \"format\": \"urn:x-nmos:format:%s\", \"source_id\": \"%s\", "
                       "\"device_id\": \"%s\", \"parents\": []",
                       is_video(sender) ? "video" : "audio", sender->source_id.text,
                       sender->device_id.text);
  if (is_video(sender))
  {
    const dtnmos_video_format* video = &sender->flow.format.video;
    const unsigned width = (unsigned)video->width;
    const unsigned height = (unsigned)video->height;
    const unsigned depth = (unsigned)(video->depth == 0 ? 10 : video->depth);
    dtnmos_buffer_printf(
        b,
        ", \"grain_rate\": {\"numerator\": %u, \"denominator\": %u}, \"frame_width\": "
        "%u, "
        "\"frame_height\": %u, \"colorspace\": \"%s\", \"interlace_mode\": \"%s\", "
        "\"transfer_characteristic\": \"%s\", \"media_type\": \"video/raw\", "
        "\"components\": "
        "[{\"name\": \"Y\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}, "
        "{\"name\": \"Cb\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}, "
        "{\"name\": \"Cr\", \"width\": %u, \"height\": %u, \"bit_depth\": %u}]}",
        (unsigned)video->rate_numerator,
        (unsigned)(video->rate_denominator == 0 ? 1 : video->rate_denominator), width,
        height, or_default(&video->colorimetry, "BT709"),
        video->interlaced ? "interlaced_tff" : "progressive",
        or_default(&video->tcs, "SDR"), width, height, depth, width / 2, height, depth,
        width / 2, height, depth);
    return;
  }
  const dtnmos_audio_format* audio = &sender->flow.format.audio;
  const int l16 = strcmp(dtnmos_string_get(&audio->encoding), "L16") == 0;
  dtnmos_buffer_printf(
      b,
      ", \"sample_rate\": {\"numerator\": %u}, \"media_type\": \"audio/%s\", "
      "\"bit_depth\": %d}",
      (unsigned)audio->sample_rate, l16 ? "L16" : "L24", l16 ? 16 : 24);
}

void dtnmos_node_write_sender(const dtnmos_node* node, const node_sender* sender,
                              dtnmos_buffer* b)
{
  DTNMOS_APPEND_LITERAL(b, "{");
  write_common(b, &sender->id, sender->version, sender->label, sender->description);
  dtnmos_buffer_printf(
      b,
      ", \"flow_id\": \"%s\", \"transport\": \"urn:x-nmos:transport:%s\", "
      "\"device_id\": \"%s\", \"manifest_href\": \"",
      sender->flow_id.text,
      dtnmos_is_multicast(dtnmos_string_get(&sender->flow.destination_ip)) ? "rtp.mcast"
                                                                           : "rtp.ucast",
      sender->device_id.text);
  dtnmos_node_write_base_url(node, b);
  dtnmos_buffer_printf(b,
                       "/x-nmos/connection/v1.1/single/senders/%s/transportfile\", "
                       "\"interface_bindings\": [], \"subscription\": {\"receiver_id\": ",
                       sender->id.text);
  if (sender->receiver_id.text[0] != '\0')
  {
    dtnmos_buffer_printf(b, "\"%s\"", sender->receiver_id.text);
  }
  else
  {
    DTNMOS_APPEND_LITERAL(b, "null");
  }
  dtnmos_buffer_printf(b, ", \"active\": %s}}", sender->master_enable ? "true" : "false");
}

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
      video ? "video" : "audio", video ? "\"video/raw\"" : "\"audio/L24\", \"audio/L16\"",
      receiver->device_id.text);
  if (receiver->sender_id.text[0] != '\0')
  {
    dtnmos_buffer_printf(b, "\"%s\"", receiver->sender_id.text);
  }
  else
  {
    DTNMOS_APPEND_LITERAL(b, "null");
  }
  dtnmos_buffer_printf(b, ", \"active\": %s}}",
                       receiver->master_enable ? "true" : "false");
}

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

dtnmos_result dtnmos_node_write_transport_file(const node_sender* sender,
                                               dtnmos_string* text, dtnmos_error* error)
{
  dtnmos_session session;
  memset(&session, 0, sizeof(session));
  session.size = sizeof(session);
  session.session_id = sender->session_id;
  session.session_version = sender->session_version;
  if (dtnmos_string_set_text(&session.name, sender->label) != DTNMOS_OK ||
      dtnmos_string_set_text(&session.origin_ip, sender->source_ip[0] != '\0'
                                                     ? sender->source_ip
                                                     : "0.0.0.0") != DTNMOS_OK)
  {
    dtnmos_session_clear(&session);
    return dtnmos_fail_memory(error);
  }
  const dtnmos_result result = dtnmos_sdp_write(&session, &sender->flow, 1, text, error);
  dtnmos_session_clear(&session);
  return result;
}

static void new_version(dtnmos_node* node, char* version, size_t size)
{
  dtnmos_version_now(&node->last_version, version, size);
}

node_device* dtnmos_node_find_device(dtnmos_node* node, const dtnmos_id* id)
{
  for (size_t i = 0; i < node->device_count; ++i)
  {
    if (strcmp(node->devices[i].id.text, id->text) == 0)
    {
      return &node->devices[i];
    }
  }
  return NULL;
}

node_sender* dtnmos_node_find_sender(dtnmos_node* node, const dtnmos_id* id)
{
  for (size_t i = 0; i < node->sender_count; ++i)
  {
    if (strcmp(node->senders[i].id.text, id->text) == 0)
    {
      return &node->senders[i];
    }
  }
  return NULL;
}

node_receiver* dtnmos_node_find_receiver(dtnmos_node* node, const dtnmos_id* id)
{
  for (size_t i = 0; i < node->receiver_count; ++i)
  {
    if (strcmp(node->receivers[i].id.text, id->text) == 0)
    {
      return &node->receivers[i];
    }
  }
  return NULL;
}

static int id_taken(dtnmos_node* node, const dtnmos_id* id)
{
  return strcmp(node->id.text, id->text) == 0 ||
         dtnmos_node_find_device(node, id) != NULL ||
         dtnmos_node_find_sender(node, id) != NULL ||
         dtnmos_node_find_receiver(node, id) != NULL;
}

// Grows an array of count elements of size bytes to hold one more.
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

// The device of a sender or receiver changes with it, as it lists them.
static void touch_device(dtnmos_node* node, const dtnmos_id* id)
{
  node_device* device = dtnmos_node_find_device(node, id);
  if (device != NULL)
  {
    new_version(node, device->version, sizeof(device->version));
    device->registered = 0;
  }
}

dtnmos_result dtnmos_node_add_device(dtnmos_node* node,
                                     const dtnmos_device_config* device,
                                     dtnmos_error* error)
{
  if (node == NULL || device == NULL || device->id.text[0] == '\0')
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "A device needs an ID.");
  }
  dtnmos_node_lock(node);
  dtnmos_result result = DTNMOS_OK;
  if (id_taken(node, &device->id))
  {
    result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                         device->id.text);
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
    added->id = device->id;
    added->label = copy_text(device->label);
    added->description = copy_text(device->description);
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

// Makes the ID of a resource that a sender brings along, from the ID of the sender.
static void derived_id(const dtnmos_id* sender, const char* what, dtnmos_id* id)
{
  if (dtnmos_id_from_name(sender, what, id, NULL) != DTNMOS_OK)
  {
    memset(id, 0, sizeof(*id));
  }
}

dtnmos_result dtnmos_node_add_sender(dtnmos_node* node,
                                     const dtnmos_sender_config* sender,
                                     dtnmos_sender_activate_fn activate, void* user,
                                     dtnmos_error* error)
{
  if (node == NULL || sender == NULL || sender->id.text[0] == '\0' ||
      sender->flow == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "A sender needs an ID and a flow.");
  }
  if (sender->flow->media != DTNMOS_MEDIA_VIDEO &&
      sender->flow->media != DTNMOS_MEDIA_AUDIO)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "A sender of the node sends video or audio, not %s.",
                       dtnmos_media_name(sender->flow->media));
  }
  dtnmos_node_lock(node);
  dtnmos_result result = DTNMOS_OK;
  if (id_taken(node, &sender->id))
  {
    result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                         sender->id.text);
  }
  else if (dtnmos_node_find_device(node, &sender->device_id) == NULL)
  {
    result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has no device %s.",
                         sender->device_id.text);
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
    added->id = sender->id;
    added->device_id = sender->device_id;
    derived_id(&sender->id, "source", &added->source_id);
    derived_id(&sender->id, "flow", &added->flow_id);
    added->label = copy_text(sender->label);
    added->description = copy_text(sender->description);
    added->source_ip = copy_text(sender->source_ip);
    added->activate = activate;
    added->user = user;
    added->master_enable = 1;
    added->session_id = node->last_version / 1000000000u;
    added->session_version = 1;
    if (added->label == NULL || added->description == NULL || added->source_ip == NULL ||
        dtnmos_flow_copy(&added->flow, sender->flow) != DTNMOS_OK ||
        dtnmos_connection_init_sender(added) != DTNMOS_OK)
    {
      free_sender(added);
      result = dtnmos_fail_memory(error);
    }
    else
    {
      new_version(node, added->version, sizeof(added->version));
      ++node->sender_count;
      touch_device(node, &sender->device_id);
    }
  }
  dtnmos_node_unlock(node);
  return result;
}

dtnmos_result dtnmos_node_add_receiver(dtnmos_node* node,
                                       const dtnmos_receiver_config* receiver,
                                       dtnmos_receiver_activate_fn activate, void* user,
                                       dtnmos_error* error)
{
  if (node == NULL || receiver == NULL || receiver->id.text[0] == '\0')
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "A receiver needs an ID.");
  }
  if (receiver->media != DTNMOS_MEDIA_VIDEO && receiver->media != DTNMOS_MEDIA_AUDIO)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "A receiver of the node receives video or audio, not %s.",
                       dtnmos_media_name(receiver->media));
  }
  dtnmos_node_lock(node);
  dtnmos_result result = DTNMOS_OK;
  if (id_taken(node, &receiver->id))
  {
    result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has %s already.",
                         receiver->id.text);
  }
  else if (dtnmos_node_find_device(node, &receiver->device_id) == NULL)
  {
    result = dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT, "The node has no device %s.",
                         receiver->device_id.text);
  }
  else if (!grow((void**)&node->receivers, &node->receiver_capacity, node->receiver_count,
                 sizeof(*node->receivers)))
  {
    result = dtnmos_fail_memory(error);
  }
  else
  {
    node_receiver* added = &node->receivers[node->receiver_count];
    memset(added, 0, sizeof(*added));
    added->id = receiver->id;
    added->device_id = receiver->device_id;
    added->media = receiver->media;
    added->label = copy_text(receiver->label);
    added->description = copy_text(receiver->description);
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
      touch_device(node, &receiver->device_id);
    }
  }
  dtnmos_node_unlock(node);
  return result;
}

// Adds a resource of type to delete from the registry, when it was registered.
static int schedule_removal(dtnmos_node* node, const char* type, const dtnmos_id* id)
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

static void remove_sender_at(dtnmos_node* node, size_t index)
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

static void remove_receiver_at(dtnmos_node* node, size_t index)
{
  node_receiver* receiver = &node->receivers[index];
  if (receiver->was_registered)
  {
    schedule_removal(node, "receivers", &receiver->id);
  }
  touch_device(node, &receiver->device_id);
  free_receiver(receiver);
  memmove(receiver, receiver + 1, (node->receiver_count - index - 1) * sizeof(*receiver));
  --node->receiver_count;
}

dtnmos_result dtnmos_node_remove(dtnmos_node* node, const dtnmos_id* id,
                                 dtnmos_error* error)
{
  if (node == NULL || id == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "dtnmos_node_remove() needs an ID.");
  }
  dtnmos_node_lock(node);
  dtnmos_result result = DTNMOS_OK;
  node_device* device = dtnmos_node_find_device(node, id);
  if (device != NULL)
  {
    for (size_t i = node->sender_count; i > 0; --i)
    {
      if (strcmp(node->senders[i - 1].device_id.text, id->text) == 0)
      {
        remove_sender_at(node, i - 1);
      }
    }
    for (size_t i = node->receiver_count; i > 0; --i)
    {
      if (strcmp(node->receivers[i - 1].device_id.text, id->text) == 0)
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
    remove_sender_at(node, (size_t)(dtnmos_node_find_sender(node, id) - node->senders));
  }
  else if (dtnmos_node_find_receiver(node, id) != NULL)
  {
    remove_receiver_at(node,
                       (size_t)(dtnmos_node_find_receiver(node, id) - node->receivers));
  }
  else
  {
    result = dtnmos_fail(error, DTNMOS_E_NOT_FOUND, "The node has no %s.", id->text);
  }
  dtnmos_node_unlock(node);
  return result;
}

dtnmos_result dtnmos_node_update_sender(dtnmos_node* node, const dtnmos_id* id,
                                        const dtnmos_flow* flow, dtnmos_error* error)
{
  if (node == NULL || id == NULL || flow == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "dtnmos_node_update_sender() needs an ID and a flow.");
  }
  dtnmos_node_lock(node);
  dtnmos_result result = DTNMOS_OK;
  node_sender* sender = dtnmos_node_find_sender(node, id);
  if (sender == NULL)
  {
    result =
        dtnmos_fail(error, DTNMOS_E_NOT_FOUND, "The node has no sender %s.", id->text);
  }
  else if (flow->media != sender->flow.media)
  {
    result = dtnmos_fail(
        error, DTNMOS_E_INVALID_ARGUMENT, "Sender %s sends %s and cannot send %s.",
        id->text, dtnmos_media_name(sender->flow.media), dtnmos_media_name(flow->media));
  }
  else if (dtnmos_flow_copy(&sender->flow, flow) != DTNMOS_OK)
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

// Marks everything unregistered, as when the registry has lost the node.
static void forget_registration(dtnmos_node* node)
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
  int kind;  // 0 node, 1 device, 2 sender with source and flow, 3 receiver
  dtnmos_id id;
  char version[32];
  char* bodies[3];
  const char* types[3];
  size_t count;
} pending;

static void free_pending(pending* p)
{
  for (size_t i = 0; i < p->count; ++i)
  {
    free(p->bodies[i]);
  }
  memset(p, 0, sizeof(*p));
}

// Renders into p the first resource that is not registered, parents before children;
// returns 0 when all are registered.
static int next_pending(dtnmos_node* node, pending* p)
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
      const node_device* device = dtnmos_node_find_device(node, &receiver->device_id);
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

// Marks what p registered, unless it changed meanwhile.
static void mark_registered(dtnmos_node* node, const pending* p)
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
  node_receiver* receiver = p->kind == 3 ? dtnmos_node_find_receiver(node, &p->id) : NULL;
  if (receiver != NULL && strcmp(receiver->version, p->version) == 0)
  {
    receiver->registered = 1;
    receiver->was_registered = 1;
  }
}

int dtnmos_node_registered(const dtnmos_node* node)
{
  if (node == NULL)
  {
    return 0;
  }
  dtnmos_node* mutable_node = (dtnmos_node*)node;
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

dtnmos_result dtnmos_node_poll(dtnmos_node* node, uint32_t* next_ms, dtnmos_error* error)
{
  if (node == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "dtnmos_node_poll() needs a node.");
  }
  dtnmos_result result = DTNMOS_OK;
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
    snprintf(path, sizeof(path), "resource/%s/%s", removal.type, removal.id.text);
    int status = 0;
    result = registry_request(node, "DELETE", path, NULL, &status, error);
    if (result != DTNMOS_OK)
    {
      break;
    }
    dtnmos_node_lock(node);
    if (node->removal_count > 0 &&
        strcmp(node->removals[0].id.text, removal.id.text) == 0)
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
    snprintf(path, sizeof(path), "health/nodes/%s", node->id.text);
    int status = 0;
    result = registry_request(node, "POST", path, NULL, &status, error);
    dtnmos_node_lock(node);
    if (result == DTNMOS_OK && status == 404)
    {
      node_log(node, DTNMOS_LOG_WARNING,
               "The registry has lost node %s; it registers again.", node->id.text);
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
  if (next_ms != NULL)
  {
    dtnmos_node_lock(node);
    const uint64_t now = dtnmos_monotonic_ms();
    uint32_t wait = result != DTNMOS_OK || !node->node_registered ? 1000u : 0u;
    if (result == DTNMOS_OK && node->node_registered)
    {
      wait =
          node->next_heartbeat_ms > now ? (uint32_t)(node->next_heartbeat_ms - now) : 0u;
    }
    dtnmos_node_unlock(node);
    *next_ms = wait;
  }
  return result;
}

// Deletes from the registry everything the node registered, children before parents.
static void unregister_all(dtnmos_node* node)
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
  dtnmos_error ignored;
  dtnmos_node_poll(node, NULL, &ignored);
}

void dtnmos_node_destroy(dtnmos_node* node)
{
  if (node == NULL)
  {
    return;
  }
  dtnmos_server_stop(node);
  unregister_all(node);
  dtnmos_node_free(node);
}

uint16_t dtnmos_node_api_port(const dtnmos_node* node)
{
  return node == NULL ? 0 : node->api_port;
}

dtnmos_result dtnmos_node_api_url(const dtnmos_node* node, dtnmos_string* url)
{
  if (node == NULL || url == NULL)
  {
    return DTNMOS_E_INVALID_ARGUMENT;
  }
  dtnmos_buffer b;
  memset(&b, 0, sizeof(b));
  dtnmos_node_write_base_url(node, &b);
  const dtnmos_result result =
      b.failed ? DTNMOS_E_NO_MEMORY : dtnmos_string_set(url, b.data, b.length);
  dtnmos_buffer_free(&b);
  return result;
}

// Answers with an error of the APIs of NMOS, a JSON object with its code and message.
void dtnmos_node_answer_error(dtnmos_http_response* response, int status,
                              const char* message)
{
  dtnmos_buffer b;
  memset(&b, 0, sizeof(b));
  dtnmos_buffer_printf(&b, "{\"code\": %d, \"error\": ", status);
  dtnmos_json_write_string(&b, message);
  DTNMOS_APPEND_LITERAL(&b, ", \"debug\": null}");
  dtnmos_http_response_set_status(response, status);
  if (!b.failed)
  {
    dtnmos_http_response_set_body(response, "application/json", b.data, b.length);
  }
  dtnmos_buffer_free(&b);
}

// Answers with the JSON of b, or with an error when it failed.
void dtnmos_node_answer_json(dtnmos_http_response* response, dtnmos_buffer* b)
{
  if (b->failed)
  {
    dtnmos_node_answer_error(response, 500, "Out of memory.");
    return;
  }
  dtnmos_http_response_set_status(response, 200);
  dtnmos_http_response_set_body(response, "application/json", b->data, b->length);
}

// Answers the Node API; segments follow /x-nmos/node/v1.3.
static void answer_node_api(dtnmos_node* node, char** segments, size_t count,
                            dtnmos_http_response* response)
{
  dtnmos_buffer b;
  memset(&b, 0, sizeof(b));
  static const char* const collections[] = {"sources", "flows", "devices", "senders",
                                            "receivers"};
  if (count == 0)
  {
    dtnmos_buffer_printf(&b,
                         "[\"self/\", \"sources/\", \"flows/\", \"devices/\", "
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
#define DTNMOS_WRITE_MEMBER(member_id, write)                   \
  if (wanted == NULL || strcmp((member_id)->text, wanted) == 0) \
  {                                                             \
    if (wanted == NULL && written > 0)                          \
    {                                                           \
      DTNMOS_APPEND_LITERAL(&b, ", ");                          \
    }                                                           \
    write;                                                      \
    ++written;                                                  \
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

// Answers a list of the names of the next level of an API.
static void answer_names(dtnmos_http_response* response, const char* names)
{
  dtnmos_buffer b;
  memset(&b, 0, sizeof(b));
  dtnmos_buffer_append(&b, names, strlen(names));
  dtnmos_node_answer_json(response, &b);
  dtnmos_buffer_free(&b);
}

dtnmos_result dtnmos_node_handle(dtnmos_node* node, const dtnmos_http_request* request,
                                 dtnmos_http_response* response, dtnmos_error* error)
{
  if (node == NULL || request == NULL || request->url == NULL ||
      request->method == NULL || response == NULL)
  {
    return dtnmos_fail(error, DTNMOS_E_INVALID_ARGUMENT,
                       "dtnmos_node_handle() needs a node, a request and a response.");
  }
  // The path without its query, split into its segments.
  char path[512];
  snprintf(path, sizeof(path), "%s", request->url);
  path[strcspn(path, "?#")] = '\0';
  char* segments[16];
  size_t count = 0;
  for (char* part = strtok(path, "/"); part != NULL && count < 16;
       part = strtok(NULL, "/"))
  {
    segments[count++] = part;
  }
  const int get =
      strcmp(request->method, "GET") == 0 || strcmp(request->method, "HEAD") == 0;
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
