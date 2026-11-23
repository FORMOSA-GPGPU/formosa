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
  on_test(
    function(target, opt) return import("benchmark", { rootdir = os.scriptdir() }).run(target, opt) end
  )
end)

option("opencl-qemu-all-tests", function()
  set_default(false)
  set_showmenu(true)
  set_description("Run every OpenCL test in the QEMU guest, not only the sample")
end)

-- Each guest run competes for the host's CPUs and slows the other tests, so by
-- default the guest runs a sample on one SM model: every smoke test, vecadd,
-- and a few programs (some with several kernels) from each suite.
local qemu_sample = {
  "opencl-vecadd",
  "opencl-activation",
  "opencl-batched_gemm",
  "opencl-rodinia-hotspot",
  "opencl-rodinia-kmeans",
  "opencl-rodinia-lud",
  "opencl-parboil-sgemm",
  "opencl-parboil-mri-q",
  "opencl-parboil-stencil",
  "opencl-shoc-scan",
  "opencl-shoc-reduction",
  "opencl-shoc-busspeeddownload",
  "opencl-shoc-busspeedreadback",
}

-- Runs the cases of the one OpenCL test target it depends on in a QEMU guest
-- against the device on a PCI bus (ilha/qemu.lua, x86_64 Linux only): the
-- target's own test script runs each case with `qemu` set. A case staged under
-- the build directory runs in a sibling copy of it, at the same depth for its
-- relative paths, so its outputs never meet a concurrent original run. Its
-- timeout is four times the original's.
rule("opencl.qemu", function()
  on_load(function(target) target:set("kind", "phony") end)
  after_load(function(target)
    import("core.project.config")
    local source = target:dep(table.wrap(target:get("deps"))[1])
    local builddir = path.absolute(config.builddir()) .. "/"
    local all = config.get("opencl-qemu-all-tests")
    local sampled = source:name():startswith("opencl-smoke-")
      or table.contains(qemu_sample, source:name())
    for _, name in ipairs(table.wrap(source:get("tests"))) do
      local conf = table.join(source:extraconf("tests", name) or {}, { qemu = true })
      if all or (sampled and conf.sm == "simtix.pipelined_sm") then
        -- Under CI's load the guest run took a median 3.6 times the original's.
        if conf.run_timeout then conf.run_timeout = conf.run_timeout * 4 end
        if conf.rundir and conf.rundir:startswith(builddir) then
          conf.staged, conf.rundir = conf.rundir, conf.rundir .. ".qemu"
        end
        target:add("tests", name, conf)
      end
    end
  end)
  on_build(function(target)
    for _, name in ipairs(table.wrap(target:get("tests"))) do
      local conf = target:extraconf("tests", name)
      if conf.staged then
        os.tryrm(conf.rundir)
        if os.isdir(conf.staged) then
          os.cp(conf.staged, conf.rundir)
        else
          os.mkdir(conf.rundir)
        end
      end
    end
  end)
  on_test(function(target, opt)
    -- As xmake runs a test: the target's script, else its rules'.
    local source = target:dep(table.wrap(target:get("deps"))[1])
    local run = source:script("test")
    for _, r in ipairs(source:orderules()) do
      run = run or r:script("test")
    end
    return run(source, opt)
  end)
end)

target("run-opencl", function()
  add_rules("lv.script", { script = "run_opencl.lua" })
  add_deps("fw.runfiles", { inherit = false })
end)

-- Every program also runs in the guest, as qemu-opencl-<name>.
local qemu = is_plat("linux") and is_arch("x86_64")

-- Cases and their native groups belong to their host executable.
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
      for _, case in ipairs(variants) do
        local variant = case[1]
        local hostargs = table.slice(case, 2)
        for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
          target:add("tests", (variant ~= "" and variant .. "/" or "") .. sm, {
            sm = sm,
            hostargs = hostargs,
            rundir = os.projectdir(),
            run_timeout = case.run_timeout,
          })
        end
      end
    end)
  end)

  if qemu then
    target("qemu-opencl-" .. name, function()
      add_rules("opencl.qemu")
      add_deps("opencl-" .. name)
    end)
  end
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
  "kernel_trap",
  "lmem_allocate",
  "lmem_layout",
  "lmem_size",
  "memory_ops",
  "oclprintf",
  "spirv_smoke",
  "subgroup_info",
  "workitem_ids",
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
    -- Shared public runners exceed 20 minutes in debug; retain the full dataset.
    { "irregular", path.join(data, "dc2/dc2.mtx"), run_timeout = 3600000 },
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
includes("dlprim/xmake.lua")
