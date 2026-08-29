// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gemma_transformer_layer.h"
#include "model_loader.h"

namespace {

struct Args {
  std::string model_path = std::string(GEMMA_ASSET_ROOT) + "/gemma-3-270m.bin";
  std::string output_prefix = "single_layer";
  int token_id = 100;
  int layer_idx = 0;
  bool debug = false;
};

std::string read_kernel_source(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open kernel file: " + path);
  }
  std::ostringstream source;
  source << file.rdbuf();
  return source.str();
}

int parse_int(const std::string &value, const std::string &name) {
  try {
    size_t pos = 0;
    int result = std::stoi(value, &pos);
    if (pos != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return result;
  } catch (const std::exception &e) {
    throw std::runtime_error("Invalid " + name + ": " + value);
  }
}

Args parse_args(int argc, char *argv[]) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      args.model_path = argv[++i];
    } else if (arg == "--token" && i + 1 < argc) {
      args.token_id = parse_int(argv[++i], "--token");
    } else if (arg == "--layer" && i + 1 < argc) {
      args.layer_idx = parse_int(argv[++i], "--layer");
    } else if (arg == "--output-prefix" && i + 1 < argc) {
      args.output_prefix = argv[++i];
    } else if (arg == "--debug") {
      args.debug = true;
    } else if (arg == "--help") {
      std::cout << "Usage: verify_layer_e2e [--model PATH] [--token ID] "
                << "[--layer IDX] [--output-prefix PREFIX] [--debug]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown or incomplete argument: " + arg);
    }
  }
  return args;
}

cl::Buffer upload_weights(cl::Context &ctx, float *data, size_t count) {
  return cl::Buffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                    sizeof(float) * count, data);
}

void compute_token_embedding(const ModelLoader &model, int token,
                             std::vector<float> &hidden) {
  const int dim = model.config.dim;
  const float scale = std::sqrt(static_cast<float>(dim));
  const float *token_row =
      model.embed_tokens + static_cast<size_t>(token) * dim;
  hidden.resize(dim);
  for (int i = 0; i < dim; ++i) {
    hidden[i] = token_row[i] * scale;
  }
}

void dump_buffer(cl::CommandQueue &cq, const cl::Buffer &buf, int num_floats,
                 const std::string &path) {
  std::vector<float> host(num_floats);
  cq.enqueueReadBuffer(buf, CL_TRUE, 0, sizeof(float) * num_floats,
                       host.data());

  std::ofstream f(path);
  if (!f) {
    throw std::runtime_error("Failed to open dump file: " + path);
  }
  f << std::fixed
    << std::setprecision(std::numeric_limits<float>::max_digits10);
  for (float v : host) {
    f << v << "\n";
  }
}

size_t round_up(size_t value, size_t multiple) {
  return ((value + multiple - 1) / multiple) * multiple;
}

} /* namespace */

