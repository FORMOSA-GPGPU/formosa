# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(GEMMA_RUNFILES_DIR "${CMAKE_BINARY_DIR}/tests/opencl/gemma")
file(MAKE_DIRECTORY "${GEMMA_RUNFILES_DIR}")

function(gemma_runfile source_file)
  get_filename_component(file_name "${source_file}" NAME)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}"
    "${GEMMA_RUNFILES_DIR}/${file_name}"
    COPYONLY
  )
endfunction()

set(GEMMA_SOURCE_RUNFILES
  kernels/rmsnorm.cl
  kernels/qk_norm.cl
  kernels/gemm.cl
  kernels/rope.cl
  kernels/kv_cache_update.cl
  kernels/gqa.cl
  kernels/geglu.cl
  kernels/residual_add.cl
)

foreach(runfile ${GEMMA_SOURCE_RUNFILES})
  gemma_runfile(${runfile})
endforeach()

function(add_gemma_executable target_name)
  add_executable(
    ${target_name}
    ${target_name}.cc
    model_loader.cc
    gemma_transformer_layer.cc
  )

  target_link_libraries(
    ${target_name}
    PRIVATE OpenCL::OpenCL
  )

  target_compile_features(
    ${target_name}
    PRIVATE cxx_std_20
  )

  target_compile_definitions(
    ${target_name}
    PRIVATE GEMMA_ROOT="${GEMMA_RUNFILES_DIR}"
    PRIVATE GEMMA_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE ${OCL_DEFAULT_OPTS}
    PRIVATE CL_HPP_ENABLE_EXCEPTIONS=ON
  )

  target_compile_options(
    ${target_name}
    PRIVATE $<$<CONFIG:Debug>:-ggdb3 -O0>
  )
endfunction()

add_gemma_executable(gemma)
add_gemma_executable(verify_layer_e2e)
add_gemma_executable(verify_layer_breakdown)
add_gemma_executable(verify_full)
