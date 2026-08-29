#include <CL/opencl.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

struct Matrix {
  int rows, cols, nnz;
  std::vector<int> row_ptr;
  std::vector<int> col_idx;
  std::vector<float> values;
  std::string name;
};

struct Triplet {
  int row, col;
  float val;

  bool operator<(const Triplet &other) const {
    if (row != other.row) {
      return row < other.row;
    }
    return col < other.col;
  }
};

/* Simple Matrix Market parser */
bool load_mtx(const std::string &filename, Matrix &mat) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return false;
  }

  std::string line;

  /* Read header */
  if (!std::getline(file, line) ||
      line.find("%%MatrixMarket matrix coordinate") == std::string::npos) {
    return false;
  }

  bool symmetric = (line.find("symmetric") != std::string::npos);

  /* Skip comments */
  while (std::getline(file, line) && !line.empty() && line[0] == '%') {
  }

  int rows, cols, nnz_in;
  std::stringstream ss(line);
  if (!(ss >> rows >> cols >> nnz_in)) {
    return false;
  }

  std::vector<Triplet> triplets;
  triplets.reserve(symmetric ? nnz_in * 2 : nnz_in);

  int r, c;
  float v;
  while (file >> r >> c >> v) {
    triplets.push_back({r - 1, c - 1, v}); /* 1-based to 0-based */
    if (symmetric && r != c) {
      triplets.push_back({c - 1, r - 1, v});
    }
  }

  std::sort(triplets.begin(), triplets.end());

  mat.rows = rows;
  mat.cols = cols;
  mat.row_ptr.assign(rows + 1, 0);
  mat.col_idx.clear();
  mat.values.clear();

  for (const auto &t : triplets) {
    if (t.row >= 0 && t.row < rows && t.col >= 0 && t.col < cols) {
      mat.row_ptr[t.row + 1]++;
      mat.col_idx.push_back(t.col);
      mat.values.push_back(t.val);
    }
  }

  for (int i = 0; i < rows; ++i) {
    mat.row_ptr[i + 1] += mat.row_ptr[i];
  }

  mat.nnz = static_cast<int>(mat.values.size());
  mat.name = filename.substr(filename.find_last_of("/\\") + 1);
  return true;
}

void print_stats(const Matrix &mat, double avg_time_ms, int iters,
                 int run_rows) {
  run_rows = std::max(0, std::min(run_rows, mat.rows));

  std::vector<int> row_lengths(run_rows);
  int run_nnz = 0;

  for (int i = 0; i < run_rows; ++i) {
    row_lengths[i] = mat.row_ptr[i + 1] - mat.row_ptr[i];
    run_nnz += row_lengths[i];
  }

  double avg_row_nnz =
      run_rows > 0 ? static_cast<double>(run_nnz) / run_rows : 0.0;

  int min_row_nnz = 0;
  int median_row_nnz = 0;
  int p90_row_nnz = 0;
  int p99_row_nnz = 0;
  int max_row_nnz = 0;
  if (run_rows > 0) {
    std::vector<int> sorted_lengths = row_lengths;
    std::sort(sorted_lengths.begin(), sorted_lengths.end());
    auto percentile = [&](int pct) {
      size_t index =
          (static_cast<size_t>(pct) * (sorted_lengths.size() - 1) + 50) / 100;
      return sorted_lengths[index];
    };
    min_row_nnz = sorted_lengths.front();
    median_row_nnz = percentile(50);
    p90_row_nnz = percentile(90);
    p99_row_nnz = percentile(99);
    max_row_nnz = sorted_lengths.back();
  }

  double sum_sq_diff = 0.0;
  for (int len : row_lengths) {
    double diff = len - avg_row_nnz;
    sum_sq_diff += diff * diff;
  }

  double stddev = run_rows > 0 ? std::sqrt(sum_sq_diff / run_rows) : 0.0;
  double cv = avg_row_nnz > 0 ? stddev / avg_row_nnz : 0.0;
  double gflops =
      avg_time_ms > 0 ? (2.0 * run_nnz) / (avg_time_ms * 1e-3) / 1e9 : 0.0;

  std::cout << "\n----------------------------------------\n";
  std::cout << "Matrix:           " << mat.name << "\n";
  std::cout << "Size:             " << mat.rows << " rows, " << mat.cols
            << " cols, " << mat.nnz << " nnz\n";
  std::cout << "Run:              " << run_rows << " rows, " << run_nnz
            << " nnz, " << iters << " iters\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Row nnz:          min=" << min_row_nnz
            << ", avg=" << avg_row_nnz << ", median=" << median_row_nnz
            << ", p90=" << p90_row_nnz << ", p99=" << p99_row_nnz
            << ", max=" << max_row_nnz << ", stddev=" << stddev << ", cv=" << cv
            << "\n";
  std::cout << std::setprecision(4);
  std::cout << "Avg Kernel Time:  " << avg_time_ms << " ms\n";
  std::cout << "Effective GFLOPS: " << gflops << "\n";
  std::cout << "----------------------------------------\n";
  std::cout << "[SpMV] CSV"
            << ",mode=csr"
            << ",rows=" << run_rows << ",cols=" << mat.cols
            << ",nnz=" << run_nnz << ",total_nnz=" << mat.nnz
            << ",iterations=" << iters << ",row_nnz_min=" << min_row_nnz
            << ",row_nnz_avg=" << avg_row_nnz
            << ",row_nnz_median=" << median_row_nnz
            << ",row_nnz_p90=" << p90_row_nnz << ",row_nnz_p99=" << p99_row_nnz
            << ",row_nnz_max=" << max_row_nnz << ",row_nnz_stddev=" << stddev
            << ",row_nnz_cv=" << cv << ",avg_time_ms=" << avg_time_ms << "\n";
}

