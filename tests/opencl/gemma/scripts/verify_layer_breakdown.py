# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Verify C++ GemmaTransformerLayer forward_debug() stage dumps."""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch
from download_model import MODEL_ID, find_asset, get_model_dir, is_model_downloaded
from transformers import AutoConfig, AutoModelForCausalLM

DEFAULT_TOKEN_ID = 100
DEFAULT_SEQ_POS = 0
DEFAULT_LAYER_IDX = 0
SCRIPT_DIR = Path(__file__).resolve().parent
GEMMA_DIR = SCRIPT_DIR.parent if SCRIPT_DIR.name == "scripts" else SCRIPT_DIR
DEFAULT_HF_MODEL_DIR = get_model_dir(model_id=MODEL_ID)
STAGE_ORDER = [
    "pre_att_norm",
    "q_proj",
    "k_proj",
    "v_proj",
    "q_qknorm",
    "k_qknorm",
    "q_rope",
    "k_rope",
    "att_out",
    "o_proj",
    "post_attn_norm",
    "post_attn_residual",
    "pre_mlp_norm",
    "gate_proj",
    "up_proj",
    "mlp_gate_cubed",
    "mlp_tanh_arg",
    "mlp_tanh",
    "mlp_gelu",
    "mlp_act",
    "down_proj",
    "post_ff_norm",
    "output",
]
MLP_FOCUS_STAGES = {
    "pre_mlp_norm",
    "gate_proj",
    "up_proj",
    "mlp_gate_cubed",
    "mlp_tanh_arg",
    "mlp_tanh",
    "mlp_gelu",
    "mlp_act",
    "down_proj",
    "post_ff_norm",
    "output",
}


_passed = 0
_failed = 0
_skipped = 0


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=None, help="Path to verify_layer_breakdown")
    parser.add_argument("--model-bin", default=str(find_asset("gemma-3-270m.bin")))
    parser.add_argument("--hf-model-dir", type=Path, default=DEFAULT_HF_MODEL_DIR)
    parser.add_argument("--token", type=int, default=DEFAULT_TOKEN_ID)
    parser.add_argument("--layer", type=int, default=DEFAULT_LAYER_IDX)
    parser.add_argument("--seq-pos", type=int, default=DEFAULT_SEQ_POS)
    parser.add_argument(
        "--dump-dir",
        type=Path,
        default=None,
        help="Directory for C++ stage dumps. Defaults to a temporary directory.",
    )
    parser.add_argument(
        "--stage",
        action="append",
        choices=STAGE_ORDER,
        help="Verify only this stage. Repeat to verify multiple stages.",
    )
    parser.add_argument("--skip-cpp", action="store_true")
    parser.add_argument(
        "--focus-mlp",
        action="store_true",
        help="Only verify MLP-related stages from pre_mlp_norm onward",
    )
    return parser.parse_args()


def default_cpp_binary():
    build_dir = GEMMA_DIR.parents[2]
    if build_dir.name != "build":
        build_dir = build_dir / "build"
    return build_dir / "bin" / "verify_layer_breakdown"


def run_cpp(args, cpp_bin):
    DUMP_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(cpp_bin),
        "--model",
        str(Path(args.model_bin).resolve()),
        "--token",
        str(args.token),
        "--layer",
        str(args.layer),
        "--seq-pos",
        str(args.seq_pos),
    ]
    env = os.environ.copy()
    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=True, env=env, cwd=DUMP_DIR)


def layer_type_and_rope_theta(cfg, layer_idx):
    layer_types = getattr(cfg, "layer_types", None)
    if layer_types is not None:
        if layer_idx < 0 or layer_idx >= len(layer_types):
            raise ValueError(
                f"--layer {layer_idx} is outside config layer_types length {len(layer_types)}"
            )
        layer_type = layer_types[layer_idx]
    else:
        layer_type = "full_attention"

    rope_parameters = getattr(cfg, "rope_parameters", None)
    if rope_parameters is not None:
        params = rope_parameters.get(layer_type)
        if params is not None and "rope_theta" in params:
            return layer_type, float(params["rope_theta"])

    if layer_type == "sliding_attention":
        if hasattr(cfg, "rope_local_base_freq"):
            return layer_type, float(cfg.rope_local_base_freq)
        return layer_type, 10000.0

    if hasattr(cfg, "rope_theta"):
        return layer_type, float(cfg.rope_theta)
    return layer_type, 1000000.0


args = parse_args()
if args.seq_pos != DEFAULT_SEQ_POS:
    raise ValueError("verify_layer_breakdown.py currently supports --seq-pos 0 only")
HF_MODEL_DIR = args.hf_model_dir.resolve()
if not is_model_downloaded(HF_MODEL_DIR):
    raise SystemExit(
        f"Hugging Face model is missing or incomplete: {HF_MODEL_DIR}. "
        "Run download_model.py or pass --hf-model-dir."
    )
