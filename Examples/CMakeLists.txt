# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# dtnmos - Example programs, and their smoke tests
#
# SPDX-License-Identifier: BSD-3-Clause

set(_dtnmos_examples DtNmosConnect DtNmosListSenders DtNmosReadSdp DtNmosRegisterNode)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Programs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

foreach(program IN LISTS _dtnmos_examples)
  set(sources "${CMAKE_CURRENT_SOURCE_DIR}/${program}.c"
              "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.c"
              "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.h")
  add_executable(${program} ${sources})
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Examples" FILES ${sources})
  set_target_properties(${program} PROPERTIES FOLDER "Examples")
  target_include_directories(${program} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${program} PRIVATE dtnmos::dtnmos)
  if(MSVC)
    target_compile_definitions(${program} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  dtnmos_set_warnings(${program})
endforeach()

# DtNmosRegisterNodeCpp is DtNmosRegisterNode written with the C++ API. It uses the same
# helpers.
if(DTNMOS_WITH_CPP)
  set(sources "${CMAKE_CURRENT_SOURCE_DIR}/DtNmosRegisterNode.cpp"
              "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.c"
              "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.h")
  add_executable(DtNmosRegisterNodeCpp ${sources})
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Examples" FILES ${sources})
  set_target_properties(DtNmosRegisterNodeCpp PROPERTIES FOLDER "Examples")
  target_include_directories(DtNmosRegisterNodeCpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(DtNmosRegisterNodeCpp PRIVATE dtnmos::cpp)
  if(MSVC)
    target_compile_definitions(DtNmosRegisterNodeCpp PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(DtNmosRegisterNodeCpp PRIVATE /we4062)
  endif()
  dtnmos_set_warnings(DtNmosRegisterNodeCpp)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Smoke tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# What a program does without a network: an SDP read and written back, and the failures
# a program reports. A test passes when the output matches; the regular expression spans
# lines where a test checks more than one.
#

if(NOT DTNMOS_BUILD_TESTS)
  return()
endif()

set(_sdp "${CMAKE_CURRENT_SOURCE_DIR}/Data/Camera.sdp")

add_test(NAME dtnmos.example.read-sdp COMMAND DtNmosReadSdp --file "${_sdp}")
set_tests_properties(
  dtnmos.example.read-sdp
  PROPERTIES
    PASS_REGULAR_EXPRESSION
    "session \"Camera 1\" from 192.168.1.10\r?\nflow 0: video 239.10.1.1:5004 from 192.168.1.10, PT 96, 1920x1080i 25/1 YCbCr-4:2:2 10 bit\r?\nflow 1: video 239.20.1.1:5004 from 192.168.2.10, PT 96, 1920x1080i 25/1 YCbCr-4:2:2 10 bit \\(leg 1\\)\r?\nflow 2: audio 239.10.1.2:5006 from 192.168.1.10, PT 97, L24 48000 Hz 2 ch, 1000 us"
)

add_test(NAME dtnmos.example.write-sdp COMMAND DtNmosReadSdp --file "${_sdp}" --write)
set_tests_properties(
  dtnmos.example.write-sdp
  PROPERTIES PASS_REGULAR_EXPRESSION
             "a=group:DUP primary secondary.*m=video 5004 RTP/AVP 96.*m=audio 5006 RTP/AVP 97")

# A registry that does not answer fails the request the program makes first.
add_test(NAME dtnmos.example.no-registry COMMAND DtNmosListSenders --registry
                                                 http://127.0.0.1:9)
set_tests_properties(
  dtnmos.example.no-registry PROPERTIES PASS_REGULAR_EXPRESSION
                                        "DtNmosQuery_Senders: DTNMOS_E_(HTTP|STATE): ")

add_test(NAME dtnmos.example.connect-needs-a-receiver COMMAND DtNmosConnect --sender x)
set_tests_properties(
  dtnmos.example.connect-needs-a-receiver
  PROPERTIES PASS_REGULAR_EXPRESSION "Give --receiver, and --sender or --disconnect")