std::vector<float> compute_reference(const Matrix &mat,
                                     const std::vector<float> &x,
                                     int run_rows) {
  std::vector<float> ref(run_rows, 0.0f);
  for (int row = 0; row < run_rows; ++row) {
    float sum = 0.0f;
    for (int jj = mat.row_ptr[row]; jj < mat.row_ptr[row + 1]; ++jj) {
      sum += mat.values[jj] * x[mat.col_idx[jj]];
    }
    ref[row] = sum;
  }
  return ref;
}

bool nearly_equal(float actual, float expected) {
  float tolerance = 1e-3f * std::max(1.0f, std::fabs(expected));
  return std::fabs(actual - expected) <= tolerance;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " <matrix.mtx> [run_rows] [iterations]"
              << std::endl;
    return 0;
  }

  std::string mtx_path = argv[1];
  int requested_rows = (argc > 2) ? std::atoi(argv[2]) : -1;
  int iters = (argc > 3) ? std::atoi(argv[3]) : 1;
  if (iters <= 0) {
    iters = 1;
  }

  std::cout << "[SpMV] Matrix Path : " << mtx_path << std::endl;

  Matrix mat;
  if (!load_mtx(mtx_path, mat)) {
    std::cerr << "Failed to load matrix: " << mtx_path << std::endl;
    return -1;
  }

  int run_rows = mat.rows;
  if (requested_rows > 0) {
    run_rows = std::min(requested_rows, mat.rows);
  }

  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) {
    std::cerr << "No OpenCL platforms found." << std::endl;
    return -1;
  }

  std::vector<cl::Device> devices;
  platforms[0].getDevices(CL_DEVICE_TYPE_ALL, &devices);
  if (devices.empty()) {
    std::cerr << "No OpenCL devices found." << std::endl;
    return -1;
  }

  cl::Device device = devices[0];
  std::cout << "Device      : " << device.getInfo<CL_DEVICE_NAME>()
            << std::endl;

  cl::Context context(device);

  std::string kernel_path = std::string(KERNEL_PATH);
  std::cout << "Kernel Path : " << kernel_path << std::endl;

  std::ifstream kfile(kernel_path);
  if (!kfile.is_open()) {
    std::cerr << "Failed to open kernel file: " << kernel_path << std::endl;
    return -1;
  }

  std::string src((std::istreambuf_iterator<char>(kfile)),
                  std::istreambuf_iterator<char>());

  cl::Program program(context, src);
  if (program.build("-I .") != CL_SUCCESS) {
    std::cerr << "Build log: "
              << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)
              << std::endl;
    return -1;
  }

  cl::CommandQueue queue(context, device);

  std::vector<float> x(mat.cols, 1.0f);
  std::vector<float> y(mat.rows, 0.0f);
  std::vector<float> ref = compute_reference(mat, x, run_rows);

  cl::Buffer b_row_ptr(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(int) * mat.row_ptr.size(), mat.row_ptr.data());
  cl::Buffer b_col_idx(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(int) * mat.col_idx.size(), mat.col_idx.data());
  cl::Buffer b_values(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                      sizeof(float) * mat.values.size(), mat.values.data());
  cl::Buffer b_x(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                 sizeof(float) * x.size(), x.data());
  cl::Buffer b_y(context, CL_MEM_READ_WRITE, sizeof(float) * y.size());

  cl::Kernel kernel(program, "spmv_csr");

  kernel.setArg(0, b_row_ptr);
  kernel.setArg(1, b_col_idx);
  kernel.setArg(2, b_values);
  kernel.setArg(3, b_x);
  kernel.setArg(4, b_y);
  kernel.setArg(5, run_rows);

  std::cout << "[SpMV] Running benchmark (" << iters << " iterations)..."
            << std::endl;

  /* Warmup */
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(run_rows),
                             cl::NullRange);
  queue.finish();

  /* Benchmark */
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iters; ++i) {
    queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(run_rows),
                               cl::NullRange);
  }
  queue.finish();
  auto end = std::chrono::high_resolution_clock::now();

  double total_time_ms =
      std::chrono::duration<double, std::milli>(end - start).count();

  queue.enqueueReadBuffer(b_y, CL_TRUE, 0, sizeof(float) * y.size(), y.data());

  for (int row = 0; row < run_rows; ++row) {
    if (!nearly_equal(y[row], ref[row])) {
      std::cerr << "[SpMV] FAILED row=" << row << " expected=" << ref[row]
                << " actual=" << y[row] << std::endl;
      return 1;
    }
  }

  print_stats(mat, total_time_ms / iters, iters, run_rows);
  int run_nnz = mat.row_ptr[run_rows] - mat.row_ptr[0];
  std::cout << "[SpMV] PASSED matrix=" << mtx_path << " rows=" << run_rows
            << " nnz=" << run_nnz << std::endl;

  return 0;
}
