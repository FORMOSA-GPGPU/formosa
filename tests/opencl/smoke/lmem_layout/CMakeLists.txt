# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(lmem_layout main.cc)
target_link_libraries(lmem_layout PRIVATE OpenCL::OpenCL)
target_compile_definitions(lmem_layout PRIVATE ${OCL_DEFAULT_OPTS})
add_lv_opencl_integration_test(lmem_layout ID smoke.lmem_layout)
