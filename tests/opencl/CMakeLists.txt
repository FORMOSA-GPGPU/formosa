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
  cmake_parse_arguments(TEST "" "ID;SUFFIX" "ARGS" ${ARGN})

  # Logic: Start with Program name, append Suffix, or override with ID
  set(FINAL_ID ${PROGRAM})
  if(TEST_SUFFIX)
    string(APPEND FINAL_ID "_${TEST_SUFFIX}")
  endif()
  if(TEST_ID)
    set(FINAL_ID ${TEST_ID})
  endif()

  if(NOT FINAL_ID MATCHES "^smoke\\.")
    string(REPLACE ";" "\t" TEST_ARGS_TSV "${TEST_ARGS}")
    if(TEST_ARGS_TSV)
      file(APPEND "${OPENCL_PERF_CASES}" "${FINAL_ID}\t${PROGRAM}\t${TEST_ARGS_TSV}\n")
    else()
      file(APPEND "${OPENCL_PERF_CASES}" "${FINAL_ID}\t${PROGRAM}\n")
    endif()
  endif()

  foreach(sm ${OPENCL_SMS})
    add_lv_test_script(opencl.${FINAL_ID}.${sm}
      ${OPENCL_RUN_SCRIPT}
      ARGS -s ${TESTING_OUT}/${FINAL_ID}.toml
           -t ${TESTING_OUT}/${FINAL_ID}
           --sm ${sm}
           -- $<TARGET_FILE:${PROGRAM}> ${TEST_ARGS}
      LABELS integration opencl
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
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
lv_script(run_opencl_replay.lua)

add_subdirectory(mandelbrot)
add_subdirectory(montecarlo)
add_subdirectory(vecadd)
add_subdirectory(vecmul)
add_subdirectory(gemm)
add_subdirectory(batched_gemm)
add_subdirectory(gustavson_gemm)
add_subdirectory(conv2d)
add_subdirectory(pooling)
add_subdirectory(transpose)
add_subdirectory(prefix_sum)
add_subdirectory(lmem_bank_parallel)
add_subdirectory(bfs)
add_subdirectory(gaussian)
add_subdirectory(kmeans)
add_subdirectory(nn)
add_subdirectory(activation)
add_subdirectory(bias_relu)
add_subdirectory(layernorm)
add_subdirectory(softmax)
add_subdirectory(embedding)
add_subdirectory(attention)
add_subdirectory(transformer_block)
add_subdirectory(residual_mlp)
add_subdirectory(swiglu)
add_subdirectory(gru)
add_subdirectory(rmsnorm)
add_subdirectory(gemma)

add_subdirectory(smoke)
add_subdirectory(spmv)