preflight_cfg = AutoConfig.from_pretrained(HF_MODEL_DIR, local_files_only=True)
selected_stages = set(args.stage or [])
_temp_dump_dir = None
if args.dump_dir is not None:
    DUMP_DIR = args.dump_dir.resolve()
elif args.skip_cpp:
    DUMP_DIR = GEMMA_DIR
else:
    _temp_dump_dir = tempfile.TemporaryDirectory(prefix="gemma-breakdown-")
    DUMP_DIR = Path(_temp_dump_dir.name)
TOKEN_ID = args.token
SEQ_POS = args.seq_pos
LAYER_IDX = args.layer
layer_type, theta_base = layer_type_and_rope_theta(preflight_cfg, LAYER_IDX)
if not args.skip_cpp:
    cpp_bin = Path(args.bin).resolve() if args.bin is not None else default_cpp_binary()
    run_cpp(args, cpp_bin)


def load_dump(name):
    try:
        with open(DUMP_DIR / f"{name}.dump") as f:
            return np.array([float(l) for l in f if l.strip()], dtype=np.float32)
    except FileNotFoundError:
        return None


def verify(name, golden, atol=2e-4):
    global _passed, _failed, _skipped
    if selected_stages and name not in selected_stages:
        print(f"SKIP  {name:<35}  (filtered by --stage)")
        _skipped += 1
        return

    if args.focus_mlp and name not in MLP_FOCUS_STAGES:
        print(f"SKIP  {name:<35}  (filtered by --focus-mlp)")
        _skipped += 1
        return

    actual = load_dump(name)
    if actual is None:
        print(f"SKIP  {name:<35}  (no .dump file)")
        _skipped += 1
        return

    expected = np.asarray(golden, dtype=np.float32).flatten()
    if expected.shape != actual.shape:
        print(
            f"FAIL  {name:<35}  shape mismatch expected={expected.shape} got={actual.shape}"
        )
        _failed += 1
        return

    max_diff = float(np.abs(expected - actual).max())
    ok = bool(np.allclose(expected, actual, atol=atol, rtol=0))
    print(f"{'PASS' if ok else 'FAIL'}  {name:<35}  max_diff={max_diff:.6e}")
    if ok:
        _passed += 1
    else:
        _failed += 1


def verify_derived_stage(
    name, golden, source_name, derive_fn, atol=2e-4, local_atol=1e-6
):
    global _passed, _failed, _skipped
    if selected_stages and name not in selected_stages:
        print(f"SKIP  {name:<35}  (filtered by --stage)")
        _skipped += 1
        return

    if args.focus_mlp and name not in MLP_FOCUS_STAGES:
        print(f"SKIP  {name:<35}  (filtered by --focus-mlp)")
        _skipped += 1
        return

    actual = load_dump(name)
    if actual is None:
        print(f"SKIP  {name:<35}  (no .dump file)")
        _skipped += 1
        return

    expected = np.asarray(golden, dtype=np.float32).flatten()
    if expected.shape != actual.shape:
        print(
            f"FAIL  {name:<35}  shape mismatch expected={expected.shape} got={actual.shape}"
        )
        _failed += 1
        return

    max_diff = float(np.abs(expected - actual).max())
    ok = bool(np.allclose(expected, actual, atol=atol, rtol=0))
    if ok:
        print(f"PASS  {name:<35}  max_diff={max_diff:.6e}")
        _passed += 1
        return

    source_actual = load_dump(source_name)
    if source_actual is not None and source_actual.shape == actual.shape:
        local_expected = np.asarray(
            derive_fn(source_actual), dtype=np.float32
        ).flatten()
        local_max_diff = float(np.abs(local_expected - actual).max())
        local_ok = bool(np.allclose(local_expected, actual, atol=local_atol, rtol=0))
        if local_ok:
            print(
                f"PASS  {name:<35}  max_diff={max_diff:.6e}"
                f"  local_max_diff={local_max_diff:.6e}"
            )
            _passed += 1
            return

    print(f"FAIL  {name:<35}  max_diff={max_diff:.6e}")
    _failed += 1


def rmsnorm(x, weight, eps):
    """Matches the C++ rmsnorm kernel exactly."""
    x = x.astype(np.float32)
    sum_sq = np.sum(x**2, dtype=np.float32)
    mean_sq = sum_sq / np.float32(len(x))
    inv_rms = np.float32(1.0) / np.sqrt(mean_sq + np.float32(eps))
    return x * inv_rms * (weight.astype(np.float32) + np.float32(1.0))


