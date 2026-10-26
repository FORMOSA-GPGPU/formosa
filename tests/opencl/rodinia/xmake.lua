-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

add_requires("openmp", { system = true })

local directory = path.absolute(
  path.join(get_config("builddir") or "build", "testing-data/rodinia"),
  os.projectdir()
)
local inputs = path.join(directory, "inputs")
local source = os.scriptdir()
local workloads = {
  bfs = {
    sources = { "bfs/bfs.cpp" },
    cxx = true,
    timing = true,
    readback = true,
    cases = {
      { "4k", args = { path.join(source, "bfs/graph4k.txt") }, config = "large_sm" },
      { "32", args = { path.join(source, "bfs/graph32.txt") } },
    },
  },
  gaussian = {
    sources = { "gaussian/gaussianElim.cpp", "gaussian/clutils.cpp", "gaussian/utils.cpp" },
    readback = true,
    cases = {},
  },
  kmeans = {
    sources = {
      "kmeans/kmeans.cpp",
      "kmeans/read_input.c",
      "kmeans/cluster.c",
      "kmeans/kmeans_clustering.c",
      "kmeans/rmse.c",
    },
    cxx = true,
    timing = true,
    readback = true,
    cases = {
      {
        "",
        args = { "-i", path.join(inputs, "kmeans100.txt"), "-n", "5", "-m", "5", "-o" },
        reference_args = { path.join(inputs, "kmeans100.txt") },
        config = "large_sm",
      },
    },
  },
  nn = {
    sources = { "nn/nearestNeighbor.cpp", "nn/clutils.cpp", "nn/utils.cpp" },
    readback = true,
    cases = {
      {
        "",
        args = { "filelist.txt", "-f", path.join(source, "nn"), "-p", "0", "-d", "0" },
        reference_args = { path.join(source, "nn/filelist.txt") },
      },
    },
  },
  hotspot = {
    sources = { "hotspot/hotspot.c", "hotspot/OpenCL_helper_library.c" },
    cases = {
      {
        "",
        config = "large_sm",
        output = "result.txt",
        args = {
          "16",
          "1",
          "2",
          path.join(inputs, "temperature16.txt"),
          path.join(inputs, "power16.txt"),
          "result.txt",
        },
        reference_args = {
          "16",
          "16",
          "2",
          "1",
          path.join(inputs, "temperature16.txt"),
          path.join(inputs, "power16.txt"),
          "result.txt",
        },
      },
    },
  },
  hotspot3d = {
    directory = "hotspot3D",
    sources = { "hotspot3D/3D.c", "hotspot3D/CL_helper.c" },
    cases = {
      {
        "",
        config = "large_sm",
        output = "result.txt",
        args = {
          "-n",
          "64",
          "-l",
          "2",
          "-i",
          "2",
          "-f",
          path.join(inputs, "power64x64x2.txt"),
          path.join(inputs, "temperature64x64x2.txt"),
          "result.txt",
        },
      },
    },
  },
  lud = {
    sources = { "lud/ocl/lud.cpp", "lud/common/common.c" },
    includes = { "opencl/lud/common" },
    cases = {
      { "", config = "large_sm", args = { "-i", path.join(inputs, "matrix32.txt"), "-v" } },
    },
  },
  nw = {
    sources = { "nw/nw.c" },
    cxx = true,
    defines = { "TRACEBACK" },
    reference = { "openmp/nw/needle.cpp" },
    cases = {
      {
        "",
        output = "result.txt",
        args = { "32", "10", "nw.cl" },
        reference_args = { "32", "10", "1" },
      },
    },
  },
  pathfinder = {
    sources = { "pathfinder/main.cpp", "pathfinder/OpenCL.cpp" },
    cxx = true,
    timing = true,
    defines = { "BENCH_PRINT" },
    reference = { "openmp/pathfinder/pathfinder.cpp" },
    cases = {
      { "", args = { "-c", "64", "-r", "8", "-h", "2" }, reference_args = { "64", "8" } },
    },
  },
  streamcluster = {
    sources = { "streamcluster/streamcluster.cpp" },
    cxx = true,
    timing = true,
    reference = { "openmp/streamcluster/streamcluster_omp.cpp" },
    cases = {
      {
        "",
        config = "large_sm",
        -- Allow the same 900-second budget as the CMake fallback in Debug CI.
        timeout = 900000,
        output = "centers.txt",
        args = { "2", "3", "2", "256", "256", "4", "none", "centers.txt", "1", "-p", "0" },
        reference_args = { "2", "3", "2", "256", "256", "4", "none", "centers.txt", "1" },
      },
    },
  },
  particlefilter = {
    sources = { "particlefilter/ex_particle_OCL_single_seq.cpp" },
    cxx = true,
    timing = true,
    readback = true,
    cases = {
      {
        "",
        config = "large_sm",
        output = "output.txt",
        args = { "-x", "16", "-y", "16", "-z", "3", "-np", "256" },
      },
    },
  },
  dwt2d = {
    sources = { "dwt2d/main.cpp" },
    cxx = true,
    timing = true,
    readback = true,
    defines = { "OUTPUT" },
    cases = {
      {
        "explicit",
        config = "large_sm",
        output = "result.dwt",
        args = { "-D", "32x32", "-l", "1", "-5", "input.rgb", "result.dwt" },
      },
      {
        "default",
        config = "large_sm",
        output = "input.rgb.dwt",
        args = { "-D", "32x32", "-l", "1", "-5", "input.rgb" },
      },
    },
  },
  btree = {
    directory = "b+tree",
    timing = true,
    reference = { "openmp/b+tree/main.c", "openmp/b+tree/kernel/*.c", "openmp/b+tree/util/*/*.c" },
    cases = {
      {
        "",
        config = "large_sm",
        output = "output.txt",
        args = {
          "file",
          path.join(inputs, "btree.txt"),
          "command",
          path.join(inputs, "btree-commands.txt"),
        },
        reference_args = {
          "file",
          path.join(inputs, "btree.txt"),
          "command",
          path.join(inputs, "btree-commands.txt"),
          "cores",
          "1",
        },
      },
    },
  },
  srad = {
    timing = true,
    reference = { "openmp/srad/srad_v1/main.c" },
    cases = {
      {
        "",
        config = "large_sm",
        output = "output/image_out.pgm",
        args = { "-n", "2", "-l", "1", "-s", "16", "16", "-p", "0" },
        reference_args = { "2", "1", "16", "16", "1", path.join(inputs, "srad.pgm") },
      },
    },
  },
  lavamd = {
    directory = "lavaMD",
    timing = true,
    readback = true,
    defines = { "OUTPUT" },
    cases = {
      { "", config = "large_sm", output = "result.txt", args = { "-boxes1d", "1", "-p", "0" } },
    },
  },
  myocyte = {
    timing = true,
    reference = { "myocyte_reference.c" },
    cases = {
      {
        "",
        config = "large_sm",
        output = "output.txt",
        args = { "-time", "1", "-r", "." },
        reference_args = {
          path.join(source, "data/myocyte/y.txt"),
          path.join(source, "data/myocyte/params.txt"),
          "output.txt",
        },
      },
    },
  },
  backprop = {
    sources = {
      "backprop/backprop_ocl.cpp",
      "backprop/backprop.c",
      "backprop/imagenet.c",
      "backprop/facetrain.c",
    },
    cxx = true,
    timing = true,
    reference = {
      "backprop_reference.cpp",
      "opencl/backprop/backprop.c",
      "opencl/backprop/imagenet.c",
    },
    includes = { "opencl/backprop" },
    readback = true,
    cases = { { "", config = "large_sm", args = { "-n", "32" } } },
  },
  particlefilter_double = {
    directory = "particlefilter",
    sources = { "particlefilter/ex_particle_OCL_double_seq.cpp" },
    cxx = true,
    timing = true,
    readback = true,
    cases = {
      {
        "",
        config = "large_sm",
        output = "output.txt",
        args = { "-x", "16", "-y", "16", "-z", "3", "-np", "256" },
      },
    },
  },
  particlefilter_naive = {
    directory = "particlefilter",
    sources = { "particlefilter/ex_particle_OCL_naive_seq.cpp" },
    cxx = true,
    readback = true,
    cases = {
      { "", config = "large_sm", args = { "-x", "16", "-y", "16", "-z", "3", "-np", "256" } },
    },
  },
  cfd = {
    sources = { "cfd/euler3d.cpp" },
    cxx = true,
    timing = true,
    readback = true,
    defines = { "iterations=2" },
    reference = { "openmp/cfd/euler3d_cpu.cpp" },
    reference_defines = { "iterations=2", "block_length=192" },
    cases = {
      {
        "",
        config = "large_sm",
        output = "density.txt",
        args = { "ctest.mesh" },
        reference_args = { path.join(inputs, "cfd.txt") },
      },
    },
  },
}
for _, size in ipairs({ "4", "25", "33" }) do
  local matrix = path.join(source, "gaussian/matrix" .. size .. ".txt")
  table.insert(
    workloads.gaussian.cases,
    { size, args = { "-f", matrix, "-p", "0", "-d", "0" }, reference_args = { matrix } }
  )
