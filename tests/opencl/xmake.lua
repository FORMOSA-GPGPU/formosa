-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

add_requires("pkgconfig::OpenCL", { alias = "opencl", system = true })

rule("opencl.host", function()
  -- Coverage measures the simulator/runtime, not workload host harnesses.
  add_deps("build.no-coverage")
  on_load(function(target)
    target:set("kind", "binary")
    target:add("packages", "opencl")
    target:add(
      "defines",
      "CL_HPP_TARGET_OPENCL_VERSION=120",
      "CL_HPP_MINIMUM_OPENCL_VERSION=120",
      "CL_HPP_ENABLE_EXCEPTIONS=ON",
      "CL_HPP_CL_1_2_DEFAULT_BUILD"
    )
  end)
end)

-- A benchmark owns its host executable, while LV runs it under the selected SM.
-- Optional validators wrap the same command and retain their numerical oracle.
rule("opencl.benchmark", function()
  on_load(function(target) target:add("deps", "lv", "fw.runfiles", { inherit = false }) end)
  on_test(function(target, opt)
    import("core.project.config")
    local output = path.absolute(path.join(config.builddir(), "testing-out", opt.name))
    os.mkdir(output)
    local argv = {
      path.join(os.projectdir(), "tests/opencl/run_opencl.lua"),
      "-s",
      path.join(output, "stats.toml"),
      "-t",
      path.join(output, "trace"),
      "--sm",
      opt.sm,
    }
    if opt.config then table.join2(argv, { "--config", opt.config }) end
    table.join2(argv, { "--", path.absolute(target:targetfile()) }, opt.hostargs or {})
    opt.runenvs =
      table.join(opt.runenvs or {}, { POCL_CACHE_DIR = path.join(output, "pocl-cache") })
    local binary = path.absolute(target:dep("lv"):targetfile())
    if opt.wrapper then
      return import("testing").run_with(
        "python3",
        table.join(opt.wrapper, { "--", binary }, argv),
        opt
      )
    end
    return import("testing").run_with(binary, argv, opt)
  end)
end)

target("run-opencl", function()
  add_rules("lv.script", { script = "run_opencl.lua" })
  add_deps("fw.runfiles", { inherit = false })
end)

-- Cases belong to their host executable. The perf manifest uses the same native
-- test metadata; workflow membership remains in workflows.lua selectors.
local function program(name, directory, cases, sources)
  target("opencl-" .. name, function()
    set_group("opencl/" .. directory:match("^[^/]+"))
    add_rules("opencl.host", "opencl.benchmark")
    for _, source in ipairs(sources or { "main.cc" }) do
      add_files(directory .. "/" .. source)
    end
    add_defines(
      "CL_TARGET_OPENCL_VERSION=" .. (directory == "smoke/subgroup_info" and "300" or "120")
    )
    if os.isfile(path.join(os.scriptdir(), directory, "kernel.cl")) then
      add_defines('KERNEL_PATH="' .. path.join(os.scriptdir(), directory, "kernel.cl") .. '"')
    end
    after_load(function(target)
      local variants = type(cases) == "function" and cases(target) or cases or { { "" } }
      local perf_cases = {}
      for _, case in ipairs(variants) do
        local variant = case[1]
        local hostargs = table.slice(case, 2)
        local id = name .. (variant ~= "" and "_" .. variant or "")
        if not directory:startswith("smoke/") then
          table.insert(perf_cases, table.concat(table.join({ id, target:name() }, hostargs), "\t"))
        end
        for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
          target:add("tests", (variant ~= "" and variant .. "/" or "") .. sm, {
            sm = sm,
            hostargs = hostargs,
            perf_id = not directory:startswith("smoke/") and id or nil,
            rundir = os.projectdir(),
          })
        end
      end
      target:data_set("opencl.perf-cases", perf_cases)
    end)
  end)
end

for _, name in ipairs({
  "activation",
  "attention",
  "batched_gemm",
  "bias_relu",
  "conv2d",
  "embedding",
  "gemm",
  "gru",
  "gustavson_gemm",
  "image_blur",
  "layernorm",
  "lmem_bank_parallel",
  "pooling",
  "prefix_sum",
  "residual_mlp",
  "rmsnorm",
  "softmax",
  "swiglu",
  "transformer_block",
  "transpose",
  "vecadd",
  "vecmul",
}) do
  program(name, "formosa/" .. name)
