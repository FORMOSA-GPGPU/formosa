// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
  std::string vocab_path = std::string(GEMMA_ASSET_ROOT) + "/vocab.dump";
  int max_new_tokens = 3;
  int repeat_window = 64;
  float repetition_penalty = 1.08f;
  std::vector<int> prompt_tokens;
};

static std::string read_kernel_source(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open kernel file: " + path);
  }
  std::ostringstream source;
  source << file.rdbuf();
  return source.str();
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  throw std::runtime_error("Invalid hex character in vocab.dump");
}

static std::string decode_hex(const std::string &hex) {
  if ((hex.size() % 2) != 0) {
    throw std::runtime_error("Invalid hex token length in vocab.dump");
  }

  std::string out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = hex_value(hex[i]);
    const int lo = hex_value(hex[i + 1]);
    out.push_back(static_cast<char>((hi << 4) | lo));
  }
  return out;
}

static int parse_int(const std::string &value, const std::string &name) {
  try {
    size_t pos = 0;
    int result = std::stoi(value, &pos);
    if (pos != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return result;
  } catch (const std::exception &) {
    throw std::runtime_error("Invalid " + name + ": " + value);
  }
}

static float parse_float(const std::string &value, const std::string &name) {
  try {
    size_t pos = 0;
    float result = std::stof(value, &pos);
    if (pos != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return result;
  } catch (const std::exception &) {
    throw std::runtime_error("Invalid " + name + ": " + value);
  }
}

static Args parse_args(int argc, char *argv[]) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      args.model_path = argv[++i];
    } else if (arg == "--vocab" && i + 1 < argc) {
      args.vocab_path = argv[++i];
    } else if (arg == "--max-new-tokens" && i + 1 < argc) {
      args.max_new_tokens = parse_int(argv[++i], "--max-new-tokens");
    } else if (arg == "--repeat-window" && i + 1 < argc) {
      args.repeat_window = parse_int(argv[++i], "--repeat-window");
    } else if (arg == "--repetition-penalty" && i + 1 < argc) {
      args.repetition_penalty = parse_float(argv[++i], "--repetition-penalty");
    } else if (arg == "--help") {
      std::cout << "Usage: gemma [--model PATH] [--vocab PATH] "
                << "[--max-new-tokens N] [--repeat-window N] "
                << "[--repetition-penalty VALUE] [TOKEN_ID ...]\n";
      std::exit(0);
    } else {
      args.prompt_tokens.push_back(parse_int(arg, "token id"));
    }
  }
  return args;
}

static std::vector<std::string> load_vocab(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw std::runtime_error("Failed to open vocab file: " + path);
  }
  std::vector<std::string> vocab;
  std::string line;
  while (std::getline(f, line)) {
    vocab.push_back(decode_hex(line));
  }
  return vocab;
}

/* Decode one SentencePiece token piece to a printable UTF-8 string.
 * Handles two special forms produced by convert_ids_to_tokens():
 *   ▁word  -> " word"   (U+2581 = word-start space marker)
 *   <0xNN> -> raw byte  (byte-fallback tokens for out-of-vocab characters) */
static std::string decode_piece(const std::string &piece) {
  /* Byte-fallback token: exactly "<0xNN>" where NN is a hex byte */
  if (piece.size() == 6 && piece[0] == '<' && piece[1] == '0' &&
      piece[2] == 'x' && piece[5] == '>') {
    char byte_val =
        static_cast<char>(std::stoi(piece.substr(3, 2), nullptr, 16));
    return std::string(1, byte_val);
  }

  /* Replace ▁ (UTF-8: E2 96 81) with a regular space */
  const std::string marker = "\xe2\x96\x81";
  std::string result;
  result.reserve(piece.size());
  size_t i = 0;
  while (i < piece.size()) {
    if (piece.compare(i, marker.size(), marker) == 0) {
      result += ' ';
      i += marker.size();
    } else {
      result += piece[i++];
    }
  }
  return result;
}

static cl::Buffer upload_weights(cl::Context &ctx, float *data, size_t count) {
  return cl::Buffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                    sizeof(float) * count, data);
}

