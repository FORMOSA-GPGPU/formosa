// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gemma_transformer_layer.h"
#include "model_loader.h"

namespace {

struct Args {
  std::string model_path = std::string(GEMMA_ASSET_ROOT) + "/gemma-3-270m.bin";
  int token_id = 100;
  int layer_idx = 0;
  int seq_pos = 0;
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
    } else if (arg == "--seq-pos" && i + 1 < argc) {
      args.seq_pos = parse_int(argv[++i], "--seq-pos");
    } else if (arg == "--help") {
      std::cout << "Usage: verify_layer_breakdown [--model PATH] [--token ID] "
                << "[--layer IDX] [--seq-pos POS]\n";
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

LayerConfig make_layer_config(const ModelConfig &model_config, int layer_idx) {
  LayerConfig cfg;
  cfg.hidden_dim = model_config.dim;
  cfg.num_heads = model_config.n_heads;
  cfg.num_kv_heads = model_config.n_kv_heads;
  cfg.head_dim = model_config.head_dim;
  cfg.mlp_hidden_dim = model_config.hidden_dim;
  cfg.max_seq_len = 512;
  cfg.epsilon = 1e-6f;
  cfg.attn_scale = 1.0f / std::sqrt(static_cast<float>(cfg.head_dim));
  cfg.theta_base = ((layer_idx % 6) == 5) ? 1000000.0f : 10000.0f;
  return cfg;
}

GemmaTransformerLayer make_transformer_layer(cl::Context &ctx,
                                             cl::CommandQueue &cq,
                                             const cl::Program &program,
                                             const ModelConfig &model_config,
                                             const LayerWeights &weights,
                                             int layer_idx) {
  return GemmaTransformerLayer(
      ctx, cq, program, make_layer_config(model_config, layer_idx),
      upload_weights(ctx, weights.input_layernorm,
                     weights.input_layernorm_size),
      upload_weights(ctx, weights.q_norm, weights.q_norm_size),
      upload_weights(ctx, weights.k_norm, weights.k_norm_size),
      upload_weights(ctx, weights.q_proj, weights.q_proj_size),
      upload_weights(ctx, weights.k_proj, weights.k_proj_size),
      upload_weights(ctx, weights.v_proj, weights.v_proj_size),
      upload_weights(ctx, weights.o_proj, weights.o_proj_size),
      upload_weights(ctx, weights.post_attn_layernorm,
                     weights.post_attn_layernorm_size),
      upload_weights(ctx, weights.pre_ff_layernorm,
                     weights.pre_ff_layernorm_size),
      upload_weights(ctx, weights.post_ff_layernorm,
                     weights.post_ff_layernorm_size),
      upload_weights(ctx, weights.gate_proj, weights.gate_proj_size),
      upload_weights(ctx, weights.up_proj, weights.up_proj_size),
      upload_weights(ctx, weights.down_proj, weights.down_proj_size));
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
    if (args.seq_pos < 0 || args.seq_pos >= 512) {
      throw std::runtime_error("Sequence position out of range");
    }

    std::vector<GemmaTransformerLayer> transformer_layers;
    transformer_layers.reserve(args.layer_idx + 1);
    for (int i = 0; i <= args.layer_idx; ++i) {
      transformer_layers.push_back(make_transformer_layer(
          ctx, cq, program, model.config, model.layers[i], i));
    }

    const int dim = model.config.dim;
    cl::Buffer d_hidden(ctx, CL_MEM_READ_WRITE, sizeof(float) * dim);
    std::vector<float> host_hidden;
    compute_token_embedding(model, args.token_id, host_hidden);
    cq.enqueueWriteBuffer(d_hidden, CL_TRUE, 0, sizeof(float) * dim,
                          host_hidden.data());

    const cl::Buffer *hidden = &d_hidden;
    for (int i = 0; i < args.layer_idx; ++i) {
      hidden = &transformer_layers[i].forward(*hidden, args.seq_pos,
                                              args.seq_pos + 1);
    }
    cq.finish();

    std::cout << "Running layer " << args.layer_idx
              << " forward_debug for token " << args.token_id << " at seq_pos "
              << args.seq_pos << "\n";
    hidden = &transformer_layers[args.layer_idx].forward_debug(
        *hidden, args.seq_pos, args.seq_pos + 1);
    cq.finish();

    std::cout << "Dumped forward_debug layer breakdown files\n";
  } catch (const std::exception &e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }

  return 0;
}
