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
  set(XLEN "${CMAKE_MATCH_1}")
  foreach(PROFILE performance validation)
    set(BENCHMARK coremark)
    if(PROFILE STREQUAL "validation")
      set(BENCHMARK coremark-validation)
    endif()
    set(IMAGE "cp-${BENCHMARK}-rv${XLEN}im")
    if(NOT TARGET ${IMAGE})
      add_cp_benchmark_image(${IMAGE} ${XLEN})
      target_sources(${IMAGE} PRIVATE core_portme.c upstream/core_main.c
        upstream/core_list_join.c upstream/core_matrix.c upstream/core_state.c
        upstream/core_util.c)
      target_include_directories(${IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/upstream")
      target_compile_definitions(${IMAGE} PRIVATE ITERATIONS=10)
      get_target_property(COREMARK_OPTIONS ${IMAGE} COMPILE_OPTIONS)
      get_target_property(RUNTIME_OPTIONS cp-runtime-rv${XLEN}im INTERFACE_COMPILE_OPTIONS)
      string(JOIN " " COREMARK_FLAGS ${COREMARK_OPTIONS} ${RUNTIME_OPTIONS})
      target_compile_definitions(${IMAGE} PRIVATE COMPILER_FLAGS="${COREMARK_FLAGS}")
      if(PROFILE STREQUAL "validation")
        target_compile_definitions(${IMAGE} PRIVATE VALIDATION_RUN=1)
        set_target_properties(${IMAGE} PROPERTIES OUTPUT_NAME "rv${XLEN}im-validation")
      else()
        target_compile_definitions(${IMAGE} PRIVATE PERFORMANCE_RUN=1)
      endif()
    endif()
    add_lv_test_script(cp.${BENCHMARK}.${CORE} run.lua
      ARGS --core "${CORE}" --profile "${PROFILE}"
      LABELS cp coremark benchmark integration ${CORE})
    set_tests_properties(cp.${BENCHMARK}.${CORE} PROPERTIES TIMEOUT 300)
  endforeach()
endforeach()
