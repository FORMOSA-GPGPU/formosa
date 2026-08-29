# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Download Gemma and export the OpenCL model/vocabulary artifacts."""

import argparse
from pathlib import Path

from download_model import MODEL_ID, MODEL_ROOT, download_model, get_model_dir
from export_model import export_model
from export_vocab import export_vocab

SCRIPT_DIR = Path(__file__).resolve().parent
GEMMA_DIR = SCRIPT_DIR.parent if SCRIPT_DIR.name == "scripts" else SCRIPT_DIR


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-id", default=MODEL_ID)
    parser.add_argument("--model-root", type=Path, default=MODEL_ROOT)
    parser.add_argument(
        "--model-output",
        type=Path,
        default=GEMMA_DIR / "gemma-3-270m.bin",
    )
    parser.add_argument("--vocab-output", type=Path, default=GEMMA_DIR / "vocab.dump")
    args = parser.parse_args()

    model_dir = get_model_dir(args.model_id, args.model_root)
    download_model(model_id=args.model_id, model_dir=model_dir)
    export_model(model_id=args.model_id, output_file=args.model_output)
    export_vocab(output_file=args.vocab_output)


if __name__ == "__main__":
    main()
