# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(kernel_trap main.cc)
target_link_libraries(kernel_trap PRIVATE OpenCL::OpenCL)
target_compile_definitions(kernel_trap PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(kernel_trap ID smoke.kernel_trap)
