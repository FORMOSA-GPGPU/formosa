# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

foreach(CORE IN LISTS CP_SELECTED_CORES)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
    "${CP_LUA}" -e "require('cp.cores'); print(require('${CORE}').isa)"
    OUTPUT_VARIABLE ISA OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(NOT ISA MATCHES "^rv(32|64)im")
    continue()
  endif()
  set(ARCH "rv${CMAKE_MATCH_1}im")
  add_cp_runtime(${ARCH})
  set(IMAGE "cp-runtime-test-${ARCH}")
  if(NOT TARGET ${IMAGE})
    add_executable(${IMAGE} test.c)
    target_link_libraries(${IMAGE} PRIVATE cp-runtime-${ARCH})
    target_compile_options(${IMAGE} PRIVATE -O2 -fno-lto)
    set_target_properties(${IMAGE} PROPERTIES
      OUTPUT_NAME "${ARCH}" SUFFIX ".elf"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  add_lv_test_script(cp.runtime.${CORE} test.lua ARGS --core "${CORE}"
    LABELS cp runtime integration ${CORE})
  set_tests_properties(cp.runtime.${CORE} PROPERTIES TIMEOUT 30)
endforeach()
