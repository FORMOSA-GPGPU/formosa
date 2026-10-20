-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local directory = path.absolute(
  path.join(get_config("builddir") or "build", "testing-data/parboil"),
  os.projectdir()
)
local inputs = path.join(directory, "inputs")
local workloads = {
  ["sgemm"] = {
    variant = "opencl_base",
    reference = "base",
    config = "large_sm",
    sources = { "main.cc", "io.cc" },
    reference_sources = { "main.cc", "io.cc" },
    args = {
      "-i",
      path.join(inputs, "sgemm_a.txt")
        .. ","
        .. path.join(inputs, "sgemm_b.txt")
        .. ","
        .. path.join(inputs, "sgemm_bt.txt"),
    },
  },
  ["stencil"] = {
    variant = "opencl_base",
    reference = "cpu",
    config = "large_sm",
    sources = { "main.c", "file.cc" },
    reference_sources = { "main.c", "file.c", "kernels.c" },
    args = { "-i", path.join(inputs, "stencil.bin"), "16", "4", "4", "2" },
  },
  ["mri-q"] = {
    variant = "opencl",
    reference = "cpu",
    config = "large_sm",
    sources = { "main.c", "file.cc", "computeQ.c", "ocl.c" },
    reference_sources = { "main.c", "file.cc" },
    extra_kernels = { "src/opencl_nvidia/macros.h" },
    args = { "-i", path.join(inputs, "mri-q.bin") },
  },
  ["spmv"] = {
    variant = "opencl_base",
    reference = "cpu",
    sources = { "main.c", "file.c", "gpu_info.c", "ocl.c" },
    reference_sources = { "main.c", "file.c" },
    args = { "-i", path.join(inputs, "spmv.mtx") .. "," .. path.join(inputs, "spmv-vector.bin") },
  },
  ["cutcp"] = {
    variant = "opencl_base",
    reference = "base",
    config = "large_sm",
    sources = { "main.c", "cutoff.c", "cutcpu.c", "excl.c", "output.c", "readatom.c", "ocl.c" },
    reference_sources = { "main.c", "cutcpu.c", "excl.c", "output.c", "readatom.c" },
    args = { "-i", path.join(inputs, "cutcp.pqr") },
  },
  ["histo"] = {
    timeout = 1200000, -- Coverage with concurrent simulator workloads needs headroom.
    variant = "opencl_base",
    reference = "base",
    config = "large_sm",
    sources = { "main.cpp", "util.cpp", "OpenCL_common.cpp" },
    reference_sources = { "main.c", "util.c" },
    args = { "-i", path.join(inputs, "histo.bin"), "2" },
  },
  ["lbm"] = {
    variant = "opencl_base",
    reference = "cpu",
    sources = { "main.c", "lbm.c", "ocl.c" },
    reference_sources = { "main.c", "lbm.c" },
    defines = { "SIZE_X=8", "SIZE_Y=8", "SIZE_Z=8" },
    args = { "3" },
  },
  ["mri-gridding"] = {
    variant = "opencl_base",
    reference = "base",
    config = "large_sm",
    sources = {
      "main.cpp",
      "CPU_kernels.c",
      "OpenCL_interface.cpp",
      "scanLargeArray.cpp",
      "sort.cpp",
      "OpenCL_common.cpp",
    },
    reference_sources = { "main.c", "CPU_kernels.c" },
    args = { "-i", path.join(inputs, "mri-gridding.uks"), "128" },
  },
  ["sad"] = {
    timeout = 1200000,
    variant = "opencl_base",
    reference = "cpu",
    config = "large_sm",
    sources = { "main.cpp", "image.c", "file.c", "OpenCL_common.cpp" },
    reference_sources = { "main.c", "image.c", "file.c", "sad_cpu.c" },
    args = {
      "-i",
      path.join(inputs, "sad-reference.bin") .. "," .. path.join(inputs, "sad-current.bin"),
    },
  },
  ["tpacf"] = {
    variant = "opencl_base",
    reference = "base",
    config = "large_sm",
    sources = { "main.cc", "args.cc", "model.cc" },
    reference_sources = { "main.c", "args.c", "model_io.c", "model_compute_cpu.c" },
    args = {
      "-i",
      path.join(inputs, "tpacf-data.txt") .. "," .. path.join(inputs, "tpacf-random.txt"),
      "--",
      "-n",
      "1",
      "-p",
      "256",
    },
  },
}

