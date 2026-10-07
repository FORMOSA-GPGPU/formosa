#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Embed SHOC kernels using the original config/targets.mk.in conversion."""

from pathlib import Path
import sys


source, destination, symbol = sys.argv[1:]
text = Path(source).read_bytes().replace(b"\r", b"").decode("utf-8")
lines = text.split("\n")
if lines[-1] == "":
    lines.pop()
literals = [
    '"' + line.replace("\\", "\\\\").replace('"', '\\"') + '\\n"\n' for line in lines
]
Path(destination).write_text(f"const char *{symbol} =\n" + "".join(literals) + ";\n")
