// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define BATCH 4
#define INPUT_DIM 8
#define HIDDEN_DIM 8
#define FP_ERROR 0.001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void linear_cpu(const std::vector<float> &input,
                       const std::vector<float> &weights,
                       const std::vector<float> &bias,
                       std::vector<float> &output, int rows, int input_cols,
                       int output_cols) {
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < output_cols; ++col) {
      float sum = bias[col];
      for (int i = 0; i < input_cols; ++i) {
        sum += input[row * input_cols + i] * weights[i * output_cols + col];
      }
      output[row * output_cols + col] = sum;
    }
  }
}

static void sigmoid_cpu(const std::vector<float> &input,
                        std::vector<float> &output) {
  for (size_t i = 0; i < input.size(); ++i)
    output[i] = 1.0f / (1.0f + std::exp(-input[i]));
}

static void tanh_cpu(const std::vector<float> &input,
                     std::vector<float> &output) {
  for (size_t i = 0; i < input.size(); ++i) output[i] = std::tanh(input[i]);
}

static void mul_cpu(const std::vector<float> &lhs,
                    const std::vector<float> &rhs, std::vector<float> &output) {
  for (size_t i = 0; i < output.size(); ++i) output[i] = lhs[i] * rhs[i];
}

static void gru_update_cpu(const std::vector<float> &update_gate,
                           const std::vector<float> &candidate,
                           const std::vector<float> &hidden_prev,
                           std::vector<float> &hidden_next) {
  for (size_t i = 0; i < hidden_next.size(); ++i) {
    float z = update_gate[i];
    hidden_next[i] = (1.0f - z) * candidate[i] + z * hidden_prev[i];
  }
}

