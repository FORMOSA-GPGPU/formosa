# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

foreach(FORMOSA_WORKLOAD IN ITEMS
  mandelbrot
  montecarlo
  vecadd
  vecmul
  gemm
  batched_gemm
  gustavson_gemm
  conv2d
  pooling
  transpose
  prefix_sum
  lmem_bank_parallel
  image_blur
  activation
  bias_relu
  layernorm
  softmax
  embedding
  attention
  transformer_block
  residual_mlp
  swiglu
  gru
  rmsnorm
  spmv
)
  add_subdirectory(${FORMOSA_WORKLOAD})
  get_property(SUITE_TESTS DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${FORMOSA_WORKLOAD}"
    PROPERTY TESTS)
  set_property(TEST ${SUITE_TESTS} DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${FORMOSA_WORKLOAD}"
    APPEND PROPERTY LABELS formosa)
endforeach()