static void compute_token_embedding(const ModelLoader &model, int token,
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

static void rmsnorm_cpu(const float *input, const float *weights, int dim,
                        float epsilon, std::vector<float> &output) {
  float sum_sq = 0.0f;
  for (int i = 0; i < dim; ++i) {
    sum_sq += input[i] * input[i];
  }
  const float mean_sq = sum_sq / static_cast<float>(dim);
  const float inv_rms = 1.0f / std::sqrt(mean_sq + epsilon);

  output.resize(dim);
  for (int i = 0; i < dim; ++i) {
    output[i] = input[i] * inv_rms * (weights[i] + 1.0f);
  }
}

static int sample_next_cpu(const ModelLoader &model, const float *hidden,
                           const std::vector<int> &all_tokens,
                           int repeat_window, float repetition_penalty,
                           float epsilon) {
  const int dim = model.config.dim;
  const int vocab_size = model.config.vocab_size;

  std::vector<float> norm_out;
  rmsnorm_cpu(hidden, model.final_norm, dim, epsilon, norm_out);

  std::vector<float> logits(vocab_size);
  for (int token = 0; token < vocab_size; ++token) {
    const float *embed_row =
        model.embed_tokens + static_cast<size_t>(token) * dim;
    float sum = 0.0f;
    for (int i = 0; i < dim; ++i) {
      sum += embed_row[i] * norm_out[i];
    }
    logits[token] = sum;
  }

  const int recent_start =
      std::max(0, static_cast<int>(all_tokens.size()) - repeat_window);
  for (int i = recent_start; i < static_cast<int>(all_tokens.size()); ++i) {
    const int token = all_tokens[i];
    if (token >= 0 && token < vocab_size) {
      if (logits[token] > 0.0f) {
        logits[token] /= repetition_penalty;
      } else {
        logits[token] *= repetition_penalty;
      }
    }
  }

  int next_token = 0;
  float best_logit = -std::numeric_limits<float>::infinity();
  for (int token = 0; token < vocab_size; ++token) {
    if (logits[token] > best_logit) {
      best_logit = logits[token];
      next_token = token;
    }
  }
  return next_token;
}

}  // namespace

