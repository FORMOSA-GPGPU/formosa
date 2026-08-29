# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Tokenize a prompt string and print space-separated token IDs to stdout.

Usage:
    python tokenize_prompt.py "Your prompt here"

The output (e.g. "1234 5678 910") can be passed directly to the gemma binary:
    ./gemma $(python tokenize_prompt.py "Your prompt here")
"""

import argparse

from transformers import AutoTokenizer

from download_model import get_model_dir


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("prompt")
    args = parser.parse_args()

    tokenizer = AutoTokenizer.from_pretrained(get_model_dir())

    # add_special_tokens=False: we prepend BOS manually in C++
    ids = tokenizer.encode(args.prompt, add_special_tokens=False)
    print(" ".join(str(t) for t in ids))


if __name__ == "__main__":
    main()
