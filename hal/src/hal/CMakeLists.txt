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
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # HAL is a C API but the static archive uses libstdc++. Darwin PoCL is
    # clang/libc++; export the GNU runtime so the ICD can link it.
    set(_formosa_stdcxx "")
    foreach(_name IN ITEMS libstdc++.dylib libstdc++.so)
      execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=${_name}
        OUTPUT_VARIABLE _formosa_stdcxx
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(EXISTS "${_formosa_stdcxx}")
        break()
      endif()
    endforeach()
    if(EXISTS "${_formosa_stdcxx}")
      target_link_libraries(${target} INTERFACE "${_formosa_stdcxx}")
    else()
      target_link_libraries(${target} INTERFACE stdc++)
    endif()
  endif()
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
