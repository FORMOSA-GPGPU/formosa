# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# target for c++
add_library(stb INTERFACE)

target_include_directories(stb
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

set(STB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# target for luajit ffi
add_library(luajitstb SHARED libstb.c)
set_target_properties(luajitstb PROPERTIES POSITION_INDEPENDENT_CODE ON)