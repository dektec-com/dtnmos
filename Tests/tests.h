// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# tests.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The tests of dtnmos, each a function of no arguments
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#define DTNMOS_TESTS                                                                     \
    DTNMOS_TEST(string_keeps_short_and_long_texts)                                       \
    DTNMOS_TEST(string_sets_from_its_own_text)                                           \
    DTNMOS_TEST(string_copies_and_clears)                                                \
    DTNMOS_TEST(id_is_the_uuid_of_version_5)                                             \
    DTNMOS_TEST(id_refuses_a_namespace_of_no_uuid)                                       \
    DTNMOS_TEST(names_results_and_media)                                                 \
    DTNMOS_TEST(keeps_the_last_error_of_the_thread)                                      \
    DTNMOS_TEST(sdp_reads_video_on_two_paths)                                            \
    DTNMOS_TEST(sdp_reads_audio)                                                         \
    DTNMOS_TEST(sdp_reads_compressed_video)                                              \
    DTNMOS_TEST(sdp_reads_ancillary_data)                                                \
    DTNMOS_TEST(sdp_reads_other_media_as_they_are)                                       \
    DTNMOS_TEST(sdp_takes_defaults_of_the_session)                                       \
    DTNMOS_TEST(sdp_names_the_line_of_an_error)                                          \
    DTNMOS_TEST(sdp_writes_what_it_reads_back)                                           \
    DTNMOS_TEST(sdp_writes_an_audio_sender)                                              \
    DTNMOS_TEST(sdp_refuses_to_write_an_incomplete_flow)                                 \
    DTNMOS_TEST(flow_copy_owns_its_strings)                                              \
    DTNMOS_TEST(json_reads_values_and_escapes)                                           \
    DTNMOS_TEST(json_refuses_what_is_malformed)                                          \
    DTNMOS_TEST(json_writes_escaped_strings)                                             \
    DTNMOS_TEST(http_response_owns_what_it_holds)                                        \
    DTNMOS_TEST(query_lists_the_senders_of_every_page)                                   \
    DTNMOS_TEST(query_finds_a_sender_and_its_sdp)                                        \
    DTNMOS_TEST(query_names_what_went_wrong)                                             \
    DTNMOS_TEST(query_refuses_an_incomplete_config)                                      \
    DTNMOS_TEST(query_lists_and_finds_receivers)                                         \
    DTNMOS_TEST(controller_connects_a_receiver)                                          \
    DTNMOS_TEST(controller_disconnects_a_receiver)                                       \
    DTNMOS_TEST(controller_names_what_went_wrong)                                        \
    DTNMOS_TEST(controller_moves_a_sender)                                               \
    DTNMOS_TEST(json_writes_what_it_reads_back)                                          \
    DTNMOS_TEST(subscription_reports_what_changes)                                       \
    DTNMOS_TEST(subscription_names_what_went_wrong)                                      \
    DTNMOS_TEST(websocket_on_curl_reads_messages)                                        \
    DTNMOS_TEST(node_registers_parents_before_children)                                  \
    DTNMOS_TEST(node_registers_again_when_the_registry_lost_it)                          \
    DTNMOS_TEST(node_moves_to_the_next_registry)                                         \
    DTNMOS_TEST(node_deletes_what_is_removed_and_what_it_had)                            \
    DTNMOS_TEST(node_answers_its_node_api_and_transport_files)                           \
    DTNMOS_TEST(node_serves_itself_over_http)                                            \
    DTNMOS_TEST(connection_answers_its_parameters)                                       \
    DTNMOS_TEST(connection_connects_a_receiver)                                          \
    DTNMOS_TEST(connection_moves_a_sender)                                               \
    DTNMOS_TEST(connection_refuses_bad_patches)                                          \
    DTNMOS_TEST(dns_writes_a_query)                                                      \
    DTNMOS_TEST(dns_reads_records_and_compression)                                       \
    DTNMOS_TEST(dns_escapes_dots_within_labels)                                          \
    DTNMOS_TEST(dns_refuses_malformed_messages)                                          \
    DTNMOS_TEST(discovery_finds_registries_by_priority)                                  \
    DTNMOS_TEST(discovery_asks_again_for_what_is_missing)                                \
    DTNMOS_TEST(discovery_finds_nothing_in_silence)                                      \
    DTNMOS_TEST(dns_reads_resolv_conf)                                                   \
    DTNMOS_TEST(discovery_asks_a_dns_server_too)                                         \
    DTNMOS_TEST(discovery_takes_only_the_dns_server)

#define DTNMOS_TEST(name) void name(void);
DTNMOS_TESTS
#undef DTNMOS_TEST
