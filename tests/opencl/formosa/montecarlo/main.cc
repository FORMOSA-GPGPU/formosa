#include <CL/opencl.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <radius>" << std::endl;
    return -1;
  }

  int n = atoi(argv[1]);
  int n_points = n * n;

  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  cl::Platform platform = platforms[0];
  std::vector<cl::Device> devices;

  platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) {
    std::cerr << "No Devices!" << std::endl;
    return -1;
  }

  cl::Device device = devices[0];
  cl::Context context({device});
  cl::CommandQueue queue(context, device);

  std::ifstream t(KERNEL_PATH);
  if (!t) {
    std::cerr << "Error Opening Kernel Source file\n";
    return -1;
  }

  std::string source = {std::istreambuf_iterator<char>(t),
                        std::istreambuf_iterator<char>()};

  cl::Program::Sources sources = {source};

  cl::Program program(context, sources);
  if (program.build() != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return -1;
  }
  cl::Kernel montecarlo(program, "montecarlo");
  cl::Kernel all_one(program, "all_one");
  cl::Kernel all_zero(program, "all_zero");

  cl::Buffer result_buf(context, CL_MEM_WRITE_ONLY, sizeof(int) * n_points);
  std::vector<int> result(n_points);

  montecarlo.setArg(0, result_buf);

  queue.enqueueNDRangeKernel(montecarlo, cl::NullRange, cl::NDRange(n, n),
                             cl::NDRange(8, 8));
  queue.enqueueReadBuffer(result_buf, CL_FALSE, 0, sizeof(int) * n_points,
                          result.data());
  queue.finish();

  int in_arc = std::count(result.begin(), result.end(), 1);
  double ratio = (double)in_arc / n_points;
  std::cout << in_arc << " / " << n_points << " = " << ratio << std::endl;
  double pi = ratio * 4;
  std::cout << "Pi = " << pi << std::endl;
  if (pi > 3.15 || pi < 3.14) {
    std::cout << "Wrong!" << std::endl;
    return -1;
  }
  return 0;
}
