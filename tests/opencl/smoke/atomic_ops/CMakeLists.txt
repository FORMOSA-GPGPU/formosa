# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(atomic_ops main.cc)
target_link_libraries(atomic_ops PRIVATE OpenCL::OpenCL)
target_compile_definitions(atomic_ops PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(atomic_ops ID smoke.atomic_ops)
