# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(spirv_smoke main.cc)
target_link_libraries(spirv_smoke PRIVATE OpenCL::OpenCL)
target_compile_definitions(spirv_smoke PRIVATE ${OCL_DEFAULT_OPTS})
add_lv_opencl_integration_test(spirv_smoke ID smoke.spirv_smoke)
