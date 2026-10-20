-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("kernel-sim.lua", function()
  set_kind("phony")
  add_rules("lv.lua", { namespaces = { ["kernel-sim"] = "lua" } })
end)

rule("kernel-sim.elf", function()
  add_deps("riscv.elf")
  on_load(function(target)
    target:add("cxflags", "-mcpu=formosa-gpgpu", "-mabi=lp64", { force = true })
    target:add("asflags", "-mcpu=formosa-gpgpu", "-mabi=lp64", { force = true })
    target:set("optimize", "faster")
    target:add("deps", "kernel-sim.lua")
  end)
  on_config(function(target)
    import("lv.layout")
    target:set("prefixdir", "", { bindir = path.join(layout.directories().runfiles, "bin") })
  end)
  after_build(
    function(target)
      import("lv.layout").stage_runfile(target:targetfile(), path.join("bin", target:filename()))
    end
  )
end)

for _, name in ipairs({
  "2mm",
  "bfs",
  "convolution",
  "fractal-tree",
  "gemm",
  "oesort",
  "relu",
  "vecadd",
  "vecmul",
  "voronoi",
  "trap",
}) do
  target("kernel-sim-" .. name, function()
    add_rules("kernel-sim.elf", "lv.test", "linker.link_scripts")
    set_filename(name .. ".kernel-sim.elf")
    add_files(name .. "/kernel.c", "common/start.S", "common/link.ld")
    add_includedirs("common")
    for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
      add_tests(sm, {
        runargs = { path.join(os.scriptdir(), name, name .. ".lua"), "--sm", sm },
        rundir = path.join(os.scriptdir(), name),
        run_timeout = 30000,
      })
    end
  end)
end

target("kernel-sim-bfs", function()
  before_test(function()
    import("lv.layout")
    -- The existing BFS script writes its diagnostic graph at this logical path.
    os.mkdir(
      path.join(layout.runtime_root(), layout.directories().runfiles, "tests/kernel-sim/bfs")
    )
  end)
end)

includes("riscv-test/xmake.lua", "barrier/xmake.lua")
