# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.14...3.28)
project(formosa_sdk_consumer LANGUAGES C CXX)

find_package(Formosa CONFIG REQUIRED)

add_executable(formosa_sdk_consumer consumer.cc)
target_link_libraries(formosa_sdk_consumer PRIVATE Formosa::formosa-hal.lv)

# Exercise the same C API without a C++ compiler supplying its own runtime.
configure_file(consumer.cc consumer.c COPYONLY)
add_executable(formosa_sdk_c_consumer "${CMAKE_CURRENT_BINARY_DIR}/consumer.c")
target_link_libraries(formosa_sdk_c_consumer PRIVATE Formosa::formosa-hal.lv)
