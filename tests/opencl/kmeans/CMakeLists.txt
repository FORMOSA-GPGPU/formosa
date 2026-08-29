add_executable(kmeans
  main.cc
  cluster.c
  kmeans_clustering.c
  read_input.c
  rmse.c)
target_link_libraries(kmeans PRIVATE OpenCL::OpenCL)
target_compile_definitions(kmeans PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")
add_lv_opencl_integration_test(kmeans ARGS -f100 -p100)
