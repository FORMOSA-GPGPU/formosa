# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.24...3.28)

project(
  lv
  VERSION 0.1
  DESCRIPTION "Lunaverse"
  LANGUAGES CXX C)

include(cmake/FindLuajit.cmake)

option(LV_ENABLE_TESTING "Build tests for lv" OFF)
option(LV_ENABLE_DOCGEN "Build docgen target for lv" ON)

add_subdirectory(src/liblv)
add_subdirectory(src/lv)

if(LV_ENABLE_TESTING)
  message(STATUS "Enabling testing for lv")
  include(CTest)
  add_subdirectory(tests)
endif()
