# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# DRAMSys 5.6. As a subdirectory all DRAMSYS_USE_FETCH_CONTENT_* options
# default OFF, so DRAMSys find_package()s its dependencies:
# - SystemCLanguage / fmt are already provided by systemc.cmake / fmt.cmake
#   (OVERRIDE_FIND_PACKAGE, both run before this file).
# - DRAMUtils / nlohmann_json come from pinned submodules: declaring them here
#   first (with OVERRIDE_FIND_PACKAGE) wins over DRAMSys's own URL-based
#   FetchContent declarations, so nothing is downloaded.
# - The vendored sqlite3 stays enabled; it FetchContent-downloads the
#   amalgamation at configure time, same behavior as before 5.6.
# - We don't need the DRAMSys CLI, and DRAMPower stays off (parity with the
#   pre-5.6 integration).
set(DRAMSYS_BUILD_CLI OFF)
set(DRAMSYS_USE_DRAMPOWER OFF)
set(DRAMSYS_USE_FETCH_CONTENT_SQLITE3 ON)

include(FetchContent)
FetchContent_Declare(DRAMUtils
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/DRAMUtils
  OVERRIDE_FIND_PACKAGE
)
FetchContent_Declare(nlohmann_json
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/nlohmann_json
  OVERRIDE_FIND_PACKAGE
)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/DRAMSys)
