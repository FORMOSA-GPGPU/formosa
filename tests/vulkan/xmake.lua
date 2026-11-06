-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

option("vulkan-tests", function()
  set_default("auto")
  set_values("auto", "y", "n")
  set_showmenu(true)
  set_description("Build and register the experimental Vulkan compute tests")
end)

add_requires("pkgconfig::vulkan", { alias = "vulkan", system = true, optional = true })
target("vulkan-compute", function()
  set_kind("binary")
  add_rules("build.no-coverage")
  add_files("smoke/main.cc")
  add_packages("vulkan")
  add_syslinks("pthread")
  add_deps("lv", "fw.runfiles", { inherit = false })
  on_load(function(target)
    local enabled = import("workflow.backends.xmake").enabled("vulkan-tests", false)
    target:set("enabled", enabled)
    if not enabled then return end
    assert(target:pkg("vulkan"), "Vulkan loader/headers are required; enter the default Nix shell")
    for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
      for _, mode in ipairs({ "smoke", "validation", "unsupported", "device-lost" }) do
        target:add("tests", mode .. "/" .. sm, {
          sm = sm,
          mode = mode,
          rundir = os.projectdir(),
          run_timeout = 120000,
        })
      end
    end
  end)
  on_test(function(target, opt)
    local output =
      path.absolute(path.join(import("core.project.config").builddir(), "testing-out", opt.name))
    os.mkdir(output)
    local args = {
      path.join(os.projectdir(), "tests/opencl/run_opencl.lua"),
      "--sm",
      opt.sm,
      "--config",
      "large_sm",
      "--stats",
      path.join(output, "stats.toml"),
      "--trace",
      path.join(output, "trace"),
      "--",
      path.absolute(target:targetfile()),
      path.absolute(path.join(target:targetdir(), "vulkan-shaders")),
    }
    if opt.mode == "unsupported" or opt.mode == "device-lost" then table.insert(args, opt.mode) end
    opt.runenvs = table.join(opt.runenvs or {}, {
      VK_INSTANCE_LAYERS = opt.mode == "validation" and "VK_LAYER_KHRONOS_validation" or "",
      VK_LOADER_DEBUG = opt.mode == "validation" and "layer" or "",
    })
    opt.fail_outputs = { "Validation Error", "VUID%-" }
    local passed =
      import("testing").run_with(path.absolute(target:dep("lv"):targetfile()), args, opt)
    if opt.mode == "validation" then
      -- The loader can ignore an unavailable implicit layer; prove it loaded.
      if
        not (opt.stderr or ""):find('Insert instance layer "VK_LAYER_KHRONOS_validation"', 1, true)
      then
        opt.errors = "Khronos validation layer was not loaded"
        return false
      end
    end
    return passed
  end)
  before_build(function(target)
    local directory = path.join(target:targetdir(), "vulkan-shaders")
    os.mkdir(directory)
    for _, name in ipairs({ "smoke/smoke", "smoke/images", "smoke/unsupported", "smoke/empty" }) do
      os.vrunv("glslangValidator", {
        "-V",
        "--target-env",
        "vulkan1.1",
        path.join(target:scriptdir(), name .. ".comp"),
        "-o",
        path.join(directory, path.filename(name) .. ".spv"),
      })
    end
  end)
end)