end

local function target_name(name)
  return "opencl-"
    .. (table.contains({ "bfs", "gaussian", "kmeans", "nn" }, name) and "" or "rodinia-")
    .. name
end

target("opencl.rodinia-data", function()
  set_kind("phony")
  on_build(function(target)
    import("core.project.depend")
    local script = path.join(target:scriptdir(), "generate_inputs.py")
    local files = table.join(
      os.files(path.join(source, "opencl/**.cl")),
      os.files(path.join(source, "opencl/**.h")),
      {
        script,
        path.join(source, "data/myocyte/y.txt"),
        path.join(source, "data/myocyte/params.txt"),
        path.join(source, "xmake.lua"),
        path.join(source, "nn/filelist.txt"),
      }
    )
    table.sort(files)
    local dependfile = target:dependfile("inputs")
    local missing = false
    for _, output in ipairs((depend.load(dependfile) or {}).outputs or {}) do
      if not os.exists(output) then
        missing = true
        break
      end
    end
    depend.on_changed(function()
      os.vrunv("python3", { script, inputs })
      local outputs = os.files(path.join(inputs, "**"))
      local function stage(file, destination)
        os.mkdir(path.directory(destination))
        os.cp(file, destination)
        table.insert(outputs, destination)
      end
      for name, workload in pairs(workloads) do
        local upstream = path.join(source, "opencl", workload.directory or name)
        local variants = #workload.cases > 0 and workload.cases or { { "" } }
        for _, case in ipairs(variants) do
          for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
            local cwd = path.join(directory, name .. case[1], sm)
            os.mkdir(cwd)
            for _, file in
              ipairs(
                table.join(
                  os.files(path.join(upstream, "**.cl")),
                  os.files(path.join(upstream, "**.h"))
                )
              )
            do
              local destination = path.join(cwd, path.relative(file, upstream))
              stage(file, destination)
            end
            if name == "nn" then
              stage(path.join(source, "nn/filelist.txt"), path.join(cwd, "filelist.txt"))
            end
            if name == "dwt2d" then
              stage(path.join(inputs, "input.rgb"), path.join(cwd, "input.rgb"))
            end
            if name == "srad" then
              local outputdir = path.join(cwd, "output")
              os.mkdir(outputdir)
              table.insert(outputs, outputdir)
            end
            if name == "cfd" then
              stage(path.join(inputs, "cfd.txt"), path.join(cwd, "ctest.mesh"))
            end
            if name == "myocyte" then
              stage(path.join(inputs, "myocyte/y.txt"), path.join(cwd, "y.txt"))
              stage(path.join(inputs, "myocyte/params.txt"), path.join(cwd, "params.txt"))
            end
          end
        end
      end
      -- The unmodified SRAD host reads ../../data/srad/image.pgm.
      stage(path.join(inputs, "srad.pgm"), path.join(directory, "data/srad/image.pgm"))
      return { outputs = outputs }
    end, {
      files = files,
      dependfile = dependfile,
      changed = target:is_rebuilt() or missing,
    })
  end)
end)

