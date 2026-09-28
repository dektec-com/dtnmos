// SPDX-License-Identifier: BSD-3-Clause
//
// The Connection API of the node (AMWA IS-05 v1.1), as far as milestone 4 of plan 0016
// needs it: the names of its levels and the transport file of each sender, to which the
// manifest_href of the sender points.

#include <stdlib.h>
#include <string.h>

#include "node-internal.h"

dtnmos_result dtnmos_connection_init_sender(node_sender* sender)
{
  sender->connection = NULL;
  return DTNMOS_OK;
}

void dtnmos_connection_clear_sender(node_sender* sender)
{
  sender->connection = NULL;
}

dtnmos_result dtnmos_connection_init_receiver(node_receiver* receiver)
{
  receiver->connection = NULL;
  return DTNMOS_OK;
}

void dtnmos_connection_clear_receiver(node_receiver* receiver)
{
  receiver->connection = NULL;
}

// Answers the transport file of the sender whose ID is id.
static void answer_transport_file(dtnmos_node* node, const char* id,
                                  dtnmos_http_response* response)
{
  dtnmos_id wanted;
  memset(&wanted, 0, sizeof(wanted));
  if (strlen(id) >= sizeof(wanted.text))
  {
    dtnmos_node_answer_error(response, 404, "The node has no sender of that ID.");
    return;
  }
  strcpy(wanted.text, id);
  const node_sender* sender = dtnmos_node_find_sender(node, &wanted);
  if (sender == NULL)
  {
    dtnmos_node_answer_error(response, 404, "The node has no sender of that ID.");
    return;
  }
  dtnmos_string text = {0};
  dtnmos_error error;
  if (dtnmos_node_write_transport_file(sender, &text, &error) != DTNMOS_OK)
  {
    dtnmos_node_answer_error(response, 500, error.message);
    return;
  }
  dtnmos_http_response_set_status(response, 200);
  dtnmos_http_response_set_body(response, "application/sdp", dtnmos_string_get(&text),
                                dtnmos_string_length(&text));
  dtnmos_string_clear(&text);
}

dtnmos_result dtnmos_connection_handle(dtnmos_node* node,
                                       const dtnmos_http_request* request,
                                       char** segments, size_t count,
                                       dtnmos_http_response* response,
                                       dtnmos_error* error)
{
  (void)error;
  if (strcmp(request->method, "GET") != 0 && strcmp(request->method, "HEAD") != 0)
  {
    dtnmos_node_answer_error(response, 405, "The Connection API answers GET here.");
    return DTNMOS_OK;
  }
  dtnmos_node_lock(node);
  if (count == 4 && strcmp(segments[0], "single") == 0 &&
      strcmp(segments[1], "senders") == 0 && strcmp(segments[3], "transportfile") == 0)
  {
    answer_transport_file(node, segments[2], response);
  }
  else
  {
    dtnmos_node_answer_error(response, 404, "The Connection API has no such resource.");
  }
  dtnmos_node_unlock(node);
  return DTNMOS_OK;
}