end

program("mandelbrot", "formosa/mandelbrot", { { "", "256" } })
program("montecarlo", "formosa/montecarlo", { { "", "65536" }, { "seeded", "4099", "4294967295" } })

program(
  "smoke-atomic_ops",
  "smoke/atomic_ops",
  function(target)
    return {
      { "" },
      { "spirv", path.join(target:dep("opencl.atomic-spirv"):autogendir(), "c11.spv") },
    }
  end
)
includes("smoke/atomic_ops/xmake.lua")

for _, name in ipairs({
  "copy_buffer",
  "divergence",
  "inactive_lanes",
  "kernel_clock",
  "kernel_args",
  "lmem_allocate",
  "memory_ops",
  "oclprintf",
  "spirv_smoke",
  "subgroup_info",
}) do
  program("smoke-" .. name, "smoke/" .. name)
end

-- Fetch the existing pinned benchmark inputs only when SpMV is built.
target("opencl.spmv-data", function()
  set_kind("phony")
  on_build(function(target)
    import("net.http")
    import("utils.archive")
    local inputs = {
      {
        "https://wiki.caslab.ee.ncku.edu.tw/static-sites/files/spmv/data/msc00726.tar.gz",
        "msc00726",
        "3b5cf6684c8f6f264ff5d67988aa07c327ff5be6286bfe11b00f482e2c45578c",
      },
      {
        "https://wiki.caslab.ee.ncku.edu.tw/static-sites/files/spmv/data/dc2.tar.gz",
        "dc2",
        "14e039beb453b1d4c9c94471f3a699b18acafb5b0e508e3cb7cf5cba1f4288d1",
      },
    }
    local output = target:autogendir()
    for _, input in ipairs(inputs) do
      local url, name, checksum = table.unpack(input)
      if not os.isfile(path.join(output, name, name .. ".mtx")) then
        os.mkdir(output)
        local tarball = path.join(output, name .. ".tar.gz")
        if not os.isfile(tarball) then http.download(url, tarball) end
        assert(hash.sha256(tarball) == checksum, "SpMV archive checksum mismatch: " .. tarball)
        archive.extract(tarball, output)
      end
    end
  end)
end)
program("spmv", "formosa/spmv", function(target)
  local data = path.absolute(target:dep("opencl.spmv-data"):autogendir(), os.projectdir())
  return {
    { "regular", path.join(data, "msc00726/msc00726.mtx") },
    { "irregular", path.join(data, "dc2/dc2.mtx") },
  }
end)
target("opencl-spmv", function() add_deps("opencl.spmv-data") end)

-- Gemma tools have no regression cases in the existing build.
for _, name in ipairs({ "gemma", "verify_layer_e2e", "verify_layer_breakdown", "verify_full" }) do
  target("opencl-" .. name, function()
    add_rules("opencl.host")
    set_languages("c++20")
    add_files(
      "gemma/" .. name .. ".cc",
      "gemma/model_loader.cc",
      "gemma/gemma_transformer_layer.cc"
    )
    add_defines(
      "CL_TARGET_OPENCL_VERSION=120",
      'GEMMA_ROOT="' .. path.join(os.scriptdir(), "gemma/kernels") .. '"',
      'GEMMA_ASSET_ROOT="' .. path.join(os.scriptdir(), "gemma") .. '"'
    )
  end)
end

includes("rodinia/xmake.lua", "parboil/xmake.lua", "shoc/xmake.lua")

target("opencl.perf-cases", function()
  set_kind("phony")
  set_default(true)
  on_build(function()
    import("core.project.project")
    import("core.project.config")
    local entries = {}
    for _, target in ipairs(project.ordertargets()) do
      for _, entry in ipairs(target:data("opencl.perf-cases") or {}) do
        table.insert(entries, entry)
      end
    end
    table.sort(entries)
    local output = path.join(config.builddir(), "tests/opencl/opencl_perf_cases.tsv")
    os.mkdir(path.directory(output))
    io.writefile(output, table.concat(entries, "\n") .. "\n")
  end)
end)
