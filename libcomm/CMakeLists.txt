# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Works with 3.14 and tested through 3.28
cmake_minimum_required(VERSION 3.14...3.28)

# Project name and a few useful settings. Other commands can pick up the results
project(
  libcomm
  VERSION 0.1
  DESCRIPTION "CASLab Virtual Platform IPC Library"
  LANGUAGES CXX)

# Always dump compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Setup output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(libcomm INTERFACE)
target_include_directories(libcomm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

install(TARGETS libcomm
  EXPORT FormosaTargets)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

option(LIBCOMM_ENABLE_EXAMPLE "Enable example for libcomm" OFF)
if(LIBCOMM_ENABLE_EXAMPLE)
  message(STATUS "Enabling example for libcomm")
  add_executable(client example/client.cc)
  target_link_libraries(client PRIVATE libcomm)
  add_executable(server example/server.cc)
  target_link_libraries(server PRIVATE libcomm)
endif()

option(LIBCOMM_ENABLE_TESTING "Build the libcomm public-header consumer test" OFF)
if(LIBCOMM_ENABLE_TESTING)
  include(CTest)
  enable_testing()
  add_executable(libcomm_unit_test tests/unit/consumer.cc)
  target_link_libraries(libcomm_unit_test PRIVATE libcomm)
  add_test(NAME libcomm.unit COMMAND libcomm_unit_test)
  set_tests_properties(libcomm.unit PROPERTIES LABELS "libcomm;unit")

  add_test(
    NAME libcomm.unit.install
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
            --prefix ${CMAKE_BINARY_DIR}/libcomm-install)
  set_tests_properties(libcomm.unit.install
                       PROPERTIES FIXTURES_SETUP libcomm.unit.install)

  add_test(
    NAME libcomm.unit.install_consumer
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/install_consumer.sh
            ${CMAKE_BINARY_DIR}/libcomm-install)
  set_tests_properties(libcomm.unit.install_consumer
                       PROPERTIES FIXTURES_REQUIRED libcomm.unit.install)
endif()

# Use GCOV to generate test coverages
option(LIBCOMM_ENABLE_GCOV "Enable gcov support for libcomm" OFF)
if(LIBCOMM_ENABLE_GCOV)
  target_compile_options(libcomm INTERFACE --coverage)
  target_link_libraries(libcomm INTERFACE --coverage)
endif()
