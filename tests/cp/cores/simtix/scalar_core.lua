-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

---@class simtix.scalar_core : cp.core
---@field private _native simtix.ScalarCore
---@field private _sc_module sc.Module
---@overload fun(name: string): simtix.scalar_core
local ScalarCore = {
  isa = "rv64im",
  num_mem_ports = 2,
  has_ext_int = true,
  has_sw_int = true,
  has_timer_int = true,
  has_reset = false,
}
-- Build tools read capabilities without constructing a native model.
if xmake then
  function main() return ScalarCore end
  return
end

function ScalarCore:__index(key)
  if key == "ext_int" or key == "sw_int" or key == "timer_int" then return self._native[key] end
  return ScalarCore[key]
end

---@param _ string Module name, consumed by lv.sc_module.wrap.
---@return simtix.scalar_core
function ScalarCore.new(_)
  assert(simtix and simtix.ScalarCore, "simtix.ScalarCore is not available in this lv build")
  local self = setmetatable({}, ScalarCore --[[@as table]])
  self._native = simtix.ScalarCore("native")
  return self
end

---@param entry integer
function ScalarCore:boot(entry) self._native.pc = entry end

function ScalarCore:__newindex(key, value)
  if key == "clock" then
    self._native.clock = value
  elseif key == "imem_target" then
    self._native.imem = value
  elseif key == "dmem_target" then
    self._native.dmem = value
  elseif key == "ext_int" or key == "sw_int" or key == "timer_int" then
    error(key .. " is a port; use " .. key .. "(signal)")
  elseif key == "reset_n" then
    -- This model has no reset input.
    return
  else
    rawset(self, key, value)
  end
end

setmetatable(ScalarCore --[[@as table]], {
  __call = function(cls, ...) return cls.new(...) end,
})

return require("lv.sc_module").wrap(ScalarCore)
