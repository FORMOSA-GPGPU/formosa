# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

find_program(CP_LUA NAMES luajit lua REQUIRED)
foreach(CORE IN LISTS CP_SELECTED_CORES)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
    "${CP_LUA}" -e
    "require('cp.cores'); local core = require('${CORE}'); if core.has_ext_int then print(core.isa .. ';' .. string.format('0x%x', require('cp.platform').text_base)) end"
    OUTPUT_VARIABLE CORE_INFO OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(NOT CORE_INFO)
    continue()
  endif()
  list(GET CORE_INFO 0 ISA)
  list(GET CORE_INFO 1 TEXT_BASE)
  if(NOT ISA MATCHES "^rv(32|64)")
    message(FATAL_ERROR "Unsupported interrupt-test ISA: ${ISA}")
  endif()
  set(XLEN "${CMAKE_MATCH_1}")
  set(IMAGE "cp-interrupt-rv${XLEN}.elf")
  if(NOT TARGET ${IMAGE})
    add_executable(${IMAGE} interrupt.S)
    set_target_properties(${IMAGE} PROPERTIES
      OUTPUT_NAME "rv${XLEN}.elf"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/cp/interrupt")
    target_compile_options(${IMAGE} PRIVATE --target=riscv${XLEN} -march=rv${XLEN}i_zicsr)
    target_link_options(${IMAGE} PRIVATE --target=riscv${XLEN} -march=rv${XLEN}i_zicsr
      -fuse-ld=lld -nostdlib -static -Wl,-Ttext=${TEXT_BASE} -Wl,-e,start)
  endif()
  add_lv_test_script(cp.interrupt.${CORE} run.lua
    ARGS --core "${CORE}"
    LABELS cp interrupt integration ${CORE})
  set_tests_properties(cp.interrupt.${CORE} PROPERTIES TIMEOUT 30)
endforeach()
