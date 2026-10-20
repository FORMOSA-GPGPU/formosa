-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

rule("build.no-coverage", function()
  after_config(function(target)
    -- Keep native mode.coverage off targets outside the coverage boundary.
    for _, kind in ipairs({ "cxflags", "mxflags", "ldflags", "shflags" }) do
      local flags = {}
      for _, flag in ipairs(table.wrap(target:get(kind))) do
        if flag ~= "--coverage" then table.insert(flags, flag) end
      end
      target:set(kind, flags)
    end
  end)
end)

rule("freestanding", function() add_deps("build.no-coverage") end)
