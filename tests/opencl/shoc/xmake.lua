-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

includes("@builtin/check")
local directory =
  path.absolute(path.join(get_config("builddir") or "build", "testing-data/shoc"), os.projectdir())

target("opencl.shoc-common", function()
  set_kind("static")
  add_rules("build.no-coverage")
  set_languages("c++11")
  for _, name in ipairs({
    "CTimer",
    "ResultDatabase",
    "OptionParser",
    "Option",
    "Timer",
    "ProgressBar",
    "InvalidArgValue",
    "Matrix2DStatics",
  }) do
    add_files("src/common/" .. name .. ".cpp")
  end
  add_includedirs("src/common", { public = true })
  on_load(function(target)
    local configdir = path.join(target:autogendir(), "config")
    target:set("configdir", configdir)
    target:add("includedirs", configdir, { public = true })
  end)
  -- Both producers consume the same four platform checks and header template.
  add_configfiles("config.h.in", { pattern = "#cmake(define [A-Z_]+) 1" })
  configvar_check_cfuncs("HAVE_CLOCK_GETTIME", "clock_gettime", { includes = "time.h" })
  configvar_check_csnippets(
    "HAVE_CLOCK_PROCESS_CPUTIME_ID",
    "clockid_t id = CLOCK_PROCESS_CPUTIME_ID;",
    { includes = "time.h" }
  )
  configvar_check_cfuncs("HAVE_GETTIMEOFDAY", "gettimeofday", { includes = "sys/time.h" })
  configvar_check_cxxincludes("HAVE_STDINT_H", "stdint.h")
end)

target("opencl.shoc-opencl-common", function()
  set_kind("static")
  add_rules("build.no-coverage")
  set_languages("c++11")
  for _, name in ipairs({
    "OpenCLDeviceInfo",
    "OpenCLPlatform",
    "OpenCLNodePlatformContainer",
    "Event",
  }) do
    add_files("src/opencl/common/" .. name .. ".cpp")
  end
  add_includedirs("src/opencl/common", { public = true })
  add_deps("opencl.shoc-common", { public = true })
  add_packages("opencl", { public = true })
  add_defines("CL_TARGET_OPENCL_VERSION=120", { public = true })
end)

