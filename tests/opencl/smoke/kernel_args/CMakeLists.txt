# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(kernel_args main.cc)
target_link_libraries(kernel_args PRIVATE OpenCL::OpenCL)
target_compile_definitions(kernel_args PRIVATE ${OCL_DEFAULT_OPTS})
add_lv_opencl_integration_test(kernel_args ID smoke.kernel_args)
