# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(atomic_ops main.cc)
target_link_libraries(atomic_ops PRIVATE OpenCL::OpenCL)
target_compile_definitions(atomic_ops PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl"
  C11_KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/c11.cl")
add_lv_opencl_integration_test(atomic_ops ID smoke.atomic_ops)

find_program(LLVM_SPIRV NAMES llvm-spirv REQUIRED)
set(C11_SPIRV "${CMAKE_CURRENT_BINARY_DIR}/c11.spv")
add_custom_command(OUTPUT "${C11_SPIRV}"
  COMMAND ${CLANG} --target=spir64 -x cl -cl-std=CL2.0 -O1 -emit-llvm -c
    "${CMAKE_CURRENT_SOURCE_DIR}/c11.cl" -o "${CMAKE_CURRENT_BINARY_DIR}/c11.bc"
  COMMAND ${LLVM_SPIRV} "${CMAKE_CURRENT_BINARY_DIR}/c11.bc" -o "${C11_SPIRV}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/c11.cl"
  VERBATIM)
add_custom_target(atomic_ops_spirv DEPENDS "${C11_SPIRV}")
add_dependencies(atomic_ops atomic_ops_spirv)
add_lv_opencl_integration_test(atomic_ops ID smoke.atomic_ops_spirv
  ARGS "${C11_SPIRV}")
