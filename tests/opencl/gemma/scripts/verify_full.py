# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Compare OpenCL Gemma full-model output with Hugging Face."""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch
from transformers import AutoModelForCausalLM

from download_model import MODEL_ID, find_asset, get_model_dir, is_model_downloaded

SCRIPT_DIR = Path(__file__).resolve().parent
GEMMA_DIR = SCRIPT_DIR.parent if SCRIPT_DIR.name == "scripts" else SCRIPT_DIR
DEFAULT_HF_MODEL_DIR = get_model_dir(model_id=MODEL_ID)


DEFAULT_TOKENS = [2, 100, 101]
HIDDEN_ATOL = 2e-3
HIDDEN_RTOL = 1e-4
NORM_ATOL = 5e-3
NORM_RTOL = 1e-4
LOGITS_ATOL = 1e-3
LOGITS_RTOL = 0.0


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=None, help="Path to verify_full")
    parser.add_argument("--model-bin", default=str(find_asset("gemma-3-270m.bin")))
    parser.add_argument("--hf-model-dir", type=Path, default=DEFAULT_HF_MODEL_DIR)
    parser.add_argument("--token", type=int, default=None)
    parser.add_argument("--tokens", default=None, help="Comma-separated token IDs")
    parser.add_argument("--prefix", default="full_model")
    parser.add_argument(
        "--dump-dir",
        type=Path,
        default=None,
        help="Directory for C++ output dumps. Defaults to a temporary directory.",
    )
    parser.add_argument("--skip-cpp", action="store_true")
    args = parser.parse_args()
    if args.tokens is not None:
        args.token_ids = [int(item) for item in args.tokens.split(",") if item]
    elif args.token is not None:
        args.token_ids = [args.token]
    else:
        args.token_ids = DEFAULT_TOKENS
    if not args.token_ids:
        raise ValueError("empty token list")
    return args


def default_cpp_binary():
    build_dir = GEMMA_DIR.parents[2]
    if build_dir.name != "build":
        build_dir = build_dir / "build"
    return build_dir / "bin" / "verify_full"


def load_dump(path):
    with open(DUMP_DIR / path) as f:
        return np.array([float(line) for line in f if line.strip()], dtype=np.float32)


def compare(name, actual, expected, atol, rtol):
    if actual.shape != expected.shape:
        print(f"FAIL  {name:<16} shape expected={expected.shape} got={actual.shape}")
        return False

    diff = np.abs(actual - expected)
    max_diff = float(diff.max())
    mean_diff = float(diff.mean())
    ok = bool(np.allclose(actual, expected, atol=atol, rtol=rtol))
    print(
        f"{'PASS' if ok else 'FAIL'}  {name:<16} "
        f"max_diff={max_diff:.6e} mean_diff={mean_diff:.6e}"
    )
    if not ok:
        idx = int(diff.argmax())
        print(
            f"      worst_idx={idx} cpp={actual[idx]:.9g} "
            f"python={expected[idx]:.9g}"
        )
    return ok


def run_cpp(args, cpp_bin):
    DUMP_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(cpp_bin),
        "--model",
        str(Path(args.model_bin).resolve()),
        "--tokens",
        ",".join(str(token_id) for token_id in args.token_ids),
        "--output-prefix",
        args.prefix,
    ]
    env = os.environ.copy()
    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=True, env=env, cwd=DUMP_DIR)


def python_outputs(token_ids):
    print(f"Loading Python model from {HF_MODEL_DIR} ...")
    model = AutoModelForCausalLM.from_pretrained(
        HF_MODEL_DIR,
        dtype=torch.float32,
        local_files_only=True,
    )
    model.eval()

    captured = {}

    def capture_last_layer(_module, _inputs, output):
        hidden = output[0] if isinstance(output, tuple) else output
        captured["final_hidden"] = hidden.detach().cpu().numpy().astype(np.float32)

    handle = model.model.layers[-1].register_forward_hook(capture_last_layer)
    with torch.no_grad():
        input_ids = torch.tensor([token_ids], dtype=torch.long)
        out = model(input_ids=input_ids, output_hidden_states=True, use_cache=False)
    handle.remove()

    final_hidden = captured["final_hidden"][0]
    final_norm = out.hidden_states[-1][0].detach().cpu().numpy().astype(np.float32)
    logits = out.logits[0].detach().cpu().numpy().astype(np.float32)
    return final_hidden, final_norm, logits


def compare_position(prefix, pos, py_hidden, py_norm, py_logits):
    cpp_hidden = load_dump(f"{prefix}_pos{pos}_hidden.dump")
    cpp_norm = load_dump(f"{prefix}_pos{pos}_norm.dump")
    cpp_logits = load_dump(f"{prefix}_pos{pos}_logits.dump")

    ok = True
    print(f"seq_pos={pos}")
    ok &= compare("final_hidden", cpp_hidden, py_hidden[pos], HIDDEN_ATOL, HIDDEN_RTOL)
    ok &= compare("final_norm", cpp_norm, py_norm[pos], NORM_ATOL, NORM_RTOL)
    ok &= compare("logits", cpp_logits, py_logits[pos], LOGITS_ATOL, LOGITS_RTOL)

    cpp_next = int(cpp_logits.argmax())
    py_next = int(py_logits[pos].argmax())
    print(f"argmax cpp={cpp_next} python={py_next}")
    return ok and cpp_next == py_next


def main():
    args = parse_args()
    global HF_MODEL_DIR
    HF_MODEL_DIR = args.hf_model_dir.resolve()
    if not is_model_downloaded(HF_MODEL_DIR):
        raise SystemExit(
            f"Hugging Face model is missing or incomplete: {HF_MODEL_DIR}. "
            "Run download_model.py or pass --hf-model-dir."
        )

    global DUMP_DIR
    temp_dump_dir = None
    if args.dump_dir is not None:
        DUMP_DIR = args.dump_dir.resolve()
    elif args.skip_cpp:
        DUMP_DIR = GEMMA_DIR
    else:
        temp_dump_dir = tempfile.TemporaryDirectory(prefix="gemma-full-")
        DUMP_DIR = Path(temp_dump_dir.name)

    cpp_bin = Path(args.bin).resolve() if args.bin is not None else default_cpp_binary()
    print(f"tokens={args.token_ids} final_seq_pos={len(args.token_ids) - 1}")
    if not args.skip_cpp:
        run_cpp(args, cpp_bin)

    py_hidden, py_norm, py_logits = python_outputs(args.token_ids)

    ok = True
    for pos in range(len(args.token_ids)):
        ok &= compare_position(args.prefix, pos, py_hidden, py_norm, py_logits)

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
