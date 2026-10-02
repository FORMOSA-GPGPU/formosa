# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

find_program(CP_LUA NAMES luajit lua REQUIRED)
foreach(CORE IN LISTS CP_SELECTED_CORES)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
    "${CP_LUA}" -e "require('cp.cores'); print(require('${CORE}').isa)"
    OUTPUT_VARIABLE ISA OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(NOT ISA MATCHES "^rv(32|64)i")
    continue()
  endif()
  set(XLEN "${CMAKE_MATCH_1}")
  set(IMAGE "cp-prog0-rv${XLEN}")
  if(NOT TARGET ${IMAGE})
    add_executable(${IMAGE} setup.S test.S)
    set_target_properties(${IMAGE} PROPERTIES
      OUTPUT_NAME "rv${XLEN}" SUFFIX ".elf"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/cp/prog0"
      LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")
    target_compile_options(${IMAGE} PRIVATE --target=riscv${XLEN} -march=rv${XLEN}i)
    target_link_options(${IMAGE} PRIVATE --target=riscv${XLEN} -march=rv${XLEN}i
      -fuse-ld=lld -static -nostdlib -Wl,--gc-sections -T "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")
  endif()
  add_lv_test_script(cp.prog0.${CORE} run.lua
    ARGS --core "${CORE}" --golden "${CMAKE_CURRENT_SOURCE_DIR}/rv${XLEN}.golden"
    LABELS cp prog0 integration ${CORE})
  set_tests_properties(cp.prog0.${CORE} PROPERTIES TIMEOUT 120)
endforeach()
