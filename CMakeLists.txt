# #*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - dtnmos, a C library for the SDP of SMPTE ST 2110 flows and for NMOS
#
# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.25)

project(
  dtnmos
  VERSION 0.2.0
  DESCRIPTION "SDP of SMPTE ST 2110 flows, and NMOS"
  HOMEPAGE_URL "https://github.com/dektec-com/dtnmos"
  LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The version of dtnmos_version.h, which DtNmos_Version() reports, is the project's, so
# that the version is stated in one place only.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Include/dtnmos_version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/Generated/dtnmos_version.h" @ONLY)

include(GNUInstallDirs)

option(DTNMOS_BUILD_TESTS "Build the dtnmos tests" ${PROJECT_IS_TOP_LEVEL})
option(DTNMOS_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
option(
  DTNMOS_INSTALL
  "Install the dtnmos library, headers, CMake package and pkg-config file"
  ${PROJECT_IS_TOP_LEVEL})
option(DTNMOS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(DTNMOS_WITH_CURL "Build the HTTP transport on libcurl, dtnmos_curl_http()" OFF)
option(
  DTNMOS_WITH_SERVER
  "Build the server of the node on civetweb, dtnmos_node_serve()"
  OFF)
# The flags of the sanitizers the library is built with, which the package test builds its
# consumer with as well; a project that builds dtnmos with sanitizers sets them.
set(DTNMOS_SANITIZER_FLAGS
    ""
    CACHE STRING "Sanitizer flags of the consumer the package test builds")

# dtnmos_set_warnings(<target>) enables the warning level of dtnmos on <target>.
function(dtnmos_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    if(DTNMOS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(
      ${target}
      PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(DTNMOS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_library(
  dtnmos
  Source/Api/DtNmos.c
  Source/Core/NmosJson.c
  Source/Core/NmosSha1.c
  Source/Core/NmosText.c
  Source/Discovery/DtNmosDiscovery.c
  Source/Discovery/NmosDns.c
  Source/Http/DtNmosHttp.c
  Source/Http/NmosCurl.c
  Source/Http/NmosCurlWebSocket.c
  Source/Http/NmosServer.c
  Source/Node/DtNmosNode.c
  Source/Node/NmosConnection.c
  Source/OAL/NmosOs.c
  Source/Query/DtNmosController.c
  Source/Query/DtNmosQuery.c
  Source/Query/DtNmosSubscription.c
  Source/Sdp/NmosFlow.c
  Source/Sdp/DtNmosSdpParse.c
  Source/Sdp/DtNmosSdpWrite.c)
add_library(dtnmos::dtnmos ALIAS dtnmos)
# The headers, public and internal, are added to the target as well, so that an IDE shows
# them, and source_group turns the directories on disk into the Visual Studio filters. The
# glob only decides what is shown; what is compiled stays the list above.
file(GLOB_RECURSE _dtnmos_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.in"
     "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
target_sources(dtnmos PRIVATE ${_dtnmos_headers})
get_target_property(_dtnmos_sources dtnmos SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_dtnmos_sources})
# The version header is made in the build directory, outside the tree.
target_sources(dtnmos PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated/dtnmos_version.h")
source_group("Generated" FILES "${CMAKE_CURRENT_BINARY_DIR}/Generated/dtnmos_version.h")
target_include_directories(
  dtnmos PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>"
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Generated>"
                "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
# The internal headers are named without their directory, from any source.
set(DTNMOS_INTERNAL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Discovery"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Node"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/OAL"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Query"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sdp")
target_include_directories(dtnmos PRIVATE ${DTNMOS_INTERNAL_INCLUDE_DIRS})
target_compile_features(dtnmos PRIVATE c_std_11)
target_compile_definitions(dtnmos PRIVATE DTNMOS_BUILDING)
if(BUILD_SHARED_LIBS)
  target_compile_definitions(dtnmos PUBLIC DTNMOS_SHARED)
  set_target_properties(dtnmos PROPERTIES C_VISIBILITY_PRESET hidden)
endif()
set_target_properties(dtnmos PROPERTIES VERSION ${PROJECT_VERSION}
                                        SOVERSION ${PROJECT_VERSION_MAJOR})
if(DTNMOS_WITH_CURL)
  find_package(CURL REQUIRED)
  target_link_libraries(dtnmos PRIVATE CURL::libcurl)
  target_compile_definitions(dtnmos PRIVATE DTNMOS_WITH_CURL)
endif()
if(DTNMOS_WITH_SERVER)
  find_package(civetweb CONFIG REQUIRED)
  target_link_libraries(dtnmos PRIVATE civetweb::civetweb)
  target_compile_definitions(dtnmos PRIVATE DTNMOS_WITH_SERVER)
endif()
# The node finds the address of this host with sockets, and runs a thread when it serves.
if(WIN32)
  target_link_libraries(dtnmos PRIVATE ws2_32 iphlpapi)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(dtnmos PRIVATE Threads::Threads)
endif()
if(MSVC)
  # snprintf and the string functions of the C library are what dtnmos means to use.
  target_compile_definitions(dtnmos PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
dtnmos_set_warnings(dtnmos)

if(DTNMOS_INSTALL)
  include(CMakePackageConfigHelpers)
  set(_dtnmos_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/dtnmos")
  install(
    TARGETS dtnmos
    EXPORT dtnmos-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(
    DIRECTORY Include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "*.h")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Generated/dtnmos_version.h"
          DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(
    EXPORT dtnmos-targets
    NAMESPACE dtnmos::
    DESTINATION "${_dtnmos_package_dir}")
  configure_package_config_file(
    dtnmos-config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/dtnmos-config.cmake"
    INSTALL_DESTINATION "${_dtnmos_package_dir}")
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/dtnmos-config-version.cmake"
    COMPATIBILITY SameMinorVersion)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dtnmos-config.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/dtnmos-config-version.cmake"
          DESTINATION "${_dtnmos_package_dir}")

  # The pkg-config file, which projects that do not use CMake read, such as FFmpeg.
  if(BUILD_SHARED_LIBS)
    set(DTNMOS_PC_CFLAGS "-DDTNMOS_SHARED")
  else()
    set(DTNMOS_PC_CFLAGS "")
  endif()
  set(_dtnmos_pc_requires "")
  if(DTNMOS_WITH_CURL)
    list(APPEND _dtnmos_pc_requires libcurl)
  endif()
  if(DTNMOS_WITH_SERVER)
    list(APPEND _dtnmos_pc_requires civetweb)
  endif()
  if(_dtnmos_pc_requires)
    list(JOIN _dtnmos_pc_requires ", " _dtnmos_pc_requires)
    set(DTNMOS_PC_REQUIRES "Requires.private: ${_dtnmos_pc_requires}")
  else()
    set(DTNMOS_PC_REQUIRES "")
  endif()
  if(WIN32)
    set(DTNMOS_PC_LIBS_PRIVATE "-lws2_32 -liphlpapi")
  else()
    set(DTNMOS_PC_LIBS_PRIVATE "-lpthread")
  endif()
  # The way from the directory of the file up to the prefix, "../.." for lib/pkgconfig;
  # the prefix of the configuration is not where every install goes.
  file(RELATIVE_PATH DTNMOS_PC_PREFIX_FROM_PCDIR "/prefix/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
       "/prefix")
  string(REGEX REPLACE "/$" "" DTNMOS_PC_PREFIX_FROM_PCDIR "${DTNMOS_PC_PREFIX_FROM_PCDIR}")
  configure_file(dtnmos.pc.in "${CMAKE_CURRENT_BINARY_DIR}/dtnmos.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dtnmos.pc"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

if(DTNMOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()

if(DTNMOS_BUILD_EXAMPLES)
  add_subdirectory(Examples)
endif()
