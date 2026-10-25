#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

from functools import cache
import hashlib
import json
import math
import os
import tomllib
from pathlib import Path
import sys


METRICS = {
    "active_cycles": "System.PipelinedSM0.PipelinedCore.active_cycles",
    "ipc": "System.PipelinedSM0.PipelinedCore.ipc",
    "total_instret": "System.PipelinedSM0.PipelinedCore.total_instret",
    "dcache_hit_rate": "System.PipelinedSM0.DCache.hit_rate",
    "dcache_total_misses": "System.PipelinedSM0.DCache.total_misses",
    "icache_hit_rate": "System.PipelinedSM0.ICache.hit_rate",
    "icache_total_misses": "System.PipelinedSM0.ICache.total_misses",
}


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


@cache
def kernel_inputs(directory):
    root = Path(directory)
    return {
        str(path.relative_to(root)): digest(path)
        for pattern in ("**/*.cl", "**/*.h")
        for path in sorted(root.glob(pattern))
    }


def case_identity(case):
    # Build/source locations may differ across CI jobs; datasets must not.
    def portable(value):
        for key in ("build_dir", "source_dir"):
            value = value.replace(case[key], f"<{key}>")
        return value

    inputs = {}
    cwd = Path(case["rundir"])

    def argument(arg, role):
        values = []
        for index, value in enumerate(arg.split(",")):
            path = Path(value)
            if not path.is_absolute():
                path = cwd / path
            if path.is_file() and str(path.resolve()) not in case.get("outputs", {}):
                key = f"{role}/{index}:{path.name}"
                inputs[key] = digest(path)
                value = f"<input:{key}>"
            values.append(portable(value))
        return ",".join(values)

    args = [argument(arg, f"arg/{index}") for index, arg in enumerate(case["args"])]
    references = [
        arg.removeprefix("--reference-arg=")
        for arg in case["wrapper"]
        if arg.startswith("--reference-arg=")
    ]
    for index, arg in enumerate(references):
        argument(arg, f"reference/{index}")
    for index, directory in enumerate(sorted(set(case.get("input_dirs", [str(cwd)])))):
        inputs.update(
            {
                f"source/{index}/{key}": value
                for key, value in kernel_inputs(directory).items()
            }
        )
    return {
        "args": args,
        "config": case["config"],
        "inputs": inputs,
    }


def validate_metrics(metrics):
    if not metrics:
        raise SystemExit("No simulator metrics found")
    for case, values in metrics.items():
        for key in ("active_cycles", "total_instret"):
            value = values.get(key)
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise SystemExit(f"{case}: missing or invalid simulator metric {key}")
        cycles, instructions, ipc = (
            values.get(key) for key in ("active_cycles", "total_instret", "ipc")
        )
        # Compile/transfer benchmarks can pass without launching any GPU work.
        if cycles == instructions == 0 and ipc is None and "ipc" in values:
            continue
        if (
            type(ipc) not in (int, float)
            or not math.isfinite(ipc)
            or ipc < 0
            or (instructions > 0 and (cycles == 0 or ipc == 0))
        ):
            raise SystemExit(f"{case}: missing or invalid simulator metric ipc")


def normalize_value(value):
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value


def numeric_stats(data, prefix=""):
    result = {}
    for name, value in data.items():
        key = f"{prefix}.{name}" if prefix else name
        if isinstance(value, dict):
            if "val" not in value:
                result.update(numeric_stats(value, key))
                continue
            value = value["val"]
        if type(value) in (int, float):
            result[key] = normalize_value(value)
    return result


def collect(input_dir, cases=None):
    metrics = {}
    if cases is None:
        paths = sorted(input_dir.rglob("stats.toml")) or sorted(
            input_dir.glob("*.toml")
        )
    else:
        # A saved run owns its recorded cases, including when another run is nested.
        native = (input_dir / "cases.json").is_file()
        paths = [
            input_dir / name / "stats.toml" if native else input_dir / (name + ".toml")
            for name in sorted(cases)
        ]
        paths = [path for path in paths if path.is_file()]
    if not paths:
        raise SystemExit(f"No TOML stats files found in {input_dir}")

    for path in paths:
        with path.open("rb") as f:
            data = tomllib.load(f)
        test_metrics = numeric_stats(data)
        for name, dotted_path in METRICS.items():
            if dotted_path in test_metrics:
                test_metrics[name] = test_metrics[dotted_path]
        key = (
            path.parent.relative_to(input_dir).as_posix()
            if path.name == "stats.toml"
            else path.stem
        )
        metrics[key] = test_metrics
    validate_metrics(metrics)
    return metrics


def env_first(*names, default=None):
    for name in names:
        value = os.getenv(name)
        if value:
            return value
    return default


def build_result(input_dir):
    kernel_inputs.cache_clear()
    record_path = input_dir / "cases.json"
    if not record_path.is_file():
        record_path = input_dir.parent / "cases.json"
    if not record_path.is_file():
        raise SystemExit("Missing successful perf run metadata; rerun the perf driver")
    record = json.loads(record_path.read_text())
    if "cases" not in record:
        entries = [
            json.loads(path.read_text())
            for path in sorted(input_dir.rglob("case.json"))
        ]
        if (
            not entries
            or any(not case["passed"] for case in entries)
            or len({case["sm"] for case in entries}) != 1
        ):
            raise SystemExit(
                "No successful cases, failed validation or mixed SMs in perf run"
            )
        record["sm"] = entries[0]["sm"]
        record["cases"] = {case["name"]: case_identity(case) for case in entries}
        record["runner_inputs"] = {
            str(index): {"path": value, "sha256": digest(Path(value))}
            for index, arg in enumerate(record["runner_args"])
            if Path(value := arg.split("=", 1)[-1]).is_file()
        }
    metrics = collect(input_dir, record["cases"])
    if set(metrics) != set(record["cases"]):
        raise SystemExit("Stats do not match the recorded perf case set")
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return {
        "schema_version": 2,
        "suite": {"sm": record["sm"], "cases": record["cases"]},
        "run": {
            key: record[key]
            for key in ("wall_seconds", "sm_param", "runner_args", "runner_inputs")
            if key in record
        },
        "commit": os.getenv("CI_COMMIT_SHA"),
        "branch": env_first("CI_COMMIT_BRANCH", "CI_COMMIT_REF_NAME"),
        "pipeline_id": os.getenv("CI_PIPELINE_ID"),
        "job_id": os.getenv("CI_JOB_ID"),
        "job_url": os.getenv("CI_JOB_URL"),
        "metrics": metrics,
    }


if __name__ == "__main__":
    request = json.loads(Path(sys.argv[1]).read_text())
    output = Path(request["out"])
    if request["command"] == "collect":
        result = build_result(Path(request["input"]))
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(f"Collected {len(result['metrics'])} cases: {output}")
    elif request["command"] == "compare":
        from compare_opencl_perf import compare

        compare(
            Path(request["baseline"]),
            Path(request["current"]),
            output,
            request.get("metric"),
        )
    else:
        raise SystemExit("Unknown stats command")
