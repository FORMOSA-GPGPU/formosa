#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SIMTIX_SM=simtix/src/cores

XFORMOSA=$SIMTIX_SM/xformosa.txt
RISCV_OPCODES=third-party/riscv-opcodes
TARGET=$RISCV_OPCODES/extensions/unratified/rv_xformosa

ENCODING_OUT_H=$RISCV_OPCODES/encoding.out.h
ENCODING_H=$SIMTIX_SM/encoding.h

trap 'rm -f "$TARGET"' EXIT
cp "$XFORMOSA" "$TARGET"
(
  cd "$RISCV_OPCODES"
  PYTHONPATH=src python - <<'PY'
from pathlib import Path
import re

from riscv_opcodes.c_utils import make_c
from riscv_opcodes.constants import emitted_pseudo_ops
from riscv_opcodes.shared_utils import create_inst_dict

# Keep alias selection local to Formosa; use upstream encoding definitions.
aliases = [*emitted_pseudo_ops, "ntl_p1", "ntl_pall", "ntl_s1", "ntl_all"]
instructions = create_inst_dict(
    ["rv*", "unratified/rv*"], include_pseudo_ops=aliases
)
make_c(dict(sorted(instructions.items())))

# Public exports flatten submodules, where Git reports the superproject's
# revision instead. Keep the provenance comment stable in both layouts.
output = Path("encoding.out.h")
output.write_text(re.sub(
    r"(https://github\.com/riscv/riscv-opcodes) \([^\n]*\)",
    r"\1",
    output.read_text(),
    count=1,
))
PY
)
cp "$ENCODING_OUT_H" "$ENCODING_H"
