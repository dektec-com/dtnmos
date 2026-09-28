# SPDX-License-Identifier: BSD-3-Clause
#
# A consumer of an installed dtnmos: it finds the package and parses an SDP with it.

cmake_minimum_required(VERSION 3.25)
project(dtnmos-package-test LANGUAGES C)

find_package(dtnmos CONFIG REQUIRED)

add_executable(dtnmos-package-test main.c)
target_link_libraries(dtnmos-package-test PRIVATE dtnmos::dtnmos)
