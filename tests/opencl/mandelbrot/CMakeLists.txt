add_executable(mandelbrot main.cc)
target_link_libraries(mandelbrot PRIVATE OpenCL::OpenCL)
target_compile_definitions(mandelbrot PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(mandelbrot ARGS 256)
