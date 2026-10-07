add_executable(vecadd main.cc)
target_link_libraries(vecadd PRIVATE OpenCL::OpenCL)
target_compile_definitions(vecadd PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(vecadd)
