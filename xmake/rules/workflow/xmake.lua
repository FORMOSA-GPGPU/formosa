-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

rule("workflow.test_suites", function()
  after_config(function(target)
    local backend = import("workflow.backends.xmake")
    local selected = backend.selected()
    backend.schedule_config_summary()
    if #selected.test_suites == 0 then return end
    backend.validate_tests()
    local patterns = {}
    for _, selector in ipairs(selected.selectors) do
      table.insert(patterns, "^" .. path.pattern(selector) .. "$")
    end
    local tests = {}
    for _, name in ipairs(table.wrap(target:get("tests"))) do
      local fullname = target:name() .. "/" .. name
      for _, pattern in ipairs(patterns) do
        if fullname:match(pattern) then
          table.insert(tests, name)
          break
        end
      end
    end
    target:set("tests", tests)
    if #tests > 0 then target:set("default", true) end
  end)
end)

-- The selected target owns installation; workflow metadata only enables it.
option("installdir", function()
  set_default("outputs/out")
  set_showmenu(true)
  set_description("Local workflow installation prefix")
end)

rule("workflow.install", function()
  after_config(function(target)
    local config = import("core.project.config")
    local selected = import("workflow.backends.xmake").selected()
    target:set("installdir", path.absolute(config.get("installdir")))
    if
      selected.install
      and table.contains(selected.build_options["install-targets"] or {}, target:name())
    then
      assert(
        target:script("install"),
        "Workflow installation requires an on_install handler: " .. target:name()
      )
      target:set("default", true)
      target:data_set("workflow.install", true)
    end
  end)
  after_build(function(target)
    if target:data("workflow.install") then target:script("install")(target) end
  end)
end)
