#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Record CPU OpenCL reference outputs from separately compiled Rodinia hosts.

Build separate hosts from tests/opencl/rodinia/opencl.
Use -O3 -std=c++11 -DTIMING, link -lm, and compile the original util/timing.c
as a verbatim .cpp translation unit. DWT2D also needs -DOUTPUT. Compile
tests/opencl/rodinia/readback.c as a shared library linked with -ldl.

Link the hosts against a CPU OpenCL ICD and select its library paths in the
environment. Never generate references with Formosa's runtime. Supply the
two hosts, readback library, and backend/compiler description through the
options below; review regenerated outputs before replacing checked-in files.
"""

import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from generate_inputs import generate
from reference import unpack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--particlefilter", required=True, type=Path)
    parser.add_argument("--dwt2d", required=True, type=Path)
    parser.add_argument("--readback-library", required=True, type=Path)
    parser.add_argument(
        "--backend", required=True, help="CPU ICD version and host compiler"
    )
    parser.add_argument(
        "--output", type=Path, default=Path(__file__).parent / "references"
    )
    args = parser.parse_args()
    support = Path(__file__).resolve().parent
    upstream = support
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [
        (
            "particlefilter",
            ["-x", "16", "-y", "16", "-z", "3", "-np", "256"],
            "f",
            0.003,
            0,
        ),
        (
            "dwt2d",
            ["-D", "32x32", "-l", "1", "-5", "input.rgb", "result.dwt"],
            "i",
            0,
            0,
        ),
    ]
    for kind, command_args, code, absolute, relative in cases:
        with tempfile.TemporaryDirectory(prefix=f"rodinia-cpu-{kind}-") as directory:
            work = Path(directory)
            generate(work)
            for file in (upstream / "opencl" / kind).rglob("*"):
                if file.is_file() and file.suffix in [".cl", ".h"]:
                    destination = work / file.relative_to(upstream / "opencl" / kind)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(file, destination)
            reads = work / "readbacks"
            reads.mkdir()
            environment = dict(
                os.environ,
                LD_PRELOAD=str(args.readback_library.resolve()),
                FORMOSA_BENCHMARK_READBACK=str(reads),
            )
            subprocess.run(
                [str(getattr(args, kind).resolve()), *command_args],
                cwd=work,
                env=environment,
                check=True,
                timeout=120,
            )
            files = sorted(reads.glob("readback-*.bin"))
            if len(files) != (9 if kind == "particlefilter" else 3):
                raise ValueError(f"incomplete CPU readbacks for {kind}")
            reference = {
                "SPDX-FileCopyrightText": "2026 CASLab, National Cheng Kung University",
                "SPDX-License-Identifier": "Apache-2.0",
                "backend": args.backend,
                "arguments": command_args,
                "arrays": [],
            }
            for index, file in enumerate(files):
                weight = kind == "particlefilter" and index % 3 == 2
                reference["arrays"].append(
                    {
                        "type": code,
                        "absolute_tolerance": 1e-5 if weight else absolute,
                        "relative_tolerance": 1e-3 if weight else relative,
                        "values": unpack(file, code),
                    }
                )
            if kind == "dwt2d":
                reference["outputs"] = {
                    suffix: base64.b64encode(
                        (work / ("result.dwt" + suffix)).read_bytes()
                    ).decode()
                    for suffix in [".r", ".g", ".b"]
                }
            (args.output / f"{kind}.json").write_text(
                json.dumps(reference, indent=2) + "\n"
            )


if __name__ == "__main__":
    main()
