# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

set(DHRYSTONE_UPSTREAM "${RISCV_TESTS_SOURCE_DIR}/benchmarks/dhrystone")
foreach(CORE IN LISTS CP_SELECTED_CORES)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
    "${CP_LUA}" -e "require('cp.cores'); print(require('${CORE}').isa)"
    OUTPUT_VARIABLE ISA OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(NOT ISA MATCHES "^rv(32|64)im")
    continue()
  endif()
  set(XLEN "${CMAKE_MATCH_1}")
  set(IMAGE "cp-dhrystone-rv${XLEN}im")
  if(NOT TARGET ${IMAGE})
    add_cp_benchmark_image(${IMAGE} ${XLEN})
    target_sources(${IMAGE} PRIVATE main.c "${DHRYSTONE_UPSTREAM}/dhrystone.c")
    target_include_directories(${IMAGE} PRIVATE "${DHRYSTONE_UPSTREAM}")
    target_compile_options(${IMAGE} PRIVATE
      -std=gnu89 -fno-inline -Wno-implicit-int -Wno-implicit-function-declaration
      -Wno-deprecated-non-prototype)
    target_compile_definitions(${IMAGE} PRIVATE DHRYSTONE_ITERATIONS=1000)
  endif()
  add_lv_test_script(cp.dhrystone.${CORE} run.lua ARGS --core "${CORE}"
    LABELS cp dhrystone benchmark integration ${CORE})
  set_tests_properties(cp.dhrystone.${CORE} PROPERTIES TIMEOUT 900)
endforeach()
