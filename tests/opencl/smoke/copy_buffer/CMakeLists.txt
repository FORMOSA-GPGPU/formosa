# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(copy_buffer main.cc)
target_link_libraries(copy_buffer PRIVATE OpenCL::OpenCL)
target_compile_definitions(copy_buffer PRIVATE ${OCL_DEFAULT_OPTS})
add_lv_opencl_integration_test(copy_buffer ID smoke.copy_buffer)
