add_executable(bfs main.cc)
target_link_libraries(bfs PRIVATE OpenCL::OpenCL)
target_compile_definitions(bfs PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(bfs SUFFIX 4k ARGS ${CMAKE_CURRENT_SOURCE_DIR}/graph4k.txt)
add_lv_opencl_integration_test(bfs SUFFIX 32 ARGS ${CMAKE_CURRENT_SOURCE_DIR}/graph32.txt)