int main(int argc, char *argv[]) {
  try {
    const Args args = parse_args(argc, argv);

    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.empty()) {
      throw std::runtime_error("No OpenCL platforms available");
    }

    std::vector<cl::Device> devices;
    platforms[0].getDevices(CL_DEVICE_TYPE_ALL, &devices);
    if (devices.empty()) {
      throw std::runtime_error("No OpenCL devices available");
    }
    cl::Device device = devices[0];
    std::cout << "Device: " << device.getInfo<CL_DEVICE_NAME>() << "\n";

    cl::Context ctx(device);
    cl::CommandQueue cq(ctx, device);

    const std::string kernel_prefix = std::string(GEMMA_ROOT) + "/";
    const std::vector<std::string> kernel_files = {
        "rmsnorm.cl",         "qk_norm.cl", "gemm.cl",  "rope.cl",
        "kv_cache_update.cl", "gqa.cl",     "geglu.cl", "residual_add.cl",
    };

    cl::Program::Sources sources;
    for (const std::string &file : kernel_files) {
      sources.push_back(read_kernel_source(kernel_prefix + file));
    }

    cl::Program program(ctx, sources);
    try {
      program.build({device});
    } catch (cl::Error &) {
      std::cout << "Build error:\n"
                << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
      return 1;
    }

    ModelLoader model(args.model_path);
    if (args.layer_idx < 0 || args.layer_idx >= model.config.n_layers) {
      throw std::runtime_error("Layer index out of range");
    }
    if (args.token_id < 0 || args.token_id >= model.config.vocab_size) {
      throw std::runtime_error("Token id out of range");
    }

    const int dim = model.config.dim;
    const int max_seq_len = 512;
    LayerConfig base_cfg;
    base_cfg.hidden_dim = dim;
    base_cfg.num_heads = model.config.n_heads;
    base_cfg.num_kv_heads = model.config.n_kv_heads;
    base_cfg.head_dim = model.config.head_dim;
    base_cfg.mlp_hidden_dim = model.config.hidden_dim;
    base_cfg.max_seq_len = max_seq_len;
    base_cfg.epsilon = 1e-6f;
    base_cfg.attn_scale =
        1.0f / std::sqrt(static_cast<float>(base_cfg.head_dim));

    std::vector<GemmaTransformerLayer> transformer_layers;
    transformer_layers.reserve(args.layer_idx + 1);
    for (int i = 0; i <= args.layer_idx; ++i) {
      const LayerWeights &w = model.layers[i];
      LayerConfig cfg = base_cfg;
      cfg.theta_base = ((i % 6) == 5) ? 1000000.0f : 10000.0f;

      transformer_layers.emplace_back(
          ctx, cq, program, cfg,
          upload_weights(ctx, w.input_layernorm, w.input_layernorm_size),
          upload_weights(ctx, w.q_norm, w.q_norm_size),
          upload_weights(ctx, w.k_norm, w.k_norm_size),
          upload_weights(ctx, w.q_proj, w.q_proj_size),
          upload_weights(ctx, w.k_proj, w.k_proj_size),
          upload_weights(ctx, w.v_proj, w.v_proj_size),
          upload_weights(ctx, w.o_proj, w.o_proj_size),
          upload_weights(ctx, w.post_attn_layernorm,
                         w.post_attn_layernorm_size),
          upload_weights(ctx, w.pre_ff_layernorm, w.pre_ff_layernorm_size),
          upload_weights(ctx, w.post_ff_layernorm, w.post_ff_layernorm_size),
          upload_weights(ctx, w.gate_proj, w.gate_proj_size),
          upload_weights(ctx, w.up_proj, w.up_proj_size),
          upload_weights(ctx, w.down_proj, w.down_proj_size));
    }

    cl::Buffer d_hidden(ctx, CL_MEM_READ_WRITE, sizeof(float) * dim);
    std::vector<float> host_hidden;
    compute_token_embedding(model, args.token_id, host_hidden);
    cq.enqueueWriteBuffer(d_hidden, CL_TRUE, 0, sizeof(float) * dim,
                          host_hidden.data());

    const cl::Buffer *hidden = &d_hidden;
    for (int i = 0; i < args.layer_idx; ++i) {
      hidden = &transformer_layers[i].forward(*hidden, 0, 1);
    }
    cq.finish();

    dump_buffer(cq, *hidden, dim, args.output_prefix + "_input.dump");

    if (args.debug) {
      hidden = &transformer_layers[args.layer_idx].forward_debug(*hidden, 0, 1);
    } else {
      hidden = &transformer_layers[args.layer_idx].forward(*hidden, 0, 1);
    }
    cq.finish();

    dump_buffer(cq, *hidden, dim, args.output_prefix + "_output.dump");
    std::cout << "Dumped " << args.output_prefix << "_input.dump and "
              << args.output_prefix << "_output.dump\n";
  } catch (const std::exception &e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }

  return 0;
}
