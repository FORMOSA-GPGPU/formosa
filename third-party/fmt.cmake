# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Populate fmt via FetchContent from the pinned submodule (no download).
# OVERRIDE_FIND_PACKAGE redirects DRAMSys's find_package(fmt) to this copy
# instead of letting it fetch its own.
include(FetchContent)
FetchContent_Declare(fmt
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/fmt
  OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(fmt)
set_target_properties(fmt
  PROPERTIES POSITION_INDEPENDENT_CODE TRUE
)
