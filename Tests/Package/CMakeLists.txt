# #*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - A consumer of an installed dtnmos
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.25)
project(dtnmos-package-test LANGUAGES C)

option(DTNMOS_PACKAGE_CPP "Build a consumer of the C++ API as well" OFF)

find_package(dtnmos CONFIG REQUIRED)

add_executable(dtnmos-package-test main.c)
target_link_libraries(dtnmos-package-test PRIVATE dtnmos::dtnmos)

# The first C++ consumer asks for no C++ standard itself: linking dtnmos::cpp gives it
# C++20 at least. The second asks for C++23, where the headers take std::expected.
if(DTNMOS_PACKAGE_CPP)
  enable_language(CXX)
  add_executable(dtnmos-package-test-cpp main.cpp)
  target_link_libraries(dtnmos-package-test-cpp PRIVATE dtnmos::cpp)
  add_executable(dtnmos-package-test-cpp23 main.cpp)
  set_target_properties(dtnmos-package-test-cpp23 PROPERTIES CXX_STANDARD 23
                                                             CXX_STANDARD_REQUIRED ON)
  target_link_libraries(dtnmos-package-test-cpp23 PRIVATE dtnmos::cpp)
endif()
