# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Works with 3.14 and tested through 3.28
cmake_minimum_required(VERSION 3.14...3.28)

# Project name and a few useful settings. Other commands can pick up the results
project(
  simtix
  VERSION 0.1
  DESCRIPTION "SIMT + ticks, instruction set simulator for FORMOSA GPGPU"
  LANGUAGES CXX)

add_subdirectory(src)

option(SIMTIX_ENABLE_TESTING "Build tests for simtix" OFF)
if(SIMTIX_ENABLE_TESTING)
  message(STATUS "Enabling testing for simtix")
  add_subdirectory(tests)
endif()
