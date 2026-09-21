# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(target formosa-real.lv)
add_library(${target} STATIC
  lv-driver.cc
  lv-config.cc
)

if(DEFINED ILHA_GENERATED_DIR)
  set(_fsa_addr_map_dir "${ILHA_GENERATED_DIR}")
else()
  message(FATAL_ERROR "Ilha-generated platform data is required to build the LV HAL backend")
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
if(TARGET ilha-platform-data)
  add_dependencies(${target} ilha-platform-data)
endif()

formosa_install_target(${target})
