# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(rmsnorm main.cc)
target_link_libraries(rmsnorm PRIVATE OpenCL::OpenCL)
target_compile_definitions(rmsnorm PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(rmsnorm)
