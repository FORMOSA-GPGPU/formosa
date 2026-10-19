# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.15)
project(LvDevice LANGUAGES NONE)

add_library(lv-device INTERFACE)
add_library(lv::device ALIAS lv-device)
target_include_directories(lv-device INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
