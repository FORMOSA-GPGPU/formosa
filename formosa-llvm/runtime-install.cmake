# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Used by the local compiler-rt shell. The resource directory also needs the
# selected compiler's builtin headers, including the OpenCL headers PoCL uses.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  execute_process(
    COMMAND "${CMAKE_C_COMPILER}" --print-resource-dir
    OUTPUT_VARIABLE formosa_clang_resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  install(DIRECTORY "${formosa_clang_resource_dir}/include/" DESTINATION include)
endif()
