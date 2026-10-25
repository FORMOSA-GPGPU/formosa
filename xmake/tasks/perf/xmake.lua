-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

task("perf", function()
  set_category("plugin")
  set_menu({
    usage = "xmake perf [options] [TEST-PATTERNS] [-- RUNNER-OPTIONS]",
    description = [[Run native OpenCL tests and automatically collect OUT/result.json.
Configure first: xmake f -m release --workflow=opencl -o build-opencl
Selected targets are built automatically. Default: all non-smoke pipelined benchmarks.

  xmake perf -o outputs/perf/base
  xmake perf --ci -o outputs/perf/ci
  xmake perf -j1 'opencl-kmeans/*'
  xmake perf --ci -g opencl/shoc
  xmake perf --ci -o outputs/perf/gto -- --sm-param=/tmp/gto.lua
  xmake perf collect -i outputs/perf/base -o outputs/perf/base/result.json
  xmake perf compare base.json current.json -o outputs/perf/comparison

Run xmake perf collect --help or xmake perf compare --help for command options.]],
    options = {
      { "o", "out", "kv", "outputs/perf", "Output directory; includes result.json." },
      { "g", "group", "kv", nil, "Native Xmake test group pattern." },
      { nil, "ci", "k", nil, "Select CI cases; intersect with group and test-name patterns." },
      { "j", "jobs", "kv", tostring(os.default_njob()), "Native build/test concurrency." },
      { nil, "arguments", "vs", nil, "Native test-name patterns, or collect/compare." },
    },
  })
  on_run(function() import("perf.main").run() end)
end)
