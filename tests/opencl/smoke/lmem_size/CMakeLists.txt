# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(lmem_size main.cc)
target_link_libraries(lmem_size PRIVATE OpenCL::OpenCL)
target_compile_definitions(lmem_size PRIVATE ${OCL_DEFAULT_OPTS})
add_lv_opencl_integration_test(lmem_size ID smoke.lmem_size)
