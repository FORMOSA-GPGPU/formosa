# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(target formosa-real.lv)
add_library(${target} STATIC
  lv-driver.cc
  lv-config.cc
)

if(DEFINED formosa_SOURCE_DIR)
  set(_fsa_addr_map_dir "${formosa_SOURCE_DIR}/addr_map")
else()
  set(_fsa_addr_map_dir "${hal_SOURCE_DIR}/../addr_map")
endif()

target_include_directories(${target}
  PRIVATE
    "${hal_SOURCE_DIR}/src"
    "${_fsa_addr_map_dir}"
)

target_compile_options(${target}
  PRIVATE -Wall -Wextra -Wno-unused-parameter -g
)

target_link_libraries(${target}
  PRIVATE libcomm falloc
)

formosa_install_target(${target})
