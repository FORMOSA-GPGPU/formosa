# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# The shared extension supplies missing ATen operators without changing either
# upstream dependency.
option(DLPRIM_TESTS "Enable the dlprim simulator smoke test" OFF)
if(NOT DLPRIM_TESTS)
  return()
endif()

set(OPENCL_SMS simtix.atomic_sm)

set(TORCH_ROOT "$ENV{TORCH_ROOT}" CACHE PATH "LibTorch installation")
set(TORCH_CXX11_ABI 1 CACHE STRING "LibTorch C++11 ABI")
if(NOT TORCH_ROOT)
  message(FATAL_ERROR "Set TORCH_ROOT to the LibTorch installation")
endif()

add_library(dlprim_monitor SHARED monitor.cc)
target_compile_features(dlprim_monitor PUBLIC cxx_std_20)
target_include_directories(dlprim_monitor SYSTEM PUBLIC
  "${TORCH_ROOT}/include" "${TORCH_ROOT}/include/torch/csrc/api/include")
target_compile_definitions(dlprim_monitor PUBLIC
  _GLIBCXX_USE_CXX11_ABI=${TORCH_CXX11_ABI} ${OCL_DEFAULT_OPTS})
target_link_libraries(dlprim_monitor PUBLIC
  "${TORCH_ROOT}/lib/libtorch.so" "${TORCH_ROOT}/lib/libtorch_cpu.so"
  "${TORCH_ROOT}/lib/libc10.so" OpenCL::OpenCL ${CMAKE_DL_LIBS})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(dlprim_python_host IMPORTED)
set_target_properties(dlprim_python_host PROPERTIES IMPORTED_LOCATION "${CMAKE_COMMAND}")

add_lv_opencl_integration_test(dlprim_python_host
  ID smoke.pytorch_dlprim CONFIG large_sm
  ARGS -E env "LD_PRELOAD=$<TARGET_FILE:dlprim_monitor>"
    $<TARGET_FILE:Python3::Interpreter> ${CMAKE_CURRENT_SOURCE_DIR}/smoke.py)
set(test_name opencl.smoke.pytorch_dlprim.simtix.atomic_sm)
set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
set_property(TEST ${test_name} APPEND PROPERTY
  ENVIRONMENT "POCL_EXTRA_BUILD_FLAGS=-cl-std=CL1.2 -DCUSTOM_REDUCE=1"
  "DLPRIM_CACHE_DIR=${TESTING_OUT}/${test_name}/dlprim-cache")
