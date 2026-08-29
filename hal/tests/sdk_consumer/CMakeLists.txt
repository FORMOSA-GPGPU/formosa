# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.14...3.28)
project(formosa_sdk_consumer LANGUAGES CXX)

find_package(Formosa CONFIG REQUIRED)

add_executable(formosa_sdk_consumer consumer.cc)
target_link_libraries(formosa_sdk_consumer PRIVATE Formosa::formosa-hal.lv)
