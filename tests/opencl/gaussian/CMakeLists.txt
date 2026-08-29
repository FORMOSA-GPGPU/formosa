add_executable(gaussian
  main.cc
  clutils.cpp
  utils.cpp)
target_link_libraries(gaussian PRIVATE OpenCL::OpenCL)
target_compile_definitions(gaussian PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")

add_lv_opencl_integration_test(gaussian SUFFIX 4 ARGS -q -f ${CMAKE_CURRENT_SOURCE_DIR}/matrix4.txt)
add_lv_opencl_integration_test(gaussian SUFFIX 25 ARGS -q -f ${CMAKE_CURRENT_SOURCE_DIR}/matrix25.txt)
add_lv_opencl_integration_test(gaussian SUFFIX 33 ARGS -q -f ${CMAKE_CURRENT_SOURCE_DIR}/matrix33.txt)
