# #*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - The test suites, one program and one ctest each, and the package tests
#
# SPDX-License-Identifier: BSD-3-Clause

# dtnmos_add_test(<suite> <source>...) builds a suite from its sources into a program of
# its own, test_<suite>, and registers it with CTest as dtnmos.<suite>, as CDTAPI does.
# NmosTest.h is the whole framework.
function(dtnmos_add_test suite source)
  set(target "test_${suite}")
  set(framework "${CMAKE_CURRENT_SOURCE_DIR}/NmosTest.h")
  set(sources "${source}" ${ARGN})
  list(TRANSFORM sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
  add_executable(${target} ${sources} "${framework}")
  # The directories on disk are the Visual Studio filters.
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Tests" FILES ${sources}
                                                                     "${framework}")
  set_target_properties(${target} PROPERTIES FOLDER "Tests")
  target_link_libraries(${target} PRIVATE dtnmos::dtnmos)
  # The tests reach the internal headers of the library, and NmosTest.h here.
  target_include_directories(${target} PRIVATE ${DTNMOS_INTERNAL_INCLUDE_DIRS}
                                               "${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  # The tests of discovery and of the WebSocket on libcurl serve on sockets of their own.
  if(WIN32)
    target_link_libraries(${target} PRIVATE ws2_32)
  endif()
  dtnmos_set_warnings(${target})
  add_test(NAME dtnmos.${suite} COMMAND ${target})
endfunction()

dtnmos_add_test(Common Unit/TestCommon.c)
dtnmos_add_test(Connection Loopback/TestConnection.c)
dtnmos_add_test(Controller Loopback/TestController.c)
dtnmos_add_test(Discovery Loopback/TestDiscovery.c)
dtnmos_add_test(Node Loopback/TestNode.c)
dtnmos_add_test(Query Loopback/TestQuery.c)
dtnmos_add_test(Sdp Unit/TestSdp.c)
dtnmos_add_test(Subscription Loopback/TestSubscription.c)

# The suites of the C++ API, one per C++ header, on NmosTest.h like the others.
# - Each suite has a source that includes only its header. That checks that the header
#   compiles alone. As the test source includes the header too, a function that the
#   header defines without inline fails to link.
# - The conversions rely on a warning for a switch that misses a value of its enum.
#   MSVC gives that warning, C4062, only when asked, so it is turned on as an error.
# - Each suite is built a second time without exceptions, as <suite>NoExceptions. MSVC
#   does not build that, as its standard library does not support it.
# - With DTNMOS_CXX_STANDARD 23, each of those is built as C++20 too, as <variant>20,
#   so that the suites run on the Expected of dtnmos_expected.hpp as well as on
#   std::expected.
if(DTNMOS_WITH_CPP)
  set(_dtnmos_cpp_suites
      "Cpp TestCpp OnlyDtNmos" "CppExpected TestCppExpected OnlyDtNmosExpected"
      "CppSdp TestCppSdp OnlyDtNmosSdp"
      "CppHttp TestCppHttp OnlyDtNmosHttp" "CppQuery TestCppQuery OnlyDtNmosQuery"
      "CppNode TestCppNode OnlyDtNmosNode")
  foreach(entry IN LISTS _dtnmos_cpp_suites)
    separate_arguments(entry)
    list(POP_FRONT entry suite)
    list(TRANSFORM entry PREPEND "Cpp/")
    list(TRANSFORM entry APPEND ".cpp")
    set(variants ${suite})
    if(NOT MSVC)
      list(APPEND variants ${suite}NoExceptions)
    endif()
    if(DTNMOS_CXX_STANDARD GREATER 20)
      set(cpp20 ${variants})
      list(TRANSFORM cpp20 APPEND "20")
      list(APPEND variants ${cpp20})
    endif()
    foreach(variant IN LISTS variants)
      dtnmos_add_test(${variant} ${entry})
      target_link_libraries(test_${variant} PRIVATE dtnmos::cpp)
      if(MSVC)
        target_compile_options(test_${variant} PRIVATE /we4062)
      endif()
      if(variant MATCHES "NoExceptions")
        target_compile_options(test_${variant} PRIVATE -fno-exceptions)
      endif()
      if(variant MATCHES "20$")
        set_target_properties(test_${variant} PROPERTIES CXX_STANDARD 20)
      endif()
      # GCC's optimizer loses track of which member of an Expected's union is alive in
      # the assignments of CppExpected, and warns of a delete or a read of the other one,
      # at -O2 and -O3, of std::expected as of the own Expected. ASan finds nothing
      # there, so the two warnings are off for that suite.
      if(suite STREQUAL "CppExpected" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(test_${variant} PRIVATE -Wno-free-nonheap-object
                                                       -Wno-maybe-uninitialized)
      endif()
    endforeach()
  endforeach()
  # The program of two tests. It destroys a search while a node borrows it, or a query
  # while a subscription borrows it, which must call std::terminate(). The program's own
  # terminate handler then ends it with 0.
  add_executable(test_CppBorrowed Cpp/Borrowed.cpp)
  set_target_properties(test_CppBorrowed PROPERTIES FOLDER "Tests")
  target_link_libraries(test_CppBorrowed PRIVATE dtnmos::cpp)
  dtnmos_set_warnings(test_CppBorrowed)
  add_test(NAME dtnmos.CppBorrowedSearch COMMAND test_CppBorrowed search)
  add_test(NAME dtnmos.CppBorrowedQuery COMMAND test_CppBorrowed query)
  # The program of a test that reads the value of a Detail::OwnExpected that holds an
  # error, which must call std::terminate(), and so end the program with 0. It is built
  # without exceptions, which is what it checks, except with MSVC.
  add_executable(test_CppValueOfError Cpp/ValueOfError.cpp)
  set_target_properties(test_CppValueOfError PROPERTIES FOLDER "Tests")
  target_link_libraries(test_CppValueOfError PRIVATE dtnmos::cpp)
  dtnmos_set_warnings(test_CppValueOfError)
  if(NOT MSVC)
    target_compile_options(test_CppValueOfError PRIVATE -fno-exceptions)
  endif()
  add_test(NAME dtnmos.CppValueOfError COMMAND test_CppValueOfError)
  # The program of a test whose sources include dtnmos.hpp under C++20 and under C++23.
  # Each standard must have its own copy of a function that returns a Status, rather
  # than one for both. MSVC's linker refuses such a program instead, by the pragma in
  # dtnmos.hpp, so the test is not built there, nor where the tests are C++20 alone.
  if(NOT MSVC AND DTNMOS_CXX_STANDARD GREATER 20)
    add_library(test_CppMixedStandards20 OBJECT Cpp/MixedStandards20.cpp)
    set_target_properties(test_CppMixedStandards20 PROPERTIES CXX_STANDARD 20 FOLDER
                                                                         "Tests")
    target_link_libraries(test_CppMixedStandards20 PRIVATE dtnmos::dtnmos)
    dtnmos_set_warnings(test_CppMixedStandards20)
    add_executable(test_CppMixedStandards Cpp/MixedStandards.cpp
                                          $<TARGET_OBJECTS:test_CppMixedStandards20>)
    set_target_properties(test_CppMixedStandards PROPERTIES CXX_STANDARD 23 FOLDER "Tests")
    target_link_libraries(test_CppMixedStandards PRIVATE dtnmos::cpp)
    dtnmos_set_warnings(test_CppMixedStandards)
    add_test(NAME dtnmos.CppMixedStandards COMMAND test_CppMixedStandards)
    set_tests_properties(dtnmos.CppMixedStandards PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

# The interop tests run the suites of the AMWA NMOS Testing Tool against a node of
# dtnmos, and use a registry as a client: they take minutes, and need the test bed, whose
# addresses DTNMOS_TESTING_TOOL and DTNMOS_TEST_REGISTRY give at run time. They are built
# only when asked for, and run with "ctest -L interop".
if(DTNMOS_INTEROP_TESTS)
  if(NOT DTNMOS_WITH_CURL OR NOT DTNMOS_WITH_SERVER)
    message(FATAL_ERROR "DTNMOS_INTEROP_TESTS needs DTNMOS_WITH_CURL and DTNMOS_WITH_SERVER")
  endif()
  dtnmos_add_test(Interop Interop/TestInterop.c)
  set_tests_properties(dtnmos.Interop PROPERTIES LABELS interop TIMEOUT 3600)
endif()

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
  # Under Visual Studio the consumer is built without vcpkg's integration, which a user may
  # have installed for every project and which would put the headers of another dtnmos
  # before those of the package under test.
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    list(APPEND _package_vcpkg_options "-DCMAKE_VS_GLOBALS=VcpkgEnabled=false")
  endif()
  # The package test also builds a consumer of the C++ API, which links dtnmos::cpp, when
  # there is a C++ compiler.
  set(_package_cpp_options)
  if(DTNMOS_WITH_CPP)
    list(APPEND _package_cpp_options "-DDTNMOS_PACKAGE_CPP=ON"
         "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    if(DTNMOS_SANITIZER_FLAGS)
      list(APPEND _package_cpp_options "-DCMAKE_CXX_FLAGS=${DTNMOS_SANITIZER_FLAGS}")
    endif()
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
      ${_package_cpp_options}
      --test-command dtnmos-package-test)
  set_tests_properties(dtnmos.package.clean dtnmos.package.install
                       PROPERTIES FIXTURES_SETUP dtnmos-package)
  set_tests_properties(
    dtnmos.package.find-package
    PROPERTIES FIXTURES_REQUIRED dtnmos-package)
endif()
