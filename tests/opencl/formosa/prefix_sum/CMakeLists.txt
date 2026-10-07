# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(prefix_sum main.cc)
target_link_libraries(prefix_sum PRIVATE OpenCL::OpenCL)
target_compile_definitions(prefix_sum PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(prefix_sum)
