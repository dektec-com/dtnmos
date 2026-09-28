# SPDX-License-Identifier: BSD-3-Clause
#
# The dtnmos tests, one ctest per test function of tests.h, and the tests that install dtnmos
# and use it through find_package.

add_executable(dtnmos_tests main.c common-test.c sdp-test.c)
target_link_libraries(dtnmos_tests PRIVATE dtnmos::dtnmos)
if(MSVC)
  target_compile_definitions(dtnmos_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
dtnmos_set_warnings(dtnmos_tests)

# The names of the tests are read from their list, so that a new one needs no entry here.
# The list continues its lines with a backslash, so the file is read whole.
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
  add_test(NAME dtnmos.package.install
           COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}" --prefix
                   "${_package_dir}/prefix" --config "$<CONFIG>")
  add_test(
    NAME dtnmos.package.find-package
    COMMAND
      "${CMAKE_CTEST_COMMAND}" --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/package"
      "${_package_dir}/build" --build-generator "${CMAKE_GENERATOR}" --build-options
      "-DCMAKE_BUILD_TYPE=$<CONFIG>" "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_PREFIX_PATH=${_package_dir}/prefix" "-DCMAKE_C_FLAGS=${DTNMOS_SANITIZER_FLAGS}"
      "-DCMAKE_EXE_LINKER_FLAGS=${DTNMOS_SANITIZER_FLAGS}" --test-command dtnmos-package-test)
  set_tests_properties(dtnmos.package.install PROPERTIES FIXTURES_SETUP dtnmos-package)
  set_tests_properties(dtnmos.package.find-package PROPERTIES FIXTURES_REQUIRED dtnmos-package)
endif()
