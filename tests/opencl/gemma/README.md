<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# Gemma OpenCL Tests

This directory integrates the Gemma OpenCL implementation into the FORMOSA
`tests/opencl` test tree. It provides three entry points:

1. `verify_layer_breakdown`: verifies one transformer layer stage by stage
   against Hugging Face/PyTorch reference tensors.
2. `verify_layer_e2e`: verifies the input and output hidden states of one
   complete transformer layer.
3. `verify_full`: manually verifies full-model forward results across multiple
   tokens, including final hidden states, final norm, logits, and argmax token
   IDs.

The `gemma` binary is also kept as a simple autoregressive generation smoke
path. It is meant to prove that the whole OpenCL inference path can run, not to
benchmark language quality.

## Layout

- C++ sources live in this directory.
- Python model/export/verification helpers live in `scripts/`.
- OpenCL kernels live in `kernels/`.
- Local model artifacts stay at this directory root, but are ignored by git.

## Nix Shell

Enter the FORMOSA shell from the repository root:

```bash
nix develop
```

The default shell includes the existing FORMOSA OpenCL/LLVM/pocl environment
plus:

- `torch`
- `transformers`
- `huggingface-hub`
- `numpy`
- `sentencepiece`
- `safetensors`

If this is the first time downloading the Gemma model, make sure your
Hugging Face account has accepted the model license and that credentials are
available in the shell:

Accept the model license at https://huggingface.co/google/gemma-3-270m, then

```bash
# install Hugging Face CLI if not already available
curl -LsSf https://hf.co/cli/install.sh | bash

# login to Hugging Face to access the model snapshot
huggingface-cli login
```

## Prepare Model Files

Prepare all model artifacts with:

```bash
python tests/opencl/gemma/scripts/prepare_model.py
```

The individual scripts remain available when you only need one step:

```bash
python tests/opencl/gemma/scripts/download_model.py
python tests/opencl/gemma/scripts/export_model.py
python tests/opencl/gemma/scripts/export_vocab.py
```

After export, these files should exist:

- Hugging Face model snapshot: `tests/opencl/gemma/.model/`
- Exported OpenCL weights: `tests/opencl/gemma/gemma-3-270m.bin`
- Exported tokenizer vocabulary: `tests/opencl/gemma/vocab.dump`

## Build

From the repository root:

```bash
cmake --preset integration-test
cmake --build build --target gemma verify_layer_breakdown verify_layer_e2e verify_full
```

## Verification

Start the FORMOSA daemon in the background if not already running:

```bash
./build/bin/lv tests/formosa/daemon.lua
```

Run all layer-breakdown stages manually:

```bash
python tests/opencl/gemma/scripts/verify_layer_breakdown.py \
  --bin build/bin/verify_layer_breakdown
```

By default, C++ intermediate tensors are written into a temporary directory and
removed after the Python comparison finishes.

Run one stage only:

```bash
python tests/opencl/gemma/scripts/verify_layer_breakdown.py \
  --bin build/bin/verify_layer_breakdown \
  --stage q_rope
```

Keep intermediate files for debugging:

```bash
python tests/opencl/gemma/scripts/verify_layer_breakdown.py \
  --bin build/bin/verify_layer_breakdown \
  --stage q_rope \
  --dump-dir /tmp/gemma-breakdown-debug
```

Run the manual single-layer input/output check:

```bash
python tests/opencl/gemma/scripts/verify_layer_e2e.py \
  --bin build/bin/verify_layer_e2e
```

Run full-model verification:

```bash
python tests/opencl/gemma/scripts/verify_full.py \
  --bin build/bin/verify_full
```

## Generation Smoke Run

Tokenize a prompt and pass token IDs to the `gemma` binary:

```bash
./build/bin/gemma \
  --max-new-tokens 8 \
  $(python tests/opencl/gemma/scripts/tokenize_prompt.py "Explain OpenCL in one sentence.")
```

## Notes

- `verify_layer_breakdown` is the best tool for localizing numerical errors
  inside one transformer block. By default it verifies all dumped stages in one
  simulator run; use `--stage` for single-stage debugging.
- `verify_full` is intentionally manual-only. Use it when changing the
  end-to-end forward path.
- `verify_layer_e2e` is intentionally kept as a manual middle-granularity
  check.
- Re-export `gemma-3-270m.bin` and `vocab.dump` after changing the model
  version.
