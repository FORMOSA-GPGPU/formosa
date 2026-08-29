# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

import argparse
from pathlib import Path

from huggingface_hub import snapshot_download

SCRIPT_DIR = Path(__file__).resolve().parent
GEMMA_DIR = SCRIPT_DIR.parent if SCRIPT_DIR.name == "scripts" else SCRIPT_DIR
MODEL_ID = "google/gemma-3-270m"
MODEL_ROOT = GEMMA_DIR / ".model"
MODEL_NAME = MODEL_ID.split("/")[-1]
MODEL_DIR = MODEL_ROOT / MODEL_NAME


def candidate_gemma_dirs():
    """Return runfile/source Gemma dirs that may hold generated test assets."""
    dirs = [GEMMA_DIR]
    for base in [Path.cwd(), *SCRIPT_DIR.parents]:
        candidate = base / "tests" / "opencl" / "gemma"
        if candidate != GEMMA_DIR and (candidate / "scripts").is_dir():
            dirs.append(candidate)

    deduped = []
    seen = set()
    for directory in dirs:
        resolved = directory.resolve()
        if resolved not in seen:
            seen.add(resolved)
            deduped.append(directory)
    return deduped


def is_model_downloaded(model_dir=MODEL_DIR):
    model_dir = Path(model_dir)
    return (
        model_dir.is_dir()
        and any(model_dir.glob("*.safetensors"))
        and any(model_dir.glob("*.json"))
    )


def get_model_dir(model_id=MODEL_ID, model_root=None):
    model_name = model_id.split("/")[-1]
    if model_root is not None:
        return Path(model_root) / model_name

    for gemma_dir in candidate_gemma_dirs():
        model_dir = gemma_dir / ".model" / model_name
        if is_model_downloaded(model_dir):
            return model_dir
    return MODEL_ROOT / model_name


def find_asset(filename):
    for gemma_dir in candidate_gemma_dirs():
        path = gemma_dir / filename
        if path.exists():
            return path
    return GEMMA_DIR / filename


def download_model(model_id=MODEL_ID, model_dir=None):
    model_dir = Path(model_dir) if model_dir is not None else get_model_dir(model_id)
    if is_model_downloaded(model_dir):
        print(f"Model already exists in {model_dir}. Skipping download.")
        return model_dir

    print(f"Downloading {model_id} to {model_dir}...")
    snapshot_download(
        repo_id=model_id,
        local_dir=model_dir,
        allow_patterns=[
            "*.safetensors",
            "*.json",
            "tokenizer*",
            "*.model",
        ],
        ignore_patterns=[
            "*.bin",
            "*.h5",
            "*.msgpack",
            "*.onnx",
            "*.pt",
        ],
    )
    print(f"Download complete: {model_dir}")
    return model_dir


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-id", default=MODEL_ID)
    parser.add_argument("--model-root", type=Path, default=MODEL_ROOT)
    args = parser.parse_args()
    download_model(
        model_id=args.model_id,
        model_dir=get_model_dir(args.model_id, args.model_root),
    )
