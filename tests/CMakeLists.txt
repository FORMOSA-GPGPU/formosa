# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

find_program(CLANG NAMES "clang")
if(CLANG STREQUAL "CLANG-NOTFOUND")
  message(FATAL_ERROR "clang executable not found! Use `nix develop` to enter the dev shell with FORMOSA LLVM.")
else()
  message(STATUS "Found clang: ${CLANG}")
  if(NOT CLANG MATCHES ".*formosa-llvm.*")
    message(WARNING "clang is not from FORMOSA LLVM. Some of the compilation options may not be supported.")
  endif()
endif()

file(GLOB children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
set(FORMOSA_TEST_DIRS "")
set(FORMOSA_TEST_SUPPORT_DIRS "assets;common;fixtures;lua;support")

foreach(child ${children})
  if("${child}" IN_LIST FORMOSA_TEST_SUPPORT_DIRS)
    continue()
  endif()
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${child} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${child}/CMakeLists.txt)
    list(APPEND FORMOSA_TEST_DIRS ${child})
  endif()
endforeach()

lv_register_test_lua_roots("${CMAKE_CURRENT_SOURCE_DIR}")

set(TESTS_ENABLE_TESTS "all" CACHE STRING "Semicolon-separated list of test subdirectories to enable, or 'all'")
if(TESTS_ENABLE_TESTS STREQUAL "all")
  set(enabled_tests ${FORMOSA_TEST_DIRS})
else()
  set(enabled_tests ${TESTS_ENABLE_TESTS})
endif()

foreach(test_dir ${enabled_tests})
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}")
    add_subdirectory(${test_dir})
  else()
    message(WARNING "Test subdirectory '${test_dir}' not found. Check TESTS_ENABLE_TESTS.")
  endif()
endforeach()
