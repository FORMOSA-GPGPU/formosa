add_executable(transpose main.cc)
target_link_libraries(transpose PRIVATE OpenCL::OpenCL)
target_compile_definitions(transpose PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(transpose)
