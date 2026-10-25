#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Exercise selection, execution and comparison without compiling a simulator."""

import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile

from collect_opencl_stats import build_result, collect
from compare_opencl_perf import comparable_deltas, markdown_table


def main():
    repo = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="perf fixture ") as tmp:
        root = Path(tmp)
        build, out, cwd = root / "build", root / "out", root / "working directory"
        cwd.mkdir()
        data = cwd / "input with spaces.txt"
        data.write_text("deterministic input\n")
        (cwd / "kernel.cl").write_text("kernel fixture\n")
        wrapper = root / "verify.py"
        wrapper.write_text(
            "import subprocess, sys\n"
            'result = subprocess.run(sys.argv[sys.argv.index("--") + 1:])\n'
            "sys.exit(9 if 'wrong-result' in sys.argv else result.returncode)\n"
        )
        launcher = root / "fake-lv"
        launcher.write_text(
            """#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys, time
args = sys.argv[2:]
assert args[4:8] == ["--sm", "simtix.pipelined_sm", "--config", "large_sm"]
assert pathlib.Path.cwd().name == "working directory"
assert pathlib.Path("input with spaces.txt").read_text() == "deterministic input\\n"
stats = pathlib.Path(args[1])
stats.with_suffix(".argv").write_text(json.dumps(args))
assert pathlib.Path(os.environ["POCL_CACHE_DIR"]).parent == stats.parent
if args[-2] == "parallel":
    stats.with_suffix(".started").touch()
    while len(list(stats.parents[2].rglob("*.started"))) < 2: time.sleep(0.01)
if args[-2] == "timeout":
    subprocess.Popen([sys.executable, "-c", "import pathlib,time; time.sleep(1); pathlib.Path('orphan').touch()"])
    time.sleep(10)
if args[-2] == "fail": sys.exit(7)
pathlib.Path("result.txt").write_text(str(time.time_ns()))
if args[-2] != "no-stats":
    stats.write_text("[System.PipelinedSM0.PipelinedCore]\\nactive_cycles = 123\\nipc = 1.5\\ntotal_instret = 184\\navg_instr_shared = 2.0\\n")
    stats.write_text(stats.read_text() + "[Custom]\\nscalar = -2\\nzero = 0\\nflag = true\\ntext = 'ignore'\\n[Custom.counter]\\ndesc = 'custom metric'\\nval = 42\\n['custom/path']\\nvalue = 7\\n")
    if args[-2] == "zero": stats.write_text("[System.PipelinedSM0.PipelinedCore]\\nactive_cycles = 0\\nipc = nan\\ntotal_instret = 0\\navg_instr_shared = nan\\n")
    if args[-2] == "empty-metrics": stats.write_text("[Unrelated]\\ncounter = 1\\n")
"""
        )
        launcher.chmod(0o755)
        # Native targets and the production on_test adapter are used.
        cases = {"kmeans": "1", "shoc-scan": "2", "extra": "3", "smoke-probe": "4"}
        timeout = 2000

        def save():
            declarations = [
                f'add_moduledirs({json.dumps(str(repo / "xmake/modules"))})',
                f'includes({json.dumps(str(repo / "xmake/tasks/perf/xmake.lua"))})',
                'target("lv")',
                'set_kind("binary")',
                f"on_build(function(target) os.cp({json.dumps(str(launcher))}, target:targetfile()) end)",
                'rule("opencl.benchmark")',
                f'on_test(function(target, opt) return import("benchmark", {{rootdir={json.dumps(str(repo / "tests/opencl"))}}}).run(target, opt) end)',
            ]
            for name, arg in cases.items():
                declarations += [
                    f'target("opencl-{name}")',
                    'set_kind("binary")',
                    f'set_group("opencl/{name.split("-")[0] if "-" in name else "rodinia"}")',
                    'add_deps("lv")',
                    'add_rules("opencl.benchmark")',
                    'on_build(function(target) io.writefile(target:targetfile(), "built on demand") end)',
                    'add_tests("simtix.pipelined_sm", {sm="simtix.pipelined_sm", config="large_sm", '
                    f'hostargs={{{json.dumps(str(data))}, {json.dumps(arg)}, "result.txt"}}, rundir={json.dumps(str(cwd))}, '
                    f'wrapper={{{json.dumps(str(wrapper))}, "--output", "result.txt"}}, run_timeout={timeout}'
                    + "})",
                ]
                if name == "extra":
                    declarations += [
                        declarations[-1].replace(
                            'add_tests("simtix.pipelined_sm"',
                            'add_tests("variant/simtix.pipelined_sm"',
                        )
                    ]
                if name == "kmeans":
                    declarations += [
                        'add_tests("simtix.atomic_sm", {sm="simtix.atomic_sm"})'
                    ]
            (root / "xmake.lua").write_text("\n".join(declarations) + "\n")

        env = dict(
            os.environ,
            XMAKE_CONFIGDIR=str(root / ".xmake"),
            CI_COMMIT_SHA="fixture",
            CI_COMMIT_BRANCH="",
            CI_COMMIT_REF_NAME="fixture-branch",
        )
        save()
        subprocess.run(
            ["xmake", "f", "-y", "-m", "release", "-o", str(build)],
            cwd=root,
            env=env,
            check=True,
            capture_output=True,
        )

        def run(*args, success=True, jobs=2):
            save()
            result = subprocess.run(
                [
                    "xmake",
                    "perf",
                    "-o",
                    str(out),
                    *([f"-j{jobs}"] if jobs else []),
                    *args,
                ],
                cwd=root,
                env=env,
                capture_output=True,
                text=True,
                timeout=30,
            )
            assert (result.returncode == 0) == success, (
                result.stdout
                + result.stderr
                + "\n".join(p.read_text() for p in out.rglob("stderr.log"))
            )
            return result

        def names():
            return {key.split("/")[0].removeprefix("opencl-") for key in collect(out)}

        def argv(name="kmeans"):
            return json.loads(
                (out / f"opencl-{name}/simtix.pipelined_sm/stats.argv").read_text()
            )

        run()
        assert names() == {"kmeans", "shoc-scan", "extra"}
        assert "opencl-extra/variant/simtix.pipelined_sm" in collect(out)
        assert argv()[-3:] == [str(data), "1", "result.txt"]
        assert not list(
            build.rglob("*smoke-probe*")
        ), "Unselected targets must not be built"
        run("-g", "opencl/rodinia")
        assert names() == {"kmeans", "extra"}
        run("--ci", "-g", "opencl/rodinia")
        assert names() == {"kmeans"}
        run("--ci", "-g", "opencl/shoc")
        assert names() == {"shoc-scan"}
        run("--ci", "opencl-extra/*", success=False)
        run("--ci", "-g", "opencl/shoc", "opencl-kmeans/*", success=False)
        run("--ci")
        assert names() == {"kmeans", "shoc-scan"}
        baseline = build_result(out)
        written = json.loads((out / "result.json").read_text())
        assert written["commit"] == "fixture"
        assert written["branch"] == "fixture-branch"
        run("opencl-extra/*")
        assert names() == {"extra"}
        run("--ci", "-g", "opencl/rodinia", "opencl-kmeans/*")
        assert names() == {"kmeans"}
        run("opencl-kmeans/*", "opencl-extra/*")
        assert names() == {"kmeans", "extra"}
        run("missing/*", success=False)
        run("opencl-smoke-*/*", success=False)
        run("opencl-kmeans/simtix.atomic_sm", success=False)
        (build / "bin").mkdir(exist_ok=True)
        help_runner = build / "bin/run_opencl.lua"
        help_runner.write_text("#!/bin/sh\necho runner-help\n")
        help_runner.chmod(0o755)
        assert "runner-help" in run("--", "--help").stdout
        sm_param = root / "params.lua"
        sm_param.write_text('return { scheduler = "gto" }\n')
        run(
            "--ci",
            "--",
            "--sm-param",
            str(sm_param),
            "--heartbeat-frequency=0",
        )
        assert argv()[8:10] == ["--sm-param", str(sm_param)]
        current = build_result(out)
        assert baseline["suite"] == current["suite"]
        assert current["run"]["runner_inputs"]["1"]["sha256"]
        key = "opencl-kmeans/simtix.pipelined_sm"
        assert current["metrics"][key]["Custom.counter"] == 42
        assert current["metrics"][key]["Custom.scalar"] == -2
        assert "Custom.flag" not in current["metrics"][key]
        assert comparable_deltas(baseline, current, "Custom.zero")[0][-1] == 0
        current["metrics"][key]["Custom.counter"] = 84
        assert comparable_deltas(baseline, current, "Custom.counter")[0][-1] == 100
        assert "Custom.counter" in markdown_table(baseline, current, "Custom.counter")
        run("--", "--stats=override.toml", success=False)
        run("--", "-tunsafe", success=False)
        for arg in ("wrong-result", "fail", "no-stats", "empty-metrics", "timeout"):
            cases["kmeans"] = arg
            timeout = 100 if arg == "timeout" else 2000
            run("opencl-kmeans/*", success=False)
            assert not (out / "result.json").exists()
        import time

        time.sleep(1.1)
        assert not (cwd / "orphan").exists(), "Timeout left a validator child alive"
        cases["kmeans"], timeout = "1", 2000
        run("--ci")

        def refused(result):
            try:
                markdown_table(baseline, result)
            except SystemExit:
                return
            raise AssertionError("Incompatible comparison was accepted")

        refused(dict(schema_version=1, metrics=baseline["metrics"]))
        missing = copy.deepcopy(current)
        del missing["metrics"]["opencl-shoc-scan/simtix.pipelined_sm"]
        refused(missing)
        missing = copy.deepcopy(current)
        missing["metrics"]["opencl-kmeans/simtix.pipelined_sm"]["ipc"] = None
        refused(missing)
        # Recollection must retain the original run's input hashes after edits.
        (cwd / "kernel.cl").write_text("later local edit\n")
        saved_suite = json.loads((out / "result.json").read_text())["suite"]
        assert build_result(out)["suite"] == saved_suite
        (cwd / "kernel.cl").write_text("changed benchmark kernel\n")
        run("--ci")
        refused(build_result(out))
        (cwd / "kernel.cl").write_text("kernel fixture\n")
        run("--ci")

        def invoke(*args, success=True):
            result = subprocess.run(
                ["xmake", "perf", *map(str, args)],
                cwd=root,
                env=env,
                capture_output=True,
                text=True,
            )
            assert (result.returncode == 0) == success, (
                result.stdout
                + result.stderr
                + "\n".join(p.read_text() for p in out.rglob("stderr.log"))
            )
            return result

        saved = out / "recollected.json"
        invoke("collect", "-i", out, "-o", saved)
        assert json.loads(saved.read_text()) == json.loads(
            (out / "result.json").read_text()
        )
        comparison = out / "comparison"
        invoke("compare", saved, out / "result.json", "-o", comparison)
        assert (comparison / "comparison.md").is_file()
        assert all(
            (comparison / f"{metric}.png").stat().st_size > 0
            for metric in ("active_cycles", "ipc", "total_instret")
        )
        invoke("compare", saved, saved, "-o", comparison, "--metric=Custom.counter")
        assert (comparison / "Custom.counter.png").stat().st_size > 0
        assert "Custom.counter" in (comparison / "comparison.md").read_text()
        invoke("compare", saved, saved, "-o", comparison, "--metric=custom/path.value")
        assert (comparison / "custom_path.value.png").is_file()
        invoke("compare", saved, saved, "--metric=Custom.text", success=False)
        invoke("compare", saved, saved, "--metric=does.not.exist", success=False)
        invoke("compare", saved, saved, "--metric=icache_hit_rate", success=False)
        invalid = out / "legacy.json"
        invalid.write_text(json.dumps(dict(schema_version=1, metrics={})))
        invoke("compare", invalid, saved, success=False)
        cases["extra"] = "zero"
        run("opencl-extra/*")
        zero = root / "zero.json"
        zero.write_text((out / "result.json").read_text())
        invoke("compare", zero, zero, "-o", comparison)
        assert "N/A" in (comparison / "comparison.md").read_text()
        # A defined-but-undefined ratio must not erase the numeric metric path.
        metric = "System.PipelinedSM0.PipelinedCore.avg_instr_shared"
        run()
        mixed = root / "mixed.json"
        mixed.write_text((out / "result.json").read_text())
        invoke("compare", mixed, mixed, "-o", comparison, "--metric=" + metric)
        result = json.loads(mixed.read_text())
        assert result["metrics"]["opencl-extra/simtix.pipelined_sm"][metric] is None
        assert len(comparable_deltas(result, result, metric)) == 2
        # Missing numeric fields still fail rather than silently dropping cases.
        del result["metrics"]["opencl-kmeans/simtix.pipelined_sm"][metric]
        missing_metric = root / "missing-metric.json"
        missing_metric.write_text(json.dumps(result))
        invoke("compare", mixed, missing_metric, "--metric=" + metric, success=False)
        result = json.loads(mixed.read_text())
        result["metrics"]["opencl-extra/simtix.pipelined_sm"][metric] = "invalid"
        missing_metric.write_text(json.dumps(result))
        invoke("compare", mixed, missing_metric, "--metric=" + metric, success=False)
        nested = out / "gto/opencl-kmeans/simtix.pipelined_sm"
        nested.mkdir(parents=True)
        (nested / "stats.toml").write_bytes(
            (out / "opencl-kmeans/simtix.pipelined_sm/stats.toml").read_bytes()
        )
        assert build_result(out)["metrics"] == json.loads(mixed.read_text())["metrics"]
        assert "N/A" in (comparison / "comparison.md").read_text()
        # Neither entry point may impose a timeout when the case declares none.
        timeout = "nil"
        probe = root / "timeout-probe"
        probe.mkdir()
        (probe / "timeout").write_text(
            "#!/bin/sh\necho 'Unexpected timeout' >&2\nexit 97\n"
        )
        (probe / "timeout").chmod(0o755)
        env["PATH"] = str(probe) + os.pathsep + env["PATH"]
        run("opencl-kmeans/*")
        native = subprocess.run(
            ["xmake", "test", "-j1", "-g", "opencl/rodinia", "*/simtix.pipelined_sm"],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
        )
        assert native.returncode == 0, native.stdout + native.stderr
        assert len(list((build / "testing-out").rglob("stats.toml"))) == 2
        env["PATH"] = env["PATH"].split(os.pathsep, 1)[1]
        timeout = 2000
        # Native Xmake schedules both tiny hosts; the serial run must time out.
        cases = {"kmeans": "parallel", "shoc-scan": "parallel"}
        run(jobs=1, success=False)
        for marker in out.rglob("*.started"):
            marker.unlink()
        run(jobs=None)
        run("-j0", success=False)
        for command in (["--help"], ["collect", "--help"], ["compare", "--help"]):
            subprocess.run(
                ["xmake", "perf", *command],
                cwd=root,
                env=env,
                check=True,
                capture_output=True,
            )
    print("Native perf execution and comparison checks passed")


if __name__ == "__main__":
    main()
