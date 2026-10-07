# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(lmem_bank_parallel main.cc)
target_link_libraries(lmem_bank_parallel PRIVATE OpenCL::OpenCL)
target_compile_definitions(lmem_bank_parallel PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(lmem_bank_parallel)
