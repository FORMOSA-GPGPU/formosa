add_executable(nn
  main.cc
  clutils.cpp
  utils.cpp)
target_link_libraries(nn PRIVATE OpenCL::OpenCL)
target_compile_definitions(nn PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(nn ARGS ${CMAKE_CURRENT_SOURCE_DIR}/filelist.txt)
