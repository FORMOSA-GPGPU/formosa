-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

option("dlprim-tests", function()
  set_default(false)
  set_showmenu(true)
  set_description("Enable the dlprim simulator smoke test")
end)

if not has_config("dlprim-tests") then return end

target("opencl-smoke-pytorch_dlprim", function()
  set_kind("shared")
  set_basename("dlprim_monitor")
  set_group("opencl/dlprim")
  set_languages("c++20")
  add_rules("build.no-coverage")
  add_files("monitor.cc")
  add_defines("_GLIBCXX_USE_CXX11_ABI=1", "CL_TARGET_OPENCL_VERSION=120")
  add_links("torch", "torch_cpu", "c10", "dl")
  add_packages("opencl")
  add_deps("lv", "fw.runfiles", { inherit = false })
  add_tests("simtix.atomic_sm", { rundir = os.projectdir(), run_timeout = 120000 })
  on_load(function(target)
    local torch = assert(os.getenv("TORCH_ROOT"), "Set TORCH_ROOT to the LibTorch installation")
    local torchlib = path.join(torch, "lib")
    target:add(
      "sysincludedirs",
      path.join(torch, "include"),
      path.join(torch, "include/torch/csrc/api/include")
    )
    target:add("linkdirs", torchlib)
    target:add("rpathdirs", torchlib)
  end)
  on_test(function(target, opt)
    local config = import("core.project.config")
    local output = path.absolute(path.join(config.builddir(), "testing-out", opt.name))
    os.mkdir(output)
    opt.runenvs = table.join(opt.runenvs or {}, {
      POCL_EXTRA_BUILD_FLAGS = "-cl-std=CL1.2 -DCUSTOM_REDUCE=1",
      POCL_CACHE_DIR = path.join(output, "pocl-cache"),
      DLPRIM_CACHE_DIR = path.join(output, "dlprim-cache"),
    })
    return import("testing").run_with(path.absolute(target:dep("lv"):targetfile()), {
      path.join(os.projectdir(), "tests/opencl/run_opencl.lua"),
      "--sm",
      "simtix.atomic_sm",
      "--config",
      "large_sm",
      "-s",
      path.join(output, "stats.toml"),
      "-t",
      path.join(output, "trace"),
      "--",
      "env",
      "LD_PRELOAD=" .. path.absolute(target:targetfile()),
      "python3",
      path.join(target:scriptdir(), "smoke.py"),
    }, opt)
  end)
end)