local inputs = path.join(directory, "inputs")
local workloads = {
  spmv = {
    directory = "level1/spmv",
    sources = { "Spmv.cpp" },
    kernels = { "spmv" },
    cases = {
      {
        config = "large_sm",
        args = {
          "--passes",
          "1",
          "--iterations",
          "1",
          "--mm_filename",
          path.join(inputs, "matrix.mtx"),
        },
      },
    },
  },
  stencil2d = {
    directory = "level1/stencil2d",
    sources = {
      "Stencil2Dmain.cpp",
      "OpenCLStencil.cpp",
      "OpenCLStencilFactory.cpp",
      "CommonOpenCLStencilFactory.cpp",
    },
    kernels = { "stencil2d" },
    cases = {
      {
        args = {
          "--customSize",
          "32,32",
          "--lsize",
          "8,8",
          "--num-iters",
          "2",
          "--passes",
          "1",
          "--warmupPasses",
          "0",
        },
      },
    },
  },
  bfs = {
    directory = "level1/bfs",
    sources = { "BFS.cpp", "$(projectdir)/tests/opencl/shoc/src/common/Graph.cpp" },
    kernels = { "bfs_iiit", "bfs_uiuc_spill" },
    cases = {
      {
        config = "large_sm",
        args = { "--passes", "1", "--graph_file", path.join(inputs, "graph.metis"), "--algo", "1" },
      },
      {
        name = "uiuc",
        kind = "bfs_uiuc",
        config = "large_sm",
        args = { "--passes", "1", "--graph_file", path.join(inputs, "graph.metis"), "--algo", "2" },
      },
    },
  },
  fft = {
    directory = "level1/fft",
    sources = { "FFT.cpp", "fftlib.cpp" },
    kernels = { "fft" },
    cases = {
      -- FFT can exceed ten minutes on slower machines or under concurrent load.
      { args = { "--passes", "1", "--KiB", "16" }, timeout = 1200000 },
    },
  },
  gemm = {
    directory = "level1/gemm",
    sources = { "GEMM.cpp" },
    kernels = { "gemmN" },
    cases = { { args = { "--passes", "1", "--dimension", "64" } } },
  },
  md = {
    directory = "level1/md",
    sources = { "MD.cpp" },
    kernels = { "md" },
    cases = {
      { config = "large_sm", args = { "--passes", "1", "--atoms", "256", "--iterations", "1" } },
    },
  },
  md5hash = {
    directory = "level1/md5hash",
    sources = { "MD5Hash.cpp" },
    kernels = { "md5" },
    cases = {
      {
        config = "large_sm",
        args = {
          "--passes",
          "1",
          "--key-length",
          "3",
          "--values-per-byte",
          "8",
          "--seed",
          "12345",
          "--verbose",
        },
      },
    },
  },
  reduction = {
    directory = "level1/reduction",
    sources = { "Reduction.cpp" },
    kernels = { "reduction" },
    cases = {
      {
        config = "large_sm",
        args = { "--passes", "1", "--elements", "4096", "--iterations", "1" },
      },
    },
  },
  scan = {
    directory = "level1/scan",
    sources = { "Scan.cpp" },
    kernels = { "scan" },
    cases = {
      {
        config = "large_sm",
        args = { "--passes", "1", "--elements", "4096", "--iterations", "1" },
      },
    },
  },
  sort = {
    directory = "level1/sort",
    sources = { "Sort.cpp" },
    kernels = { "sort" },
    cases = {
      {
        config = "large_sm",
        args = { "--passes", "1", "--elements", "1024", "--validation-input" },
      },
    },
  },
  triad = {
    directory = "level1/triad",
    sources = { "Triad.cpp" },
    cases = {
      { config = "large_sm", args = { "--passes", "1", "--vector-kib", "16", "--block-kib", "2" } },
    },
  },
  s3d = {
    directory = "level2/s3d",
    sources = { "S3D.cpp" },
    kernels = {
      "gr_base",
      "qssa",
      "qssab",
      "qssa2",
      "ratt",
      "ratt2",
      "ratt3",
      "ratt4",
      "ratt5",
      "ratt6",
      "ratt7",
      "ratt8",
      "ratt9",
      "ratt10",
      "ratx",
      "ratxb",
      "ratx2",
      "ratx4",
      "rdsmh",
      "rdwdot",
      "rdwdot2",
      "rdwdot3",
      "rdwdot6",
      "rdwdot7",
      "rdwdot8",
      "rdwdot9",
      "rdwdot10",
    },
    cases = {
      {
        config = "large_sm",
        timeout = 1200000,
        args = { "--passes", "1", "--dimension", "8", "--dump" },
      },
    },
  },
  devicememory = {
    directory = "level0",
    sources = { "DeviceMemory.cpp" },
    cases = {
      {
        config = "large_sm",
        args = {
          "--memory-kib",
          "64",
          "--memory-repeats",
          "2",
          "--memory-work-items",
          "256",
          "--memory-group-size",
          "32",
          "--passes",
          "1",
          "--quiet",
        },
      },
    },
  },
  kernelcompile = {
    directory = "level0",
    sources = { "KernelCompile.cpp" },
    cases = { { args = { "--passes", "1" } } },
  },
  maxflops = {
    directory = "level0",
    sources = { "MaxFlops.cpp" },
    cases = {
      {
        config = "large_sm",
        args = {
          "--passes",
          "1",
          "--elements",
          "2048",
          "--repeat-factor",
          "0.15",
          "--unroll",
          "1",
          "--verify",
          "--quiet",
        },
      },
    },
  },
  queuedelay = {
    directory = "level0",
    sources = { "QueueDelay.cpp" },
    defines = { 'SHOC_DEFAULT_PASSES="3"' },
    cases = {
      { config = "large_sm", args = { "--groups", "1", "--iterations", "2", "--passes", "1" } },
      {
        name = "default",
        kind = "queuedelay_default",
        config = "large_sm",
        args = { "--groups", "1", "--iterations", "1" },
      },
    },
  },
}
for name, source in pairs({
  busspeeddownload = "BusSpeedDownload.cpp",
  busspeedreadback = "BusSpeedReadback.cpp",
}) do
  workloads[name] = {
    directory = "level0",
    sources = { source },
    cases = {
      { args = { "--max-kib", "16", "--passes", "1" } },
      { name = "unpinned", args = { "--max-kib", "16", "--passes", "1", "--nopinned" } },
    },
  }