static bool compare_vector(const std::vector<float> &got,
                           const std::vector<float> &expected,
                           const char *name) {
  for (size_t i = 0; i < got.size(); ++i) {
    if (std::fabs(got[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << name << " mismatch at " << i << ": got " << got[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return false;
    }
  }
  std::cout << GREEN << name << " passed" << RESET << std::endl;
  return true;
}

static std::vector<float> run_linear(
    cl::Context &context, cl::CommandQueue &queue, cl::Program &program,
    const std::vector<float> &input, const std::vector<float> &weights,
    const std::vector<float> &bias, int rows, int input_cols, int output_cols) {
  std::vector<float> output(rows * output_cols, 0.0f);
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_weights(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         sizeof(float) * weights.size(),
                         const_cast<float *>(weights.data()));
  cl::Buffer buf_bias(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                      sizeof(float) * bias.size(),
                      const_cast<float *>(bias.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "project_matrix");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_weights);
  kernel.setArg(2, buf_bias);
  kernel.setArg(3, buf_output);
  kernel.setArg(4, rows);
  kernel.setArg(5, input_cols);
  kernel.setArg(6, output_cols);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_unary(cl::Context &context,
                                    cl::CommandQueue &queue,
                                    cl::Program &program,
                                    const std::vector<float> &input,
                                    const char *kernel_name) {
  std::vector<float> output(input.size(), 0.0f);
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, kernel_name);
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_output);
  kernel.setArg(2, static_cast<int>(input.size()));
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_mul(cl::Context &context, cl::CommandQueue &queue,
                                  cl::Program &program,
                                  const std::vector<float> &lhs,
                                  const std::vector<float> &rhs) {
  std::vector<float> output(lhs.size(), 0.0f);
  cl::Buffer buf_lhs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     sizeof(float) * lhs.size(),
                     const_cast<float *>(lhs.data()));
  cl::Buffer buf_rhs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     sizeof(float) * rhs.size(),
                     const_cast<float *>(rhs.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "combine_reset_candidate");
  kernel.setArg(0, buf_lhs);
  kernel.setArg(1, buf_rhs);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, static_cast<int>(lhs.size()));
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_hidden_update(
    cl::Context &context, cl::CommandQueue &queue, cl::Program &program,
    const std::vector<float> &update_gate, const std::vector<float> &candidate,
    const std::vector<float> &hidden_prev) {
  std::vector<float> output(update_gate.size(), 0.0f);
  cl::Buffer buf_z(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * update_gate.size(),
                   const_cast<float *>(update_gate.data()));
  cl::Buffer buf_c(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * candidate.size(),
                   const_cast<float *>(candidate.data()));
  cl::Buffer buf_h(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * hidden_prev.size(),
                   const_cast<float *>(hidden_prev.data()));
  cl::Buffer buf_out(context, CL_MEM_WRITE_ONLY, sizeof(float) * output.size());
  cl::Kernel kernel(program, "gru_hidden_update");
  kernel.setArg(0, buf_z);
  kernel.setArg(1, buf_c);
  kernel.setArg(2, buf_h);
  kernel.setArg(3, buf_out);
  kernel.setArg(4, static_cast<int>(output.size()));
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_out, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

int main() {
  srand(10);
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) return -1;
  std::vector<cl::Device> devices;
  platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) return -1;
  cl::Context context({devices[0]});
  cl::CommandQueue queue(context, devices[0]);
  std::string kernel_path = std::string(KERNEL_PATH);
  std::cout << "Kernel Path : " << kernel_path << std::endl;
  std::ifstream t(kernel_path);
  if (!t) {
    std::cerr << "Error Opening Kernel Source file\n";
    return -1;
  }
  std::string source = {std::istreambuf_iterator<char>(t),
                        std::istreambuf_iterator<char>()};
  cl::Program program(context, source);
  if (program.build() != CL_SUCCESS) return -1;

  std::vector<float> input(BATCH * INPUT_DIM);
  std::vector<float> hidden_prev(BATCH * HIDDEN_DIM);
  std::vector<float> wzx(INPUT_DIM * HIDDEN_DIM);
  std::vector<float> bzx(HIDDEN_DIM);
  std::vector<float> wzh(HIDDEN_DIM * HIDDEN_DIM);
  std::vector<float> bzh(HIDDEN_DIM);
  std::vector<float> wrx(INPUT_DIM * HIDDEN_DIM);
  std::vector<float> brx(HIDDEN_DIM);
  std::vector<float> wrh(HIDDEN_DIM * HIDDEN_DIM);
  std::vector<float> brh(HIDDEN_DIM);
  std::vector<float> wnx(INPUT_DIM * HIDDEN_DIM);
  std::vector<float> bnx(HIDDEN_DIM);
  std::vector<float> wnh(HIDDEN_DIM * HIDDEN_DIM);
  std::vector<float> bnh(HIDDEN_DIM);

  auto fill = [](std::vector<float> &vec, float scale) {
    for (float &x : vec) x = static_cast<float>((rand() % 200) - 100) / scale;
  };
  fill(input, 20.0f);
  fill(hidden_prev, 20.0f);
  fill(wzx, 100.0f);
  fill(bzx, 100.0f);
  fill(wzh, 100.0f);
  fill(bzh, 100.0f);
  fill(wrx, 100.0f);
  fill(brx, 100.0f);
  fill(wrh, 100.0f);
  fill(brh, 100.0f);
  fill(wnx, 100.0f);
  fill(bnx, 100.0f);
  fill(wnh, 100.0f);
  fill(bnh, 100.0f);

  std::vector<float> z_x_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> z_h_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> z_pre_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> z_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> r_x_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> r_h_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> r_pre_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> r_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> n_x_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> n_h_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> rn_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> n_pre_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> n_ref(BATCH * HIDDEN_DIM, 0.0f);
  std::vector<float> h_next_ref(BATCH * HIDDEN_DIM, 0.0f);

  linear_cpu(input, wzx, bzx, z_x_ref, BATCH, INPUT_DIM, HIDDEN_DIM);
  linear_cpu(hidden_prev, wzh, bzh, z_h_ref, BATCH, HIDDEN_DIM, HIDDEN_DIM);
  for (size_t i = 0; i < z_pre_ref.size(); ++i)
    z_pre_ref[i] = z_x_ref[i] + z_h_ref[i];
  sigmoid_cpu(z_pre_ref, z_ref);

  linear_cpu(input, wrx, brx, r_x_ref, BATCH, INPUT_DIM, HIDDEN_DIM);
  linear_cpu(hidden_prev, wrh, brh, r_h_ref, BATCH, HIDDEN_DIM, HIDDEN_DIM);
  for (size_t i = 0; i < r_pre_ref.size(); ++i)
    r_pre_ref[i] = r_x_ref[i] + r_h_ref[i];
  sigmoid_cpu(r_pre_ref, r_ref);

  linear_cpu(input, wnx, bnx, n_x_ref, BATCH, INPUT_DIM, HIDDEN_DIM);
  linear_cpu(hidden_prev, wnh, bnh, n_h_ref, BATCH, HIDDEN_DIM, HIDDEN_DIM);
  mul_cpu(r_ref, n_h_ref, rn_ref);
  for (size_t i = 0; i < n_pre_ref.size(); ++i)
    n_pre_ref[i] = n_x_ref[i] + rn_ref[i];
  tanh_cpu(n_pre_ref, n_ref);
  gru_update_cpu(z_ref, n_ref, hidden_prev, h_next_ref);

  std::vector<float> z_x_out = run_linear(context, queue, program, input, wzx,
                                          bzx, BATCH, INPUT_DIM, HIDDEN_DIM);
  std::vector<float> z_h_out =
      run_linear(context, queue, program, hidden_prev, wzh, bzh, BATCH,
                 HIDDEN_DIM, HIDDEN_DIM);
  std::vector<float> z_pre_out(z_x_out.size(), 0.0f);
  for (size_t i = 0; i < z_pre_out.size(); ++i)
    z_pre_out[i] = z_x_out[i] + z_h_out[i];
  std::vector<float> z_out =
      run_unary(context, queue, program, z_pre_out, "sigmoid_activation");

  std::vector<float> r_x_out = run_linear(context, queue, program, input, wrx,
                                          brx, BATCH, INPUT_DIM, HIDDEN_DIM);
  std::vector<float> r_h_out =
      run_linear(context, queue, program, hidden_prev, wrh, brh, BATCH,
                 HIDDEN_DIM, HIDDEN_DIM);
  std::vector<float> r_pre_out(r_x_out.size(), 0.0f);
  for (size_t i = 0; i < r_pre_out.size(); ++i)
    r_pre_out[i] = r_x_out[i] + r_h_out[i];
  std::vector<float> r_out =
      run_unary(context, queue, program, r_pre_out, "sigmoid_activation");

  std::vector<float> n_x_out = run_linear(context, queue, program, input, wnx,
                                          bnx, BATCH, INPUT_DIM, HIDDEN_DIM);
  std::vector<float> n_h_out =
      run_linear(context, queue, program, hidden_prev, wnh, bnh, BATCH,
                 HIDDEN_DIM, HIDDEN_DIM);
  std::vector<float> rn_out = run_mul(context, queue, program, r_out, n_h_out);
  std::vector<float> n_pre_out(n_x_out.size(), 0.0f);
  for (size_t i = 0; i < n_pre_out.size(); ++i)
    n_pre_out[i] = n_x_out[i] + rn_out[i];
  std::vector<float> n_out =
      run_unary(context, queue, program, n_pre_out, "tanh_activation");
  std::vector<float> h_next_out =
      run_hidden_update(context, queue, program, z_out, n_out, hidden_prev);

  bool passed = true;
  passed &= compare_vector(z_x_out, z_x_ref, "gru_update_input");
  passed &= compare_vector(z_h_out, z_h_ref, "gru_update_hidden");
  passed &= compare_vector(z_out, z_ref, "gru_update_gate");
  passed &= compare_vector(r_x_out, r_x_ref, "gru_reset_input");
  passed &= compare_vector(r_h_out, r_h_ref, "gru_reset_hidden");
  passed &= compare_vector(r_out, r_ref, "gru_reset_gate");
  passed &= compare_vector(n_x_out, n_x_ref, "gru_candidate_input");
  passed &= compare_vector(n_h_out, n_h_ref, "gru_candidate_hidden");
  passed &= compare_vector(rn_out, rn_ref, "gru_reset_applied");
  passed &= compare_vector(n_out, n_ref, "gru_candidate");
  passed &= compare_vector(h_next_out, h_next_ref, "gru_hidden_next");
  return passed ? 0 : -1;
}
