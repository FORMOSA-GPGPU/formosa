# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(embedding main.cc)
target_link_libraries(embedding PRIVATE OpenCL::OpenCL)
target_compile_definitions(embedding PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(embedding)