def qk_norm(x_flat, num_heads, head_dim, weight, eps):
    """Per-head QK normalization, matches the C++ qk_norm kernel exactly."""
    x = x_flat.reshape(num_heads, head_dim).astype(np.float32).copy()
    w = weight.astype(np.float32)
    for h in range(num_heads):
        sum_sq = np.sum(x[h] ** 2, dtype=np.float32)
        inv_rms = np.float32(1.0) / np.sqrt(
            sum_sq / np.float32(head_dim) + np.float32(eps)
        )
        x[h] = x[h] * inv_rms * (w + np.float32(1.0))
    return x.flatten()


def apply_rope(x_flat, num_heads, head_dim, seq_pos, theta_base):
    """Matches the C++ rope kernel exactly (in-place per-head rotation)."""
    x = x_flat.reshape(num_heads, head_dim).astype(np.float32).copy()
    half_dim = head_dim // 2
    dim_idx = np.arange(0, half_dim, dtype=np.float32)
    inv_freq = np.float32(theta_base) ** (
        -(dim_idx * np.float32(2.0) / np.float32(head_dim))
    )
    theta = np.float32(seq_pos) * inv_freq
    cos_t = np.cos(theta).astype(np.float32)
    sin_t = np.sin(theta).astype(np.float32)
    x_first = x[:, :half_dim].copy()
    x_second = x[:, half_dim:].copy()
    x[:, :half_dim] = x_first * cos_t - x_second * sin_t
    x[:, half_dim:] = x_second * cos_t + x_first * sin_t
    return x.flatten()


def geglu(gate, up):
    """Matches the C++ geglu kernel (approximate GELU * up)."""
    x = gate.astype(np.float32)
    tanh_arg = np.float32(0.79788456) * (x + np.float32(0.044715) * x**3)
    gelu_x = (
        np.float32(0.5) * x * (np.float32(1.0) + np.tanh(tanh_arg).astype(np.float32))
    )
    return gelu_x * up.astype(np.float32)


def geglu_breakdown(gate, up):
    """Stage-by-stage breakdown matching the debug kernels exactly."""
    x = gate.astype(np.float32)
    up = up.astype(np.float32)
    gate_cubed = (x * x * x).astype(np.float32)
    tanh_arg = (
        np.float32(0.79788456) * (x + np.float32(0.044715) * gate_cubed)
    ).astype(np.float32)
    tanh_out = np.tanh(tanh_arg).astype(np.float32)
    gelu_x = (np.float32(0.5) * x * (np.float32(1.0) + tanh_out)).astype(np.float32)
    act = (gelu_x * up).astype(np.float32)
    return gate_cubed, tanh_arg, tanh_out, gelu_x, act


def gqa_attention(q, k_heads, v_heads, num_heads, num_kv_heads, head_dim, scale):
    """
    Matches the C++ attention_gqa kernel.
    q:       [num_heads, head_dim]
    k_heads: [num_kv_heads, seq_len, head_dim]
    v_heads: [num_kv_heads, seq_len, head_dim]
    """
    kv_groups = num_heads // num_kv_heads
    out = np.zeros_like(q)
    for h in range(num_heads):
        kv_h = h // kv_groups
        scores = (q[h] @ k_heads[kv_h].T).astype(np.float32) * np.float32(scale)
        max_s = scores.max()
        exp_s = np.exp(scores - max_s).astype(np.float32)
        weights = (exp_s / exp_s.sum()).astype(np.float32)
        out[h] = (weights @ v_heads[kv_h]).astype(np.float32)
    return out.flatten()


# ── Load model ────────────────────────────────────────────────────────────────

print(f"Loading model from {HF_MODEL_DIR} ...")
model = AutoModelForCausalLM.from_pretrained(
    HF_MODEL_DIR, dtype=torch.float32, local_files_only=True
)
model.eval()

cfg = model.config
layer = model.model.layers[LAYER_IDX]
attn = layer.self_attn
mlp = layer.mlp

hidden_dim = cfg.hidden_size
num_heads = cfg.num_attention_heads
num_kv_heads = cfg.num_key_value_heads
head_dim = cfg.head_dim
mlp_hidden_dim = cfg.intermediate_size
eps = float(cfg.rms_norm_eps)
scale = float(head_dim) ** -0.5

layer_type, theta_base = layer_type_and_rope_theta(cfg, LAYER_IDX)

print(
    f"  layer_type={layer_type}  hidden={hidden_dim}  heads={num_heads}"
    f"  kv_heads={num_kv_heads}  head_dim={head_dim}  mlp_dim={mlp_hidden_dim}"
)
print(f"  eps={eps}  theta={theta_base}  scale={scale:.6f}\n")

