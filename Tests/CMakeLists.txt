# #*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - The test suites, one program and one ctest each, and the package tests
#
# SPDX-License-Identifier: BSD-3-Clause

# dtnmos_add_test(<suite> <source>...) builds the suite of a source file, and of the
# sources after it, into a program of its own, test_<suite>, and registers it with CTest
# as dtnmos.<suite>, as CDTAPI does. NmosTest.h is the whole framework.
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

# The suites of the C++ API, one per C++ header, on NmosTest.h as the others. Each C++
# header has a source of its own that includes only it, so that it compiles alone, and so
# that a function it defines without inline fails to link. MSVC warns of a switch that
# misses a value of its enum, which the conversions rely on, only when asked. Each suite
# is built a second time without exceptions, as <suite>NoExceptions, except by MSVC,
# whose standard library does not support that.
if(DTNMOS_WITH_CPP)
  set(_dtnmos_cpp_suites "Cpp TestCpp OnlyDtNmos" "CppSdp TestCppSdp OnlyDtNmosSdp")
  foreach(entry IN LISTS _dtnmos_cpp_suites)
    separate_arguments(entry)
    list(POP_FRONT entry suite)
    list(TRANSFORM entry PREPEND "Cpp/")
    list(TRANSFORM entry APPEND ".cpp")
    set(variants ${suite})
    if(NOT MSVC)
      list(APPEND variants ${suite}NoExceptions)
    endif()
    foreach(variant IN LISTS variants)
      dtnmos_add_test(${variant} ${entry})
      target_link_libraries(test_${variant} PRIVATE dtnmos::cpp)
      if(MSVC)
        target_compile_options(test_${variant} PRIVATE /we4062)
      endif()
    endforeach()
    if(NOT MSVC)
      target_compile_options(test_${suite}NoExceptions PRIVATE -fno-exceptions)
    endif()
  endforeach()
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
  # A consumer of the C++ API as well, which links dtnmos::cpp, when there is a compiler
  # for it.
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