for _, kind in ipairs({ "opencl", "cpu" }) do
  target("opencl.parboil-" .. kind, function()
    set_kind("static")
    add_rules("build.no-coverage")
    set_languages("gnu99")
    add_files(kind == "opencl" and "common/src/parboil_opencl.c" or "common/src/parboil.c")
    add_includedirs("common/include", { public = true })
    add_syslinks("m", { public = true })
    if kind == "opencl" then
      add_packages("opencl", { public = true })
      add_defines("CL_TARGET_OPENCL_VERSION=120")
    end
  end)
end

target("opencl.parboil-data", function()
  set_kind("phony")
  on_build(function(target)
    import("core.project.depend")
    local script = path.join(target:scriptdir(), "generate_inputs.py")
    local files = { script, path.join(target:scriptdir(), "xmake.lua") }
    local kernels = {}
    for name, workload in pairs(workloads) do
      local benchmark = path.join(target:scriptdir(), "benchmarks", name)
      kernels[name] = table.join(
        os.files(path.join(benchmark, "src", workload.variant, "*.cl")),
        os.files(path.join(benchmark, "src", workload.variant, "*.h"))
      )
      for _, file in ipairs(workload.extra_kernels or {}) do
        table.insert(kernels[name], path.join(benchmark, file))
      end
      table.join2(files, kernels[name])
    end
    table.sort(files)
    local dependfile = target:dependfile("inputs")
    local missing = false
    for _, output in ipairs((depend.load(dependfile) or {}).outputs or {}) do
      if not os.isfile(output) then
        missing = true
        break
      end
    end
    depend.on_changed(function()
      os.vrunv("python3", { script, inputs })
      local outputs = os.files(path.join(inputs, "**"))
      for name, sources in pairs(kernels) do
        local benchmark = path.join(target:scriptdir(), "benchmarks", name)
        for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
          for _, file in ipairs(sources) do
            local output = path.join(directory, name, sm, path.relative(file, benchmark))
            os.mkdir(path.directory(output))
            os.cp(file, output)
            table.insert(outputs, output)
          end
        end
      end
      return { outputs = outputs }
    end, {
      files = files,
      dependfile = dependfile,
      changed = target:is_rebuilt() or missing,
    })
  end)
end)

for name, workload in pairs(workloads) do
  for _, kind in ipairs({ "opencl", "cpu" }) do
    target("opencl-parboil-" .. name .. (kind == "cpu" and "-reference" or ""), function()
      set_group("opencl/parboil")
      if kind == "opencl" then
        add_rules("opencl.host", "opencl.benchmark")
        add_deps(
          "opencl-parboil-" .. name .. "-reference",
          "opencl.parboil-data",
          { inherit = false }
        )
      else
        set_kind("binary")
        add_rules("build.no-coverage")
      end
      add_deps("opencl.parboil-" .. kind)
      set_languages("gnu99", "c++11")
      add_defines("CL_TARGET_OPENCL_VERSION=120")
      if workload.defines then add_defines(table.unpack(workload.defines)) end
      local variant = kind == "cpu" and workload.reference or workload.variant
      for _, file in ipairs(kind == "cpu" and workload.reference_sources or workload.sources) do
        add_files(path.join("benchmarks", name, "src", variant, file))
      end
      if name == "spmv" then
        add_files(
          "benchmarks/spmv/common_src/convert-dataset/convert_dataset.c",
          "benchmarks/spmv/common_src/convert-dataset/mmio.c"
        )
        add_includedirs("benchmarks/spmv/common_src/convert-dataset")
      end
      if name == "mri-q" and kind == "cpu" then add_cflags("-fgnu89-inline") end
      if kind == "opencl" then
        after_load(function(target)
          local wrapper = {
            path.join(target:scriptdir(), "verify.py"),
            "--kind",
            name,
            "--output",
            "result.dat",
            "--reference",
            path.absolute(target:dep("opencl-parboil-" .. name .. "-reference"):targetfile()),
          }
          for _, arg in ipairs(workload.args) do
            table.insert(wrapper, "--reference-arg=" .. arg)
          end
          for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
            target:add("tests", sm, {
              hostargs = table.join({ "-o", "result.dat" }, workload.args),
              sm = sm,
              config = workload.config,
              wrapper = wrapper,
              rundir = path.join(directory, name, sm),
              run_timeout = workload.timeout or 600000,
            })
          end
        end)
      end
    end)
  end
end