end

target("opencl.shoc-data", function()
  set_kind("phony")
  on_build(function(target)
    import("core.project.depend")
    local script = path.join(target:scriptdir(), "generate_inputs.py")
    depend.on_changed(function() os.vrunv("python3", { script, inputs }) end, {
      files = { script },
      dependfile = target:dependfile("inputs"),
      changed = target:is_rebuilt()
        or not os.isfile(path.join(inputs, "matrix.mtx"))
        or not os.isfile(path.join(inputs, "graph.metis")),
    })
    for name, workload in pairs(workloads) do
      for _, case in ipairs(workload.cases) do
        for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
          os.mkdir(path.join(directory, name, case.name or "", sm))
        end
      end
    end
  end)
end)

for name, workload in pairs(workloads) do
  target("opencl-shoc-" .. name, function()
    set_group("opencl/shoc")
    add_rules("opencl.host", "opencl.benchmark")
    add_deps("opencl.shoc-opencl-common", "opencl.shoc-data")
    set_languages("c++11")
    local upstream = path.join("src/opencl", workload.directory)
    add_files("src/opencl/common/main.cpp")
    add_includedirs(upstream)
    if workload.defines then add_defines(table.unpack(workload.defines)) end
    for _, file in ipairs(workload.sources) do
      add_files(file:startswith("$(projectdir)") and file or path.join(upstream, file))
    end
    on_load(function(target)
      for _, kernel in ipairs(workload.kernels or {}) do
        target:add(
          "files",
          path.join(target:autogendir(), kernel .. "_cl.cpp"),
          { always_added = true }
        )
      end
    end)
    before_build(function(target)
      import("core.project.depend")
      local script = path.join(target:scriptdir(), "embed_kernel.py")
      for _, name in ipairs(workload.kernels or {}) do
        local kernel = path.join(target:scriptdir(), upstream, name .. ".cl")
        local output = path.join(target:autogendir(), name .. "_cl.cpp")
        depend.on_changed(function()
          os.mkdir(path.directory(output))
          os.vrunv("python3", { script, kernel, output, "cl_source_" .. name })
        end, {
          files = { script, kernel },
          dependfile = target:dependfile(output),
          lastmtime = os.mtime(output),
          changed = target:is_rebuilt(),
        })
      end
    end)
    for _, case in ipairs(workload.cases) do
      for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
        add_tests((case.name and case.name .. "/" or "") .. sm, {
          sm = sm,
          config = case.config,
          hostargs = case.args,
          wrapper = { path.join(os.scriptdir(), "verify.py"), "--kind", case.kind or name },
          rundir = path.join(directory, name, case.name or "", sm),
          run_timeout = case.timeout or 600000,
        })
      end
    end
  end)
end

target("opencl-shoc-validation", function()
  set_group("opencl/shoc")
  set_kind("phony")
  add_deps("opencl.shoc-common")
  -- This oracle compiles comparator fixtures while simulator tests run in parallel.
  add_tests("validation", { run_timeout = 120000 })
  on_test(function(target, opt)
    local cxx = target:tool("cxx")
    return import("testing").run_with("python3", {
      path.join(target:scriptdir(), "verify_test.py"),
      cxx,
      path.join(target:scriptdir(), "src"),
      path.join(target:dep("opencl.shoc-common"):autogendir(), "config"),
    }, opt)
  end)
end)
