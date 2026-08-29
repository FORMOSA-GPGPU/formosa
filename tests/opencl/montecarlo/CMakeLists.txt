add_executable(montecarlo main.cc)
target_link_libraries(montecarlo PRIVATE OpenCL::OpenCL)
target_compile_definitions(montecarlo PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(montecarlo ARGS 1024)
