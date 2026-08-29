# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Populate SystemC via FetchContent from the pinned submodule (no download).
# OVERRIDE_FIND_PACKAGE redirects DRAMSys's find_package(SystemCLanguage) to
# this copy, so exactly one SystemC exists in the build.
include(FetchContent)
FetchContent_Declare(SystemCLanguage
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/systemc
  OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(SystemCLanguage)
# Suppress warnings caused by SystemC internal headers.
set_target_properties(systemc
  PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
  $<TARGET_PROPERTY:systemc,INTERFACE_INCLUDE_DIRECTORIES>)
