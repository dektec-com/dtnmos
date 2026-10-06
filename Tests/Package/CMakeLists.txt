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

# The C++ consumer asks for no C++ standard of its own: dtnmos::cpp brings C++23.
if(DTNMOS_PACKAGE_CPP)
  enable_language(CXX)
  add_executable(dtnmos-package-test-cpp main.cpp)
  target_link_libraries(dtnmos-package-test-cpp PRIVATE dtnmos::cpp)
endif()
