// SPDX-License-Identifier: BSD-3-Clause
//
// The tests of dtnmos, each a function of no arguments. DTNMOS_TESTS lists them for the
// runner and for CMake, which reads the names from this file.

#ifndef DTNMOS_TESTS_TESTS_H
#define DTNMOS_TESTS_TESTS_H

#define DTNMOS_TESTS                                   \
  DTNMOS_TEST(string_keeps_short_and_long_texts)       \
  DTNMOS_TEST(string_sets_from_its_own_text)           \
  DTNMOS_TEST(string_copies_and_clears)                \
  DTNMOS_TEST(id_is_the_uuid_of_version_5)             \
  DTNMOS_TEST(id_refuses_a_namespace_of_no_uuid)       \
  DTNMOS_TEST(names_results_and_media)                 \
  DTNMOS_TEST(sdp_reads_video_on_two_paths)            \
  DTNMOS_TEST(sdp_reads_audio)                         \
  DTNMOS_TEST(sdp_reads_compressed_video)              \
  DTNMOS_TEST(sdp_reads_ancillary_data)                \
  DTNMOS_TEST(sdp_reads_other_media_as_they_are)       \
  DTNMOS_TEST(sdp_takes_defaults_of_the_session)       \
  DTNMOS_TEST(sdp_names_the_line_of_an_error)          \
  DTNMOS_TEST(sdp_writes_what_it_reads_back)           \
  DTNMOS_TEST(sdp_writes_an_audio_sender)              \
  DTNMOS_TEST(sdp_refuses_to_write_an_incomplete_flow) \
  DTNMOS_TEST(flow_copy_owns_its_strings)

#define DTNMOS_TEST(name) void name(void);
DTNMOS_TESTS
#undef DTNMOS_TEST

#endif  // DTNMOS_TESTS_TESTS_H
