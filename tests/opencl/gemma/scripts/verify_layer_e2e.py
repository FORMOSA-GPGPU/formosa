# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Compare one OpenCL Gemma layer against Hugging Face model inference.

The C++ helper dumps the hidden state entering and leaving one layer for a
single-token sequence. This script runs that helper, asks the Python model for
the corresponding hidden states, and reports max/mean differences.
"""

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


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=None, help="Path to verify_layer_e2e")
    parser.add_argument("--model-bin", default=str(find_asset("gemma-3-270m.bin")))
    parser.add_argument("--hf-model-dir", type=Path, default=DEFAULT_HF_MODEL_DIR)
    parser.add_argument("--token", type=int, default=100)
    parser.add_argument("--layer", type=int, default=0)
    parser.add_argument("--prefix", default="single_layer")
    parser.add_argument(
        "--dump-dir",
        type=Path,
        default=None,
        help="Directory for C++ output dumps. Defaults to a temporary directory.",
    )
    parser.add_argument("--atol", type=float, default=2e-4)
    parser.add_argument("--rtol", type=float, default=0.0)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--skip-cpp", action="store_true")
    return parser.parse_args()


def default_cpp_binary():
    build_dir = GEMMA_DIR.parents[2]
    if build_dir.name != "build":
        build_dir = build_dir / "build"
    return build_dir / "bin" / "verify_layer_e2e"


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
        "--token",
        str(args.token),
        "--layer",
        str(args.layer),
        "--output-prefix",
        args.prefix,
    ]
    if args.debug:
        cmd.append("--debug")

    env = os.environ.copy()
    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=True, env=env, cwd=DUMP_DIR)


def python_hidden_states(token_id):
    print(f"Loading Python model from {HF_MODEL_DIR} ...")
    model = AutoModelForCausalLM.from_pretrained(
        HF_MODEL_DIR,
        dtype=torch.float32,
        local_files_only=True,
    )
    model.eval()

    with torch.no_grad():
        input_ids = torch.tensor([[token_id]], dtype=torch.long)
        out = model(input_ids=input_ids, output_hidden_states=True, use_cache=False)
    return [
        h[0, 0].detach().cpu().numpy().astype(np.float32) for h in out.hidden_states
    ]


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
        temp_dump_dir = tempfile.TemporaryDirectory(prefix="gemma-layer-e2e-")
        DUMP_DIR = Path(temp_dump_dir.name)

    cpp_bin = Path(args.bin).resolve() if args.bin is not None else default_cpp_binary()
    if not args.skip_cpp:
        run_cpp(args, cpp_bin)

    input_dump = load_dump(f"{args.prefix}_input.dump")
    output_dump = load_dump(f"{args.prefix}_output.dump")
    hidden_states = python_hidden_states(args.token)

    if args.layer + 1 >= len(hidden_states):
        raise ValueError(f"Layer index {args.layer} is out of range")

    ok_input = compare(
        "layer_input",
        input_dump,
        hidden_states[args.layer],
        args.atol,
        args.rtol,
    )
    ok_output = compare(
        "layer_output",
        output_dump,
        hidden_states[args.layer + 1],
        args.atol,
        args.rtol,
    )
    return 0 if ok_input and ok_output else 1


if __name__ == "__main__":
    sys.exit(main())
