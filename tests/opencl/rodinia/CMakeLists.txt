# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

include("${CMAKE_CURRENT_LIST_DIR}/helpers.cmake")

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${UPSTREAM}/data/myocyte/y.txt"
  "${UPSTREAM}/data/myocyte/params.txt"
  "${CMAKE_CURRENT_LIST_DIR}/generate_inputs.py")
set(INPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/inputs")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_inputs.py" "${INPUT_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)

add_subdirectory(bfs)
add_subdirectory(gaussian)
add_subdirectory(kmeans)
add_subdirectory(nn)

add_rodinia(rodinia_hotspot
  "${UPSTREAM}/opencl/hotspot/hotspot.c"
  "${UPSTREAM}/opencl/hotspot/OpenCL_helper_library.c")
add_rodinia(rodinia_hotspot3d
  "${UPSTREAM}/opencl/hotspot3D/3D.c"
  "${UPSTREAM}/opencl/hotspot3D/CL_helper.c")
add_rodinia(rodinia_lud
  "${UPSTREAM}/opencl/lud/ocl/lud.cpp"
  "${UPSTREAM}/opencl/lud/common/common.c")
target_include_directories(rodinia_lud PRIVATE "${UPSTREAM}/opencl/lud/common")
add_test(NAME opencl.rodinia.validation
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_test.py"
    "${CMAKE_C_COMPILER}" "${UPSTREAM}/opencl/lud/common")
set_tests_properties(opencl.rodinia.validation PROPERTIES LABELS "integration;opencl;rodinia" TIMEOUT 30)
add_rodinia(rodinia_nw "${UPSTREAM}/opencl/nw/nw.c")
# The upstream Makefile builds nw.c with g++.
set_source_files_properties("${UPSTREAM}/opencl/nw/nw.c" PROPERTIES LANGUAGE CXX)
target_compile_definitions(rodinia_nw PRIVATE TRACEBACK)
# The C hosts need C linkage for timing.c; the Pathfinder Makefile uses g++.
# A verbatim build-tree copy lets each target retain its original linkage.
configure_file("${UPSTREAM}/opencl/util/timing.c"
  "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp" COPYONLY)
add_rodinia(rodinia_pathfinder
  "${UPSTREAM}/opencl/pathfinder/main.cpp"
  "${UPSTREAM}/opencl/pathfinder/OpenCL.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
target_compile_definitions(rodinia_pathfinder PRIVATE BENCH_PRINT TIMING)
add_rodinia(rodinia_streamcluster
  "${UPSTREAM}/opencl/streamcluster/streamcluster.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
target_compile_definitions(rodinia_streamcluster PRIVATE TIMING)
add_executable(rodinia_streamcluster_reference
  "${UPSTREAM}/openmp/streamcluster/streamcluster_omp.cpp")
target_link_libraries(rodinia_streamcluster_reference PRIVATE OpenMP::OpenMP_CXX m)
add_dependencies(rodinia_streamcluster rodinia_streamcluster_reference)

set(BACKPROP_SOURCES "${UPSTREAM}/opencl/backprop/backprop.c"
  "${UPSTREAM}/opencl/backprop/imagenet.c" "${UPSTREAM}/opencl/backprop/facetrain.c")
set_source_files_properties(${BACKPROP_SOURCES} PROPERTIES LANGUAGE CXX)
add_rodinia(rodinia_backprop "${UPSTREAM}/opencl/backprop/backprop_ocl.cpp"
  ${BACKPROP_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
target_link_libraries(rodinia_backprop PRIVATE OpenMP::OpenMP_CXX)
target_compile_definitions(rodinia_backprop PRIVATE TIMING)
add_executable(rodinia_backprop_reference backprop_reference.cpp
  "${UPSTREAM}/opencl/backprop/backprop.c" "${UPSTREAM}/opencl/backprop/imagenet.c")
target_include_directories(rodinia_backprop_reference PRIVATE "${UPSTREAM}/opencl/backprop")
target_link_libraries(rodinia_backprop_reference PRIVATE OpenMP::OpenMP_CXX m)
add_dependencies(rodinia_backprop rodinia_backprop_reference rodinia_readback)
set_target_properties(rodinia_backprop rodinia_backprop_reference PROPERTIES EXCLUDE_FROM_ALL YES)
configure_file("${UPSTREAM}/opencl/backprop/backprop_kernel.cl"
  "${CMAKE_CURRENT_BINARY_DIR}/backprop/backprop_kernel.cl" COPYONLY)
# This target is diagnostic: upstream's first reduction doubles the dot product.
# It is intentionally not registered as a numerically passing benchmark.

add_rodinia(rodinia_particlefilter
  "${UPSTREAM}/opencl/particlefilter/ex_particle_OCL_single_seq.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
add_rodinia(rodinia_dwt2d "${UPSTREAM}/opencl/dwt2d/main.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
target_compile_definitions(rodinia_particlefilter PRIVATE TIMING)
target_compile_definitions(rodinia_dwt2d PRIVATE TIMING OUTPUT)

foreach(PAIR "btree;b+tree" "srad;srad" "myocyte;myocyte" "lavamd;lavaMD")
  list(GET PAIR 0 NAME)
  list(GET PAIR 1 DIRECTORY)
  file(GLOB SOURCES "${UPSTREAM}/opencl/${DIRECTORY}/main.c"
    "${UPSTREAM}/opencl/${DIRECTORY}/kernel/*wrapper*.c"
    "${UPSTREAM}/opencl/${DIRECTORY}/util/*/*.c")
  add_rodinia(rodinia_${NAME} ${SOURCES} "${UPSTREAM}/opencl/util/timing.c")
  set_target_properties(rodinia_${NAME} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
  target_compile_definitions(rodinia_${NAME} PRIVATE TIMING)
endforeach()
set_target_properties(rodinia_myocyte PROPERTIES EXCLUDE_FROM_ALL YES)
target_compile_definitions(rodinia_lavamd PRIVATE OUTPUT)
# Upstream b+tree's main declaration omits the return type (pre-C99 syntax).
target_compile_options(rodinia_btree PRIVATE -Wno-implicit-int)
file(GLOB BTREE_REFERENCE_SOURCES "${UPSTREAM}/openmp/b+tree/main.c"
  "${UPSTREAM}/openmp/b+tree/kernel/*.c" "${UPSTREAM}/openmp/b+tree/util/*/*.c")
add_executable(rodinia_btree_reference ${BTREE_REFERENCE_SOURCES})
# Its CPU sources omit stdio/time declarations, and the unused file-error
# diagnostics reverse fputs arguments. Retain legacy warning-only compilation.
target_compile_options(rodinia_btree_reference PRIVATE -Wno-implicit-int
  -Wno-error=incompatible-pointer-types "SHELL:-include stdio.h" "SHELL:-include sys/time.h")
target_link_libraries(rodinia_btree_reference PRIVATE OpenMP::OpenMP_C m)
add_dependencies(rodinia_btree rodinia_btree_reference)
add_executable(rodinia_srad_reference "${UPSTREAM}/openmp/srad/srad_v1/main.c")
target_link_libraries(rodinia_srad_reference PRIVATE OpenMP::OpenMP_C m)
add_dependencies(rodinia_srad rodinia_srad_reference)

# Preserve the original CPU implementations as independent reference programs.
foreach(NAME nw pathfinder)
  if(NAME STREQUAL "nw")
    set(SOURCE needle.cpp)
  else()
    set(SOURCE pathfinder.cpp)
  endif()
  add_executable(rodinia_${NAME}_reference "${UPSTREAM}/openmp/${NAME}/${SOURCE}")
  target_link_libraries(rodinia_${NAME}_reference PRIVATE OpenMP::OpenMP_CXX m)
  add_dependencies(rodinia_${NAME} rodinia_${NAME}_reference)
endforeach()

add_rodinia_test(hotspot hotspot CONFIG large_sm OUTPUT result.txt
  ARGS 16 1 2 "${INPUT_DIR}/temperature16.txt" "${INPUT_DIR}/power16.txt" result.txt
  REFERENCE_ARGS 16 16 2 1 "${INPUT_DIR}/temperature16.txt" "${INPUT_DIR}/power16.txt" result.txt)
add_rodinia_test(hotspot3d hotspot3D CONFIG large_sm OUTPUT result.txt
  ARGS -n 64 -l 2 -i 2 -f "${INPUT_DIR}/power64x64x2.txt" "${INPUT_DIR}/temperature64x64x2.txt" result.txt)
add_rodinia_test(lud lud CONFIG large_sm ARGS -i "${INPUT_DIR}/matrix32.txt" -v)
add_rodinia_test(nw nw OUTPUT result.txt
  ARGS 32 10 nw.cl
  REFERENCE rodinia_nw_reference REFERENCE_ARGS 32 10 1)
add_rodinia_test(pathfinder pathfinder
  ARGS -c 64 -r 8 -h 2
  REFERENCE rodinia_pathfinder_reference REFERENCE_ARGS 64 8)
add_rodinia_test(btree b+tree CONFIG large_sm OUTPUT output.txt
  ARGS file "${INPUT_DIR}/btree.txt" command "${INPUT_DIR}/btree-commands.txt"
  REFERENCE rodinia_btree_reference
  REFERENCE_ARGS file "${INPUT_DIR}/btree.txt" command "${INPUT_DIR}/btree-commands.txt" cores 1)
# SRAD's original host reads ../../data/srad/image.pgm regardless of arguments.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../data/srad"
  "${CMAKE_CURRENT_BINARY_DIR}/srad/output")
configure_file("${INPUT_DIR}/srad.pgm"
  "${CMAKE_CURRENT_BINARY_DIR}/../data/srad/image.pgm" COPYONLY)
add_rodinia_test(srad srad CONFIG large_sm OUTPUT output/image_out.pgm
  ARGS -n 2 -l 1 -s 16 16 -p 0
  REFERENCE rodinia_srad_reference
  REFERENCE_ARGS 2 1 16 16 1 "${INPUT_DIR}/srad.pgm")

add_rodinia_test(particlefilter particlefilter CONFIG large_sm READBACK OUTPUT output.txt
  ARGS -x 16 -y 16 -z 3 -np 256)
# Myocyte finishes quickly, but repeated CPU OpenCL runs do not agree numerically.
# Keep an original-source diagnostic target, without declaring a passing test.
stage_rodinia_files(myocyte myocyte)
foreach(INPUT y.txt params.txt)
  configure_file("${INPUT_DIR}/myocyte/${INPUT}"
    "${CMAKE_CURRENT_BINARY_DIR}/myocyte/${INPUT}" COPYONLY)
endforeach()
add_rodinia_test(streamcluster streamcluster CONFIG large_sm OUTPUT centers.txt
  ARGS 2 3 2 256 256 4 none centers.txt 1 -p 0
  REFERENCE rodinia_streamcluster_reference
  REFERENCE_ARGS 2 3 2 256 256 4 none centers.txt 1)
add_rodinia_test(lavamd lavaMD CONFIG large_sm READBACK OUTPUT result.txt ARGS -boxes1d 1 -p 0)
foreach(CASE explicit default)
  set(OUTPUT "result.dwt")
  set(OUTPUT_ARG "${OUTPUT}")
  if(CASE STREQUAL "default")
    set(OUTPUT "input.rgb.dwt")
    set(OUTPUT_ARG "")
  endif()
  add_rodinia_test(dwt2d.${CASE} dwt2d PROGRAM rodinia_dwt2d
    ID rodinia.dwt2d.${CASE} KIND dwt2d CONFIG large_sm READBACK OUTPUT "${OUTPUT}"
    ARGS -D 32x32 -l 1 -5 input.rgb ${OUTPUT_ARG})
  configure_file("${INPUT_DIR}/input.rgb"
    "${CMAKE_CURRENT_BINARY_DIR}/dwt2d.${CASE}/input.rgb" COPYONLY)
endforeach()
