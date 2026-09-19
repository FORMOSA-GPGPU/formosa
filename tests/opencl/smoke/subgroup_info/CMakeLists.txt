# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(subgroup_info main.cc)
target_link_libraries(subgroup_info PRIVATE OpenCL::OpenCL)
target_compile_definitions(subgroup_info PRIVATE
  CL_TARGET_OPENCL_VERSION=300
  CL_HPP_TARGET_OPENCL_VERSION=120
  CL_HPP_MINIMUM_OPENCL_VERSION=120
  CL_HPP_ENABLE_EXCEPTIONS=ON
  CL_HPP_CL_1_2_DEFAULT_BUILD)
add_lv_opencl_integration_test(subgroup_info ID smoke.subgroup_info)