target("opencl.rodinia-readback", function()
  set_kind("shared")
  add_rules("build.no-coverage")
  add_files("readback.c")
  add_packages("opencl")
  add_defines("CL_TARGET_OPENCL_VERSION=120")
  if is_plat("linux") then add_syslinks("dl") end
end)

for name, workload in pairs(workloads) do
  if workload.reference then
    target("opencl-rodinia-" .. name .. "-reference", function()
      set_kind("binary")
      add_rules("build.no-coverage")
      set_languages("gnu99", "c++11")
      add_packages("openmp")
      add_syslinks("m")
      if workload.reference_defines then add_defines(table.unpack(workload.reference_defines)) end
      for _, file in ipairs(workload.reference) do
        add_files(file, workload.cxx and { sourcekind = "cxx" } or {})
      end
      if workload.includes then add_includedirs(table.unpack(workload.includes)) end
      if name == "btree" then
        add_cflags(
          "-Wno-implicit-int",
          "-Wno-error=incompatible-pointer-types",
          "-include stdio.h",
          "-include sys/time.h",
          { force = true }
        )
      end
    end)
  end
  target(target_name(name), function()
    set_group("opencl/rodinia")
    add_rules("opencl.host", "opencl.benchmark")
    add_deps("opencl.rodinia-data", { inherit = false })
    if workload.readback then add_deps("opencl.rodinia-readback", { inherit = false }) end
    if workload.reference then
      add_deps("opencl-rodinia-" .. name .. "-reference", { inherit = false })
    end
    set_languages("gnu99", "c++11")
    add_packages("openmp")
    add_syslinks("m")
    add_defines("CL_TARGET_OPENCL_VERSION=120")
    if workload.timing or name == "gaussian" or name == "nn" then add_defines("TIMING") end
    if workload.defines then add_defines(table.unpack(workload.defines)) end
    add_includedirs("opencl/util")
    if workload.includes then add_includedirs(table.unpack(workload.includes)) end
    local upstream = workload.directory or name
    for _, file in
      ipairs(
        workload.sources
          or { upstream .. "/main.c", upstream .. "/kernel/*wrapper*.c", upstream .. "/util/*/*.c" }
      )
    do
      add_files("opencl/" .. file, workload.cxx and { sourcekind = "cxx" } or {})
    end
    if workload.timing then
      add_files("opencl/util/timing.c", workload.cxx and { sourcekind = "cxx" } or {})
    end
    if name == "btree" then add_cflags("-Wno-implicit-int", { force = true }) end
    on_config(function(target)
      if name == "kmeans" then
        -- Preserve upstream allocate()'s missing-return workaround from CMake.
        if target:has_tool("cxx", "gcc", "gxx") then
          target:set("optimize", "none")
          target:add("cxxflags", "-fno-unreachable-traps")
        else
          target:add("cxxflags", "-fno-strict-return")
        end
      end
    end)
    after_load(function(target)
      for _, case in ipairs(workload.cases) do
        local wrapper = { path.join(source, "verify.py"), "--kind", name }
        if workload.readback then
          table.join2(wrapper, {
            "--readback-library",
            path.absolute(target:dep("opencl.rodinia-readback"):targetfile()),
          })
        end
        if workload.reference then
          table.join2(wrapper, {
            "--reference",
            path.absolute(target:dep("opencl-rodinia-" .. name .. "-reference"):targetfile()),
          })
        end
        if case.output then table.join2(wrapper, { "--output", case.output }) end
        for _, arg in
          ipairs(case.reference_args or (workload.readback and name == "bfs" and case.args) or {})
        do
          table.insert(wrapper, "--reference-arg=" .. arg)
        end
        for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
          target:add("tests", (case[1] ~= "" and case[1] .. "/" or "") .. sm, {
            hostargs = case.args,
            config = case.config,
            sm = sm,
            wrapper = wrapper,
            rundir = path.join(directory, name .. case[1], sm),
            run_timeout = case.timeout or 600000,
          })
        end
      end
    end)
  end)
end

target("opencl-rodinia-validation", function()
  set_group("opencl/rodinia")
  set_kind("phony")
  add_tests("validation", { run_timeout = 30000 })
  on_test(function(target, opt)
    local cc = target:tool("cc")
    return import("testing").run_with("python3", {
      path.join(target:scriptdir(), "verify_test.py"),
      cc,
      path.join(target:scriptdir(), "opencl/lud/common"),
    }, opt)
  end)
end)
