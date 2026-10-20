# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

function(add_formosa_hal name)
  set(target formosa-hal.${name})
  add_library(${target} STATIC
    hal.cc
    $<TARGET_OBJECTS:gpufw_elf_carr>
  )
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${hal_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE "${hal_SOURCE_DIR}/src"
    PRIVATE "${ILHA_GENERATED_DIR}"
    PRIVATE $<TARGET_PROPERTY:gpufw_elf_carr,INTERFACE_INCLUDE_DIRECTORIES>
  )
  add_dependencies(${target} ilha-platform-data)
  target_compile_options(${target}
    PRIVATE -Wall -Wextra -Wno-unused-parameter -g
  )
  target_compile_features(${target} PUBLIC cxx_std_17)

  target_link_libraries(${target}
    PRIVATE formosa-real.${name} falloc
  )
  # HAL exposes a C API. Export the producer's C++ runtime so C consumers
  # and consumers using another standard library can link the static archive.
  foreach(_runtime IN ITEMS stdc++ c++)
    if("${_runtime}" IN_LIST CMAKE_CXX_IMPLICIT_LINK_LIBRARIES)
      unset(_formosa_cxxruntime CACHE)
      find_library(_formosa_cxxruntime
        NAMES "${_runtime}"
        PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES}
        NO_DEFAULT_PATH
      )
      if(_formosa_cxxruntime)
        target_link_libraries(${target} INTERFACE "${_formosa_cxxruntime}")
      else()
        target_link_libraries(${target} INTERFACE "${_runtime}")
      endif()
      unset(_formosa_cxxruntime CACHE)
    endif()
  endforeach()
  set_target_properties(${target}
    PROPERTIES OUTPUT_NAME ${target}
  )

  if(ENABLE_COVERAGE)
    target_compile_options(${target} PUBLIC --coverage)
    target_link_libraries(${target} INTERFACE --coverage)
  endif()

  formosa_install_target(${target})
endfunction()

add_formosa_hal(lv)