with torch.no_grad():
    w_input_norm = layer.input_layernorm.weight.numpy().astype(np.float32)
    w_q_norm = attn.q_norm.weight.numpy().astype(np.float32)
    w_k_norm = attn.k_norm.weight.numpy().astype(np.float32)
    w_post_attn_norm = layer.post_attention_layernorm.weight.numpy().astype(np.float32)
    w_pre_ff_norm = layer.pre_feedforward_layernorm.weight.numpy().astype(np.float32)
    w_post_ff_norm = layer.post_feedforward_layernorm.weight.numpy().astype(np.float32)
    wq = attn.q_proj.weight.numpy().astype(np.float32)  # [q_dim,   hidden]
    wk = attn.k_proj.weight.numpy().astype(np.float32)  # [kv_dim,  hidden]
    wv = attn.v_proj.weight.numpy().astype(np.float32)  # [kv_dim,  hidden]
    wo = attn.o_proj.weight.numpy().astype(np.float32)  # [hidden,  q_dim ]
    w_gate = mlp.gate_proj.weight.numpy().astype(np.float32)  # [mlp_dim, hidden]
    w_up = mlp.up_proj.weight.numpy().astype(np.float32)  # [mlp_dim, hidden]
    w_down = mlp.down_proj.weight.numpy().astype(np.float32)  # [hidden,  mlp_dim]

    token = torch.tensor([[TOKEN_ID]], dtype=torch.long)
    if LAYER_IDX == 0:
        hidden = model.model.embed_tokens(token)[0, 0].numpy().astype(np.float32)
    else:
        out = model(input_ids=token, output_hidden_states=True, use_cache=False)
        hidden = out.hidden_states[LAYER_IDX][0, 0].numpy().astype(np.float32)

# Verification
pre_att_norm = rmsnorm(hidden, w_input_norm, eps)
verify("pre_att_norm", pre_att_norm)

q = (wq @ pre_att_norm).astype(np.float32)
k = (wk @ pre_att_norm).astype(np.float32)
v = (wv @ pre_att_norm).astype(np.float32)
verify("q_proj", q)
verify("k_proj", k)
verify("v_proj", v)

q = qk_norm(q, num_heads, head_dim, w_q_norm, eps)
k = qk_norm(k, num_kv_heads, head_dim, w_k_norm, eps)
verify("q_qknorm", q)
verify("k_qknorm", k)

q_rope = apply_rope(q, num_heads, head_dim, SEQ_POS, theta_base)
k_rope = apply_rope(k, num_kv_heads, head_dim, SEQ_POS, theta_base)
verify("q_rope", q_rope)
verify("k_rope", k_rope)

seq_len = SEQ_POS + 1
k_cache = k_rope.reshape(num_kv_heads, head_dim)[:, np.newaxis, :]  # [nkv, 1, hd]
v_cache = v.reshape(num_kv_heads, head_dim)[:, np.newaxis, :]  # [nkv, 1, hd]
att_out = gqa_attention(
    q_rope.reshape(num_heads, head_dim),
    k_cache,
    v_cache,
    num_heads,
    num_kv_heads,
    head_dim,
    scale,
)
verify("att_out", att_out)

o_out = (wo @ att_out).astype(np.float32)
verify("o_proj", o_out)

o_normed = rmsnorm(o_out, w_post_attn_norm, eps)
verify("post_attn_norm", o_normed)

post_attn = (o_normed + hidden).astype(np.float32)
verify("post_attn_residual", post_attn)

pre_mlp_norm = rmsnorm(post_attn, w_pre_ff_norm, eps)
verify("pre_mlp_norm", pre_mlp_norm)

gate = (w_gate @ pre_mlp_norm).astype(np.float32)
up = (w_up @ pre_mlp_norm).astype(np.float32)
verify("gate_proj", gate)
verify("up_proj", up)

mlp_gate_cubed, mlp_tanh_arg, mlp_tanh, mlp_gelu, mlp_act = geglu_breakdown(gate, up)
verify_derived_stage(
    "mlp_gate_cubed",
    mlp_gate_cubed,
    "gate_proj",
    lambda x: x.astype(np.float32) * x.astype(np.float32) * x.astype(np.float32),
)
verify("mlp_tanh_arg", mlp_tanh_arg)
verify("mlp_tanh", mlp_tanh)
verify("mlp_gelu", mlp_gelu)
verify("mlp_act", mlp_act)

down_out = (w_down @ mlp_act).astype(np.float32)
verify("down_proj", down_out)

down_normed = rmsnorm(down_out, w_post_ff_norm, eps)
verify("post_ff_norm", down_normed)

output = (post_attn + down_normed).astype(np.float32)
verify("output", output)

# ── Summary ───────────────────────────────────────────────────────────────────

print(f"\n{_passed} passed  {_failed} failed  {_skipped} skipped")
sys.exit(0 if _failed == 0 else 1)
