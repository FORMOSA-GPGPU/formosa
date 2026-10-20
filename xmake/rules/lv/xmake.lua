-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local function resource_rule(rule_name, kind, mapping)
  rule(rule_name, function()
    on_load(function(target)
      import("lv.layout")
      local dirs = layout.directories()
      local entries = {}
      for name, source in pairs(target:extraconf("rules", rule_name, mapping) or {}) do
        local destination = path.join(dirs[kind], name)
        source = path.absolute(source, target:scriptdir())
        local directory = os.isdir(source)
        target:add("installfiles", directory and path.join(source, "**") or source, {
          rootdir = directory and source or path.directory(source),
          prefixdir = directory and destination or path.directory(destination),
          filename = not directory and path.filename(destination) or nil,
          public = true,
        })
        table.insert(entries, { source = source, destination = destination })
      end
      target:data_set(rule_name, entries)
    end)
    on_build(function(target)
      import("lv.layout")
      local copy = target:extraconf("rules", rule_name, "copy")
      for _, entry in ipairs(target:data(rule_name)) do
        assert(os.exists(entry.source), "missing LV resource: " .. entry.source)
        local destination = path.join(layout.runtime_root(), entry.destination)
        if copy then
          assert(os.isfile(entry.source), "copied LV runfile must be a file: " .. entry.source)
          if os.islink(destination) then os.rm(destination) end
          if
            not os.isfile(destination) or io.readfile(destination) ~= io.readfile(entry.source)
          then
            os.mkdir(path.directory(destination))
            os.cp(entry.source, destination)
          end
        else
          if os.islink(destination) then
            if
              path.absolute(os.readlink(destination), path.directory(destination)) ~= entry.source
            then
              os.rm(destination)
            end
          else
            assert(
              not os.exists(destination),
              "LV resource destination already exists: " .. destination
            )
          end
          if not os.islink(destination) then
            os.mkdir(path.directory(destination))
            os.ln(entry.source, destination)
          end
        end
      end
    end)
  end)
end

resource_rule("lv.lua", "lua", "namespaces")
resource_rule("lv.runfiles", "runfiles", "files")

-- Artifact owners use the host LV interpreter while retaining their own
-- native test/build dependencies (including cross-compiled executables).
rule("lv.test", function()
  on_load(function(target) target:add("deps", "lv", { inherit = false }) end)
  on_test(function(target, opt)
    opt.rundir = opt.rundir or target:scriptdir()
    return import("testing").run_with(
      path.absolute(target:dep("lv"):targetfile()),
      opt.runargs,
      opt
    )
  end)
end)

rule("lv.script", function()
  on_load(function(target)
    import("lv.layout")
    target:set("kind", "phony")
    target:set("default", true)
    target:add("deps", "lv")
    local script = target:extraconf("rules", "lv.script", "script")
    target:add("installfiles", path.absolute(script, target:scriptdir()), {
      prefixdir = layout.directories().scripts,
      filename = path.filename(script),
    })
  end)
  on_build(function(target)
    import("lv.layout")
    local script = target:extraconf("rules", "lv.script", "script")
    layout.script_launcher(layout.runtime_root(), path.absolute(script, target:scriptdir()))
  end)
  on_run(function(target)
    import("core.base.option")
    local script = target:extraconf("rules", "lv.script", "script")
    local binary = path.absolute(target:dep("lv"):targetfile())
    -- Execute the source directly so development edits do not require a build.
    os.execv(
      binary,
      table.join({ path.absolute(script, target:scriptdir()) }, option.get("arguments") or {})
    )
  end)
  on_install(function(target)
    import("lv.layout")
    import("target.action.install", { alias = "install_target" })
    install_target(target:dep("lv"))
    install_target(target)
    local dirs = layout.directories()
    local script = target:extraconf("rules", "lv.script", "script")
    local relative = path.join(path.relative(dirs.scripts, dirs.bin), path.filename(script))
    layout.script_launcher(target:installdir(), relative)
  end)
  on_uninstall(function(target)
    import("lv.layout")
    local _, files = target:installfiles()
    for _, file in ipairs(files) do
      os.tryrm(file)
    end
    local script = target:extraconf("rules", "lv.script", "script")
    os.tryrm(path.join(target:installdir(), layout.directories().bin, path.filename(script)))
  end)
end)
