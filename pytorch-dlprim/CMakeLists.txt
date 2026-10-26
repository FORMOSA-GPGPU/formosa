# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.24)
project(formosa_dlprim LANGUAGES CXX)

find_package(Torch REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(PYTHON "${Python3_EXECUTABLE}")
set(BUILD_CORE_ONLY ON CACHE BOOL "" FORCE)
set(DLPRIM_STATIC_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory("${PYTORCH_DLPRIM_SOURCE_DIR}/dlprimitives" dlprimitives)

# Build upstream sources and Formosa's extra ATen operators into a single module.
set(upstream_sources
  pointwise_ops.cpp tensor_ops.cpp vision_ops.cpp loss_ops.cpp random_ops.cpp
  norm_ops.cpp utils.cpp registeration.cpp CLTensor.cpp guard.cpp
  python_iface.cpp)
list(TRANSFORM upstream_sources PREPEND "${PYTORCH_DLPRIM_SOURCE_DIR}/src/")
pybind11_add_module(pt_ocl ${upstream_sources} ops.cc)
target_compile_features(pt_ocl PRIVATE cxx_std_17)
target_include_directories(pt_ocl SYSTEM PRIVATE
  ${TORCH_INCLUDE_DIRS} "${PYTORCH_DLPRIM_SOURCE_DIR}/src"
  "${PYTORCH_DLPRIM_SOURCE_DIR}/dlprimitives/include"
  "${CMAKE_CURRENT_BINARY_DIR}/dlprimitives" "${OCL_PATH}")
target_link_libraries(pt_ocl PRIVATE ${TORCH_LIBRARIES} ${OCL_LIB} dlprim_core)

install(TARGETS pt_ocl LIBRARY DESTINATION python/pytorch_ocl)
install(FILES "${PYTORCH_DLPRIM_SOURCE_DIR}/python/pytorch_ocl/__init__.py"
  DESTINATION python/pytorch_ocl)
