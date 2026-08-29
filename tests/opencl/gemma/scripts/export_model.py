# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

import os
import struct
from pathlib import Path

import torch
import numpy as np
from transformers import AutoModelForCausalLM

from download_model import (
    MODEL_ID,
    download_model,
    find_asset,
    get_model_dir,
    is_model_downloaded,
)

SCRIPT_DIR = Path(__file__).resolve().parent
GEMMA_DIR = SCRIPT_DIR.parent if SCRIPT_DIR.name == "scripts" else SCRIPT_DIR


def export_model(model_id=MODEL_ID, output_file=None):
    model_dir = get_model_dir(model_id)
    if not is_model_downloaded(model_dir):
        model_dir = download_model(model_id=model_id)

    print(f"Loading {model_dir}...")
    output_file = (
        Path(output_file)
        if output_file is not None
        else find_asset(f"{model_id.split('/')[-1]}.bin")
    )
    if output_file.exists():
        print(f"File {output_file} already exists. Overwriting.")

    model = AutoModelForCausalLM.from_pretrained(
        model_dir,
        torch_dtype=torch.float32,
        use_safetensors=True,
    )
    config = model.config

    # Extract Model Dimensions
    vocab_size = config.vocab_size
    dim = config.hidden_size
    hidden_dim = config.intermediate_size
    n_layers = config.num_hidden_layers
    n_heads = config.num_attention_heads
    n_kv_heads = getattr(config, "num_key_value_heads", n_heads)
    head_dim = getattr(config, "head_dim", dim // n_heads)

    state = model.state_dict()

    def write_tensor(f, name, transpose=False):
        tensor = state[name].detach().cpu().numpy().astype(np.float32)
        if transpose:
            if tensor.ndim != 2:
                raise ValueError(f"{name} must be 2D to transpose, got {tensor.shape}")
            tensor = tensor.T
        tensor = np.ascontiguousarray(tensor)
        f.write(tensor.flatten().tobytes())
        layout = "transposed" if transpose else "native"
        print(
            f"Wrote {name}: {tensor.shape} {layout} " f"({tensor.nbytes / 1e6:.2f} MB)"
        )

    with open(output_file, "wb") as f:
        # Write the fixed-size header struct
        header = struct.pack(
            "i" * 8,
            vocab_size,
            dim,
            hidden_dim,
            n_layers,
            n_heads,
            n_kv_heads,
            head_dim,
            0,
        )
        f.write(header)
        print("Wrote header dimensions.")

        # The C++ loader expects this exact order. Linear weights are exported
        # as [in_features, out_features] for the OpenCL GEMM kernel's B[K, N].
        write_tensor(f, "model.embed_tokens.weight")

        for i in range(n_layers):
            prefix = f"model.layers.{i}"
            attn_prefix = f"{prefix}.self_attn"
            mlp_prefix = f"{prefix}.mlp"

            write_tensor(f, f"{attn_prefix}.q_proj.weight", transpose=True)
            write_tensor(f, f"{attn_prefix}.k_proj.weight", transpose=True)
            write_tensor(f, f"{attn_prefix}.v_proj.weight", transpose=True)
            write_tensor(f, f"{attn_prefix}.o_proj.weight", transpose=True)
            write_tensor(f, f"{attn_prefix}.q_norm.weight")
            write_tensor(f, f"{attn_prefix}.k_norm.weight")
            write_tensor(f, f"{mlp_prefix}.gate_proj.weight", transpose=True)
            write_tensor(f, f"{mlp_prefix}.up_proj.weight", transpose=True)
            write_tensor(f, f"{mlp_prefix}.down_proj.weight", transpose=True)
            write_tensor(f, f"{prefix}.input_layernorm.weight")
            write_tensor(f, f"{prefix}.post_attention_layernorm.weight")
            write_tensor(f, f"{prefix}.pre_feedforward_layernorm.weight")
            write_tensor(f, f"{prefix}.post_feedforward_layernorm.weight")

        write_tensor(f, "model.norm.weight")

    print(f"Export complete: {output_file}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--model-id", default=MODEL_ID)
    parser.add_argument("--output", type=Path, default=find_asset("gemma-3-270m.bin"))
    args = parser.parse_args()
    export_model(model_id=args.model_id, output_file=args.output)
