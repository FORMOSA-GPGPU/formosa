# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(memory_ops main.cc)
target_link_libraries(memory_ops PRIVATE OpenCL::OpenCL)
target_compile_definitions(memory_ops PRIVATE ${OCL_DEFAULT_OPTS})
add_lv_opencl_integration_test(memory_ops ID smoke.memory_ops)
