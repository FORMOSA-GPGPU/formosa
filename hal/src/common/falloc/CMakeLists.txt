# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set_source_files_properties(falloc.c PROPERTIES LANGUAGE C)

add_library(falloc STATIC
  falloc.c
)

target_compile_options(falloc
  PRIVATE -Wall -Wextra -Wno-unused-parameter -g
)

target_include_directories(falloc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  PRIVATE
    $<BUILD_INTERFACE:${hal_SOURCE_DIR}/include>)

install(TARGETS falloc
  EXPORT FormosaTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
