# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

find_program(XMAKE_EXECUTABLE NAMES xmake REQUIRED)
include(TestBigEndian)
test_big_endian(ILHA_HOST_BIG_ENDIAN)
if(ILHA_HOST_BIG_ENDIAN)
  message(FATAL_ERROR
    "Ilha SystemInfo ABI is little-endian and requires a little-endian host")
endif()

set(ILHA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/ilha" CACHE PATH
    "Build directory for Ilha-generated software platform data")
set(ILHA_ADDR_MAP_HEADER "${ILHA_GENERATED_DIR}/formosa_addr_map.h")
set(ILHA_MEMORY_LINKER_SCRIPT "${ILHA_GENERATED_DIR}/ilha_memory.ld")

add_custom_command(
  OUTPUT "${ILHA_ADDR_MAP_HEADER}"
  COMMAND "${XMAKE_EXECUTABLE}" lua
          "${formosa_SOURCE_DIR}/ilha/scripts/generate_header.lua"
          "${ILHA_ADDR_MAP_HEADER}" "${formosa_SOURCE_DIR}"
  DEPENDS
    "${formosa_SOURCE_DIR}/ilha/scripts/generate_header.lua"
    "${formosa_SOURCE_DIR}/ilha/lua/addr_map.lua"
  COMMENT "Generating Ilha address-map header"
  VERBATIM)

add_custom_command(
  OUTPUT "${ILHA_MEMORY_LINKER_SCRIPT}"
  COMMAND "${XMAKE_EXECUTABLE}" lua
          "${formosa_SOURCE_DIR}/ilha/scripts/generate_linker.lua"
          "${ILHA_MEMORY_LINKER_SCRIPT}" "${formosa_SOURCE_DIR}"
  DEPENDS
    "${formosa_SOURCE_DIR}/ilha/scripts/generate_linker.lua"
    "${formosa_SOURCE_DIR}/ilha/lua/addr_map.lua"
  COMMENT "Generating Ilha linker script"
  VERBATIM)

add_custom_target(ilha-platform-data ALL
  DEPENDS "${ILHA_ADDR_MAP_HEADER}" "${ILHA_MEMORY_LINKER_SCRIPT}")
