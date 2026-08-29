-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- SM Lua modules live in the owning project runtime.
-- lv only puts enabled projects on package.path, so missing owners disappear
-- from --sm without editing these callers.

local M = {}

local candidates = {
  "simtix.atomic_sm",
  "simtix.pipelined_sm",
}

function M.available()
  local sms = {}
  for _, name in ipairs(candidates) do
    if package.searchpath(name, package.path) then table.insert(sms, name) end
  end
  if #sms == 0 then return { "simtix.pipelined_sm" } end
  return sms
end

return M
