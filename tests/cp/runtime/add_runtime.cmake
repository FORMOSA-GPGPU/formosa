# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

apple_cross_cc_shim(CMAKE_C_COMPILER "${CLANG}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
find_program(CP_LUA NAMES luajit lua REQUIRED)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
  "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
  "${CP_LUA}" -e
  "local p=require('cp.platform'); print(p.ram_base .. ';' .. p.ram_size .. ';' .. p.text_base .. ';' .. p.stack_base .. ';' .. p.serial_base)"
  OUTPUT_VARIABLE PLATFORM OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
list(GET PLATFORM 0 IMAGE_RAM_BASE)
list(GET PLATFORM 1 IMAGE_RAM_SIZE)
list(GET PLATFORM 2 IMAGE_TEXT_BASE)
list(GET PLATFORM 3 IMAGE_STACK_BASE)
list(GET PLATFORM 4 IMAGE_SERIAL_BASE)
set(CP_RUNTIME_LINKER "${CMAKE_CURRENT_BINARY_DIR}/runtime/linker.ld")
configure_file("${CMAKE_CURRENT_LIST_DIR}/linker.ld.in" "${CP_RUNTIME_LINKER}" @ONLY)

function(add_cp_runtime ARCH)
  set(RUNTIME "cp-runtime-${ARCH}")
  if(TARGET ${RUNTIME})
    return()
  endif()
  if(NOT ARCH MATCHES "^rv(32|64)")
    message(FATAL_ERROR "Unsupported CP runtime architecture: ${ARCH}")
  endif()
  set(XLEN "${CMAKE_MATCH_1}")
  set(SUPPORT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  set(NANOLIBC "${formosa_SOURCE_DIR}/fw/nanolibc/c")
  set(TINYPRINTF "${formosa_SOURCE_DIR}/fw/tinyprintf")
  add_library(${RUNTIME} OBJECT "${SUPPORT}/start.S" "${SUPPORT}/stdio.c" "${SUPPORT}/arithmetic.c"
    "${NANOLIBC}/string.c" "${TINYPRINTF}/tinyprintf.c")
  target_include_directories(${RUNTIME} PUBLIC "${NANOLIBC}/include"
    PRIVATE "${TINYPRINTF}")
  target_compile_options(${RUNTIME} PUBLIC
    --target=riscv${XLEN} -march=${ARCH} -ffreestanding -fno-builtin
    -fno-stack-protector -ffunction-sections -fdata-sections
    PRIVATE -O2 -fno-lto)
  target_link_options(${RUNTIME} INTERFACE
    --target=riscv${XLEN} -march=${ARCH} -fuse-ld=lld -static -nostdlib
    -Wl,--gc-sections -T "${CP_RUNTIME_LINKER}")
  set_property(TARGET ${RUNTIME} PROPERTY INTERFACE_LINK_DEPENDS "${CP_RUNTIME_LINKER}")
endfunction()
