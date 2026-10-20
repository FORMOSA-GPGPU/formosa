-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for name, description in pairs({
  ["lv-enable-perfetto"] = "LV Perfetto tracing: auto follows workflow (otherwise n); y/n override.",
  ["lv-enable-docgen"] = "Generate LV Lua API metadata: auto follows workflow (otherwise y); y/n override.",
}) do
  option(name, function()
    set_default("auto")
    set_values("auto", "y", "n")
    set_showmenu(true)
    set_description(description)
  end)
end

target("liblv", function()
  set_kind("static")
  set_basename("lv")
  add_files("src/liblv/**.cc")
  on_load(function(target)
    import("lv.layout").runtime_defines(target)
    if import("workflow.backends.xmake").enabled("lv-enable-perfetto", false) then
      target:add("defines", "ENABLE_PERFETTO=true", { public = true })
    end
  end)
  add_includedirs("include", { public = true })
  add_deps(
    "systemc",
    "fmt",
    "quill",
    "tomlplusplus",
    "sol2",
    "perfetto",
    "libcomm",
    { public = true }
  )
  if is_mode("debug", "coverage") then
    add_defines(
      "QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L3",
      { public = true }
    )
  else
    add_defines(
      "QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO",
      { public = true }
    )
  end
end)

target("Vvclint", function()
  add_rules("verilator.static", "build.no-coverage")
  set_toolchains("verilator")
  add_files("src/lv/bindings/simple/vclint.sv")
  add_values("verilator.flags", "--top-module", "vclint")
  on_load(function(target)
    -- RTL parameters share the maintained offsets with native and guest code.
    local header =
      io.readfile(path.join(target:scriptdir(), "device/include/lv/mmio/simple/clint.h"))
    for _, register in ipairs({ "MTIMECMP", "MTIME" }) do
      local offset = assert(
        header:match("#define LV_CLINT_" .. register .. "_OFFSET (0x%x+)"),
        "Missing numeric LV_CLINT_" .. register .. "_OFFSET"
      )
      target:add("values", "verilator.flags", "-G" .. register .. "_OFFSET=" .. offset)
    end
  end)
  before_prepare(function(target)
    import("core.project.depend")
    -- The native Verilator rule tracks RTL inputs, but not parameter headers.
    local header = path.join(target:scriptdir(), "device/include/lv/mmio/simple/clint.h")
    depend.on_changed(
      function() os.tryrm(path.join(target:autogendir(), "rules/verilator/Vvclint.cmake")) end,
      {
        files = { header },
        values = target:values("verilator.flags"),
        dependfile = target:dependfile(header),
      }
    )
  end)
  add_defines("VL_TIME_STAMP64", { public = true })
  set_policy("build.fence", true)
  on_config(function(target)
    -- The bundled rule defaults generated C++ to C++20; LV uses C++17.
    target:set("languages", "c++17")
  end)
end)

-- Object dependencies retain binding initializers without whole-archive linking.
target("lv.bindings", function()
  set_kind("object")
  add_files("src/lv/bindings/**.cc")
  add_deps("liblv", "lv.device", "dramsys", "elfio", "glm", "Vvclint")
  add_defines('DRAMSYS_RESOURCE_DIR="third-party/DRAMSys/configs"')
end)

target("lv.lua", function()
  set_kind("phony")
  add_rules("lv.lua", {
    namespaces = { lv = "lua" },
  })
end)

target("lv", function()
  set_kind("binary")
  set_default(true)
  add_files("src/lv/main.cc")
  add_deps("liblv", "argparse", "lv.bindings", "lv.lua")
  on_load(function(target) import("workflow.backends.xmake").selected() end)
end)

includes("tests/xmake.lua")

target("lv-docgen", function()
  set_kind("phony")
  add_deps("lv")
  on_load(
    function(target)
      target:set("default", import("workflow.backends.xmake").enabled("lv-enable-docgen", true))
    end
  )
  on_build(function(target)
    import("core.project.depend")
    import("lib.detect.find_tool")
    local outputdir = path.join(os.projectdir(), ".cache/lv_meta")
    local output = path.join(outputdir, "lv_api.lua")
    local binary = target:dep("lv"):targetfile()
    depend.on_changed(function()
      os.mkdir(outputdir)
      os.vrunv(binary, { "--docgen" }, { stdout = output })
      local lua_ls = find_tool("lua-language-server")
      if lua_ls then os.vrunv(lua_ls.program, { "--check", outputdir }) end
    end, {
      files = { binary, path.join(target:scriptdir(), "xmake.lua") },
      dependfile = target:dependfile(output),
      lastmtime = os.mtime(output),
      changed = target:is_rebuilt(),
    })
  end)
end)
