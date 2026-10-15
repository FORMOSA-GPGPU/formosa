# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Validate the complete bounded SHOC level-zero integration workloads."""

import math
import re


KINDS = {
    "busspeeddownload",
    "busspeedreadback",
    "devicememory",
    "kernelcompile",
    "queuedelay",
    "queuedelay_default",
    "maxflops",
}


def validate(kind, output):
    if kind.startswith("busspeed"):
        direction = "Download" if kind.endswith("download") else "Readback"
        sizes = re.findall(rf"^{direction} validation passed: (\d+) KiB$", output, re.M)
        if sizes != ["1", "2", "4", "8", "16"]:
            raise ValueError("missing transfer contents validation")
        expected = {
            (direction + metric, f"{size}kB")
            for metric in ("Speed", "Delay", "Time")
            for size in (1, 2, 4, 8, 16)
        }
    elif kind == "devicememory":
        if len(re.findall(r"^Test Passed$", output, re.M)) != 13:
            raise ValueError("missing memory contents comparisons")
        expected = {
            (name, "GrpSize:032")
            for name in (
                "readConstantMemoryCoalesced",
                "readGlobalMemoryCoalesced",
                "readGlobalMemoryUnit",
                "readLocalMemory",
                "writeGlobalMemoryCoalesced",
                "writeGlobalMemoryUnit",
                "writeLocalMemory",
            )
        } | {
            ("TextureRepeated" + pattern, f"{size}kB")
            for pattern in ("CacheHit", "LinearAccess", "RandomAccess")
            for size in (16, 64)
        }
    elif kind == "kernelcompile":
        expected = {
            (metric, kernel)
            for metric in (
                "CreateFromSource",
                "BuildProgram",
                "ExtractKernel",
                "Cleanup",
            )
            for kernel in ("short_kernel", "long_kernel")
        }
    elif kind in {"queuedelay", "queuedelay_default"}:
        expected = {("SSDelay", label) for label in ("1Kernel", "2Kernels", "4Kernels")}
    elif kind == "maxflops":
        names = {
            operation + str(lanes)
            for operation in ("Add", "Mul", "MAdd", "MulMAdd")
            for lanes in (1, 2, 4, 8, 16)
            if (operation, lanes) != ("MulMAdd", 16)
        } | {"MAddU", "MulMAddU"}
        names = {name + precision for name in names for precision in ("-SP", "-DP")}
        checked = re.findall(r"^Arithmetic validation passed: (\S+)$", output, re.M)
        if len(checked) != len(names) or set(checked) != names:
            raise ValueError("missing MaxFlops CPU arithmetic comparisons")
        expected = {
            (name, "Size:2048" if "U-" in name else "Size:0002048") for name in names
        }
    else:
        raise ValueError(f"unknown level-zero benchmark {kind}")

    seen = set()
    for line in output.splitlines():
        fields = line.rstrip().split("\t")
        if len(fields) < 4 or fields[0] == "test":
            continue
        key = fields[0], fields[1].strip()
        if key not in expected or key in seen:
            raise ValueError(f"unexpected or duplicate metric {key}")
        values = [float(value) for value in fields[3:]]
        trials = 3 if kind == "queuedelay_default" else 1
        if len(values) != 5 + trials or any(
            not math.isfinite(v) or v < 0 for v in values
        ):
            raise ValueError(f"invalid or incomplete metric {key}")
        seen.add(key)
    if seen != expected:
        raise ValueError("benchmark did not complete every timing measurement")
