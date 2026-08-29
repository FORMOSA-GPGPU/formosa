# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Export Gemma tokenizer vocabulary to a flat text file.

One UTF-8 hex-encoded token piece per line, in token-ID order. Hex encoding
keeps tokens containing newlines from shifting later token IDs.
"""

import argparse
from pathlib import Path

from transformers import AutoTokenizer

from download_model import find_asset, get_model_dir

SCRIPT_DIR = Path(__file__).resolve().parent
GEMMA_DIR = SCRIPT_DIR.parent if SCRIPT_DIR.name == "scripts" else SCRIPT_DIR


def export_vocab(output_file=None):
    output_file = (
        Path(output_file) if output_file is not None else find_asset("vocab.dump")
    )

    tokenizer = AutoTokenizer.from_pretrained(get_model_dir())
    pieces = tokenizer.convert_ids_to_tokens(range(tokenizer.vocab_size))

    with open(output_file, "w", encoding="ascii") as f:
        for piece in pieces:
            f.write(piece.encode("utf-8").hex() + "\n")

    print(f"Exported {len(pieces)} tokens to {output_file}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=find_asset("vocab.dump"))
    args = parser.parse_args()
    export_vocab(output_file=args.output)


if __name__ == "__main__":
    main()
