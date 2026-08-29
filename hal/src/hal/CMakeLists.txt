# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

function(add_formosa_hal name)
  set(target formosa-hal.${name})
  add_library(${target} STATIC
    hal.cc
    hal-capture-recorder.cc
    $<TARGET_OBJECTS:gpufw_elf_carr>
  )
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${hal_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE "${hal_SOURCE_DIR}/src"
    PRIVATE "${formosa_SOURCE_DIR}/addr_map"
    PRIVATE $<TARGET_PROPERTY:gpufw_elf_carr,INTERFACE_INCLUDE_DIRECTORIES>
  )
  target_compile_options(${target}
    PRIVATE -Wall -Wextra -Wno-unused-parameter -g
  )
  target_compile_features(${target} PUBLIC cxx_std_17)

  target_link_libraries(${target}
    PRIVATE formosa-real.${name} falloc
  )
  set_target_properties(${target}
    PROPERTIES OUTPUT_NAME ${target}
  )

  if(HAL_ENABLE_GCOV)
    target_compile_options(${target} PUBLIC --coverage)
    target_link_libraries(${target} INTERFACE --coverage)
  endif()

  formosa_install_target(${target})
endfunction()

add_formosa_hal(lv)
