# #*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - A consumer of an installed dtnmos
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.25)
project(dtnmos-package-test LANGUAGES C)

find_package(dtnmos CONFIG REQUIRED)

add_executable(dtnmos-package-test main.c)
target_link_libraries(dtnmos-package-test PRIVATE dtnmos::dtnmos)
