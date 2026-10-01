# #*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - The dtnmos tests, one ctest per test of tests.h, and the package tests
#
# SPDX-License-Identifier: BSD-3-Clause

add_executable(
  dtnmos_tests
  main.c
  Loopback/TestConnection.c
  Loopback/TestController.c
  Loopback/TestDiscovery.c
  Loopback/TestNode.c
  Loopback/TestQuery.c
  Loopback/TestSubscription.c
  Unit/TestCommon.c
  Unit/TestSdp.c
  check.h
  tests.h)
# The directories on disk are the Visual Studio filters.
get_target_property(_dtnmos_test_sources dtnmos_tests SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Tests"
             FILES ${_dtnmos_test_sources})
target_link_libraries(dtnmos_tests PRIVATE dtnmos::dtnmos)
# The tests of the JSON parser reach the internal headers of the library, and the tests
# in the directories below this one include check.h and tests.h from it.
target_include_directories(dtnmos_tests PRIVATE ${DTNMOS_INTERNAL_INCLUDE_DIRS}
                                                "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_definitions(dtnmos_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
# The test of the WebSocket on libcurl serves one on a socket of its own.
if(WIN32)
  target_link_libraries(dtnmos_tests PRIVATE ws2_32)
endif()
dtnmos_set_warnings(dtnmos_tests)

# The names of the tests are read from their list, so that a new one needs no entry here.
# The list continues its lines with a backslash, so the file is read whole, and a change
# of it configures again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests.h)
file(READ tests.h _dtnmos_tests_header)
string(REGEX MATCHALL "DTNMOS_TEST\\([a-z0-9_]+\\)" _dtnmos_test_calls
             "${_dtnmos_tests_header}")
foreach(_call IN LISTS _dtnmos_test_calls)
  string(REGEX REPLACE "DTNMOS_TEST\\(([a-z0-9_]+)\\)" "\\1" _name "${_call}")
  # The macro that declares each test has a parameter called name.
  if(NOT _name STREQUAL "name")
    add_test(NAME dtnmos.${_name} COMMAND dtnmos_tests ${_name})
  endif()
endforeach()

# The package tests install dtnmos into the build tree and build a consumer of it.
if(DTNMOS_INSTALL)
  set(_package_dir "${PROJECT_BINARY_DIR}/package-test")
  # A dtnmos with libcurl or civetweb finds them where the build found them.
  set(_package_prefix_path "${_package_dir}/prefix")
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    string(
      APPEND _package_prefix_path
      "$<SEMICOLON>${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  endif()
  # A consumer on vcpkg finds the dependencies of the package through the toolchain of
  # vcpkg, which knows the names a triplet gives its libraries, such as zs.lib for the
  # static zlib of a Windows triplet, where the find modules of CMake do not.
  set(_package_vcpkg_options)
  if(CMAKE_TOOLCHAIN_FILE
     AND DEFINED VCPKG_INSTALLED_DIR
     AND DEFINED VCPKG_TARGET_TRIPLET)
    list(
      APPEND
      _package_vcpkg_options
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
      "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
      "-DVCPKG_MANIFEST_MODE=OFF")
  endif()
  # The consumer is configured afresh on every run, as a toolchain takes effect only in
  # the first configuration of a build directory. Under a generator of several
  # configurations it has only the one installed, as the package holds no other, and is
  # built in it.
  add_test(NAME dtnmos.package.clean COMMAND "${CMAKE_COMMAND}" -E rm -rf
                                            "${_package_dir}/build")
  add_test(NAME dtnmos.package.install
           COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}" --prefix
                   "${_package_dir}/prefix" --config "$<CONFIG>")
  add_test(
    NAME dtnmos.package.find-package
    COMMAND
      "${CMAKE_CTEST_COMMAND}" -C "$<CONFIG>" --build-and-test
      "${CMAKE_CURRENT_SOURCE_DIR}/Package"
      "${_package_dir}/build" --build-generator "${CMAKE_GENERATOR}" --build-options
      "-DCMAKE_BUILD_TYPE=$<CONFIG>" "-DCMAKE_CONFIGURATION_TYPES=$<CONFIG>"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_PREFIX_PATH=${_package_prefix_path}"
      "-DCMAKE_C_FLAGS=${DTNMOS_SANITIZER_FLAGS}"
      "-DCMAKE_EXE_LINKER_FLAGS=${DTNMOS_SANITIZER_FLAGS}" ${_package_vcpkg_options}
      --test-command dtnmos-package-test)
  set_tests_properties(dtnmos.package.clean dtnmos.package.install
                       PROPERTIES FIXTURES_SETUP dtnmos-package)
  set_tests_properties(
    dtnmos.package.find-package
    PROPERTIES FIXTURES_REQUIRED dtnmos-package)
endif()
