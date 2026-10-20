-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- This directory contains only cp.core adapters; helpers live outside it.
local function cores()
  local root = path.join(os.projectdir(), "tests/cp/cores")
  local files = os.files(path.join(root, "**.lua"))
  table.sort(files)
  local adapters = {}
  for _, file in ipairs(files) do
    local name = path.relative(file, root):gsub("%.lua$", ""):gsub("[/\\]", ".")
    local adapter = import(name, { rootdir = root })()
    assert(type(adapter.isa) == "string", "expected cp.core adapter: " .. name)
    table.insert(adapters, { name = name, adapter = adapter })
  end
  return adapters
end

function tests(target, script, opt)
  for _, core in ipairs(cores()) do
    if opt.accepts(core.adapter) then
      target:add("tests", core.name, {
        runargs = table.join(
          { path.join(os.projectdir(), "tests/cp", script), "--core", core.name },
          opt.args or {}
        ),
        run_timeout = opt.timeout or 30000,
      })
    end
  end
end
