# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

find_package(OpenCL REQUIRED)

set(OPENCL_RUN_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/run_opencl.lua)

# Ensure output directory exists for stats and traces
set(TESTING_OUT ${CMAKE_BINARY_DIR}/testing-out)
file(MAKE_DIRECTORY ${TESTING_OUT})

set(OPENCL_SMS simtix.atomic_sm simtix.pipelined_sm)
set(OPENCL_PERF_CASES "${CMAKE_CURRENT_BINARY_DIR}/opencl_perf_cases.tsv")
file(WRITE "${OPENCL_PERF_CASES}" "")

function(add_lv_opencl_integration_test PROGRAM)
  cmake_parse_arguments(TEST "" "ID;SUFFIX;WORKING_DIRECTORY;CONFIG" "ARGS;WRAPPER" ${ARGN})

  if(NOT TEST_WORKING_DIRECTORY)
    set(TEST_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  endif()
  set(CONFIG_ARGS "")
  if(TEST_CONFIG)
    list(APPEND CONFIG_ARGS --config "${TEST_CONFIG}")
  endif()

  # Logic: Start with Program name, append Suffix, or override with ID
  set(FINAL_ID ${PROGRAM})
  if(TEST_SUFFIX)
    string(APPEND FINAL_ID "_${TEST_SUFFIX}")
  endif()
  if(TEST_ID)
    set(FINAL_ID ${TEST_ID})
  endif()

  # The perf manifest stores arguments without per-test execution settings.
  if(NOT FINAL_ID MATCHES "^smoke\\."
      AND TEST_WORKING_DIRECTORY STREQUAL "${PROJECT_SOURCE_DIR}"
      AND NOT TEST_CONFIG AND NOT TEST_WRAPPER)
    string(REPLACE ";" "\t" TEST_ARGS_TSV "${TEST_ARGS}")
    if(TEST_ARGS_TSV)
      file(APPEND "${OPENCL_PERF_CASES}" "${FINAL_ID}\t${PROGRAM}\t${TEST_ARGS_TSV}\n")
    else()
      file(APPEND "${OPENCL_PERF_CASES}" "${FINAL_ID}\t${PROGRAM}\n")
    endif()
  endif()

  # Preserve the wrapper list through the helper's argument parsing.
  string(REPLACE ";" "\\\\;" WRAPPER_ARG "${TEST_WRAPPER}")

  foreach(sm ${OPENCL_SMS})
    set(TEST_NAME opencl.${FINAL_ID}.${sm})
    set(TEST_OUTPUT ${TESTING_OUT}/${TEST_NAME})
    add_lv_test_script(${TEST_NAME}
      ${OPENCL_RUN_SCRIPT}
      ARGS -s ${TEST_OUTPUT}.toml
           -t ${TEST_OUTPUT}
           --sm ${sm}
           ${CONFIG_ARGS}
           -- $<TARGET_FILE:${PROGRAM}> ${TEST_ARGS}
      WRAPPER "${WRAPPER_ARG}"
      LABELS integration opencl
      WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}"
    )
    set_tests_properties(${TEST_NAME} PROPERTIES
      ENVIRONMENT "POCL_CACHE_DIR=${TEST_OUTPUT}/pocl-cache"
      PROCESSORS 2
    )
  endforeach()
endfunction()

set(OCL_DEFAULT_OPTS
  CL_TARGET_OPENCL_VERSION=120
  CL_HPP_TARGET_OPENCL_VERSION=120
  CL_HPP_MINIMUM_OPENCL_VERSION=120
  CL_HPP_ENABLE_EXCEPTIONS=ON
  CL_HPP_CL_1_2_DEFAULT_BUILD)

lv_script(run_opencl.lua)

add_subdirectory(smoke)
add_subdirectory(formosa)
add_subdirectory(rodinia)
add_subdirectory(parboil)
add_subdirectory(shoc)
add_subdirectory(gemma)