int main(int argc, char *argv[]) {
  const Args args = parse_args(argc, argv);

  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) {
    std::cout << "No OpenCL platforms available\n";
    return 1;
  }

  std::vector<cl::Device> devices;
  platforms[0].getDevices(CL_DEVICE_TYPE_ALL, &devices);
  if (devices.empty()) {
    std::cout << "No OpenCL devices available\n";
    return 1;
  }
  cl::Device device = devices[0];
  std::cout << "Device: " << device.getInfo<CL_DEVICE_NAME>() << "\n";

  cl::Context ctx(device);
  cl::CommandQueue cq(ctx, device);

  /* Build OpenCL program from all kernel sources */
  const std::string kernel_prefix = std::string(GEMMA_ROOT) + "/";
  cl::Program::Sources sources;
  const std::vector<std::string> kernel_files = {
      "rmsnorm.cl",         "qk_norm.cl", "gemm.cl",  "rope.cl",
      "kv_cache_update.cl", "gqa.cl",     "geglu.cl", "residual_add.cl",
  };
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

  /* Load model weights from binary file */
  ModelLoader model(args.model_path);
  const int dim = model.config.dim;
  const int n_layers = model.config.n_layers;
  const int vocab_size = model.config.vocab_size;
  std::cout << "Model: dim=" << dim << " n_layers=" << n_layers
            << " vocab_size=" << vocab_size << "\n";

  /* Working buffers */
  cl::Buffer d_hidden(ctx, CL_MEM_READ_WRITE, sizeof(float) * dim);

  /* Base layer config; theta_base is overridden per layer below */
  const int max_seq_len = 512;
  LayerConfig base_cfg;
  base_cfg.hidden_dim = dim;
  base_cfg.num_heads = model.config.n_heads;
  base_cfg.num_kv_heads = model.config.n_kv_heads;
  base_cfg.head_dim = model.config.head_dim;
  base_cfg.mlp_hidden_dim = model.config.hidden_dim;
  base_cfg.max_seq_len = max_seq_len;
  base_cfg.epsilon = 1e-6f;
  base_cfg.attn_scale = 1.0f / std::sqrt(static_cast<float>(base_cfg.head_dim));

  /* Build all transformer layers.
   * Every 6th layer (indices 5, 11, 17, ...) uses full attention
   * (theta = 1e6); all others use sliding-window attention (theta = 1e4). */
  std::vector<GemmaTransformerLayer> transformer_layers;
  transformer_layers.reserve(n_layers);
  for (int i = 0; i < n_layers; ++i) {
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
        upload_weights(ctx, w.post_attn_layernorm, w.post_attn_layernorm_size),
        upload_weights(ctx, w.pre_ff_layernorm, w.pre_ff_layernorm_size),
        upload_weights(ctx, w.post_ff_layernorm, w.post_ff_layernorm_size),
        upload_weights(ctx, w.gate_proj, w.gate_proj_size),
        upload_weights(ctx, w.up_proj, w.up_proj_size),
        upload_weights(ctx, w.down_proj, w.down_proj_size));
  }

  const std::vector<std::string> vocab = load_vocab(args.vocab_path);

  const int bos_token = 2;
  const int eos_token = 1;

  /* Build prompt token sequence: BOS followed by any IDs from argv */
  std::vector<int> prompt_tokens;
  prompt_tokens.push_back(bos_token);
  prompt_tokens.insert(prompt_tokens.end(), args.prompt_tokens.begin(),
                       args.prompt_tokens.end());
  std::vector<int> all_tokens = prompt_tokens;

  /* Print echoed prompt text (skip BOS) */
  std::cout << "Prompt:";
  for (int i = 1; i < static_cast<int>(prompt_tokens.size()); ++i) {
    int t = prompt_tokens[i];
    if (static_cast<size_t>(t) < vocab.size()) {
      std::cout << decode_piece(vocab[t]);
    }
  }
  std::cout << "\nOutput:";
  std::fflush(stdout);

  /* Helper: run embedding + all layers for one token at seq_pos, return
   * pointer to the last layer's output hidden state. */
  auto run_layers = [&](int token, int seq_pos) -> const cl::Buffer * {
    std::vector<float> host_hidden;
    compute_token_embedding(model, token, host_hidden);
    cq.enqueueWriteBuffer(d_hidden, CL_TRUE, 0, sizeof(float) * dim,
                          host_hidden.data());

    int seq_len = seq_pos + 1;
    const cl::Buffer *h = &d_hidden;
    for (int l = 0; l < n_layers; ++l) {
      h = &transformer_layers[l].forward(*h, seq_pos, seq_len);
    }
    return h;
  };

  /* Helper: run final norm + lm-head + repeat-penalized argmax on a hidden
   * state buffer, return the sampled token ID. */
  auto sample_next = [&](const cl::Buffer &hidden) -> int {
    std::vector<float> host_hidden(dim);
    cq.enqueueReadBuffer(hidden, CL_TRUE, 0, sizeof(float) * dim,
                         host_hidden.data());
    return sample_next_cpu(model, host_hidden.data(), all_tokens,
                           args.repeat_window, args.repetition_penalty,
                           base_cfg.epsilon);
  };

  /* Prefill: process all prompt tokens to populate the KV cache.
   * Sample only from the last prompt token's hidden state. */
  const cl::Buffer *hidden = nullptr;
  for (int i = 0; i < static_cast<int>(prompt_tokens.size()); ++i) {
    hidden = run_layers(prompt_tokens[i], i);
  }
  std::cout << "Prefill done. Starting autoregressive generation...\n";

  int current_token = sample_next(*hidden);
  int seq_pos = static_cast<int>(prompt_tokens.size());

  /* Autoregressive generation */
  for (int step = 0; step < args.max_new_tokens; ++step) {
    if (current_token == eos_token) {
      break;
    }

    if (static_cast<size_t>(current_token) < vocab.size()) {
      std::cout << decode_piece(vocab[current_token]);
    }
    std::fflush(stdout);

    hidden = run_layers(current_token, seq_pos);
    all_tokens.push_back(current_token);
    current_token = sample_next(*hidden);
    ++seq_pos;
  }

  std::cout << "\n";
  return 0;
}
