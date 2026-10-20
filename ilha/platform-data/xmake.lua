-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

rule("ilha.platform-data", function()
  on_load(function(target)
    target:set("kind", "phony")
    target:set("policy", "build.fence", true)
    target:add("includedirs", target:autogendir(), { public = true })
  end)
  on_build(function(target)
    import("core.project.depend")
    local fixture = target:extraconf("rules", "ilha.platform-data", "fixture")
    local inputs = { path.join(os.projectdir(), "ilha/lua/addr_map.lua") }
    if fixture then
      table.insert(inputs, path.join(os.projectdir(), fixture:gsub("%.", "/") .. ".lua"))
    end
    for kind, name in pairs({ header = "formosa_addr_map.h", linker = "ilha_memory.ld" }) do
      local script = path.join(os.projectdir(), "ilha/scripts/generate_" .. kind .. ".lua")
      local output = path.join(target:autogendir(), name)
      local dependfile = path.join(target:autogendir(), name .. ".d")
      os.mkdir(target:autogendir())
      depend.on_changed(
        function()
          import("ilha.scripts.generate_" .. kind, { rootdir = os.projectdir() }).main(
            output,
            os.projectdir(),
            fixture
          )
        end,
        {
          files = table.join(inputs, { script }),
          dependfile = dependfile,
          lastmtime = os.mtime(output),
          changed = target:is_rebuilt(),
        }
      )
    end
  end)
end)

target("ilha-platform-data", function()
  set_default(true)
  add_rules("ilha.platform-data")
  add_deps("lv.device", { public = true })
end)
