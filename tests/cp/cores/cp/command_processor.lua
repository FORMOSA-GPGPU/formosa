-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

---@class cp.command_processor : cp.core
---@field private _native cp.CommandProcessor
---@field private _gnd sc.signal
---@field private _sc_module sc.Module
---@overload fun(name: string): cp.command_processor
local CommandProcessor =
  { isa = "rv64im", num_mem_ports = 1, has_ext_int = true, has_reset = false }
CommandProcessor.__index = CommandProcessor

---@param _ string Module name, consumed by lv.sc_module.wrap.
---@return cp.command_processor
function CommandProcessor.new(_)
  assert(cp and cp.CommandProcessor, "cp.CommandProcessor is not available in this lv build")
  local self = setmetatable({}, CommandProcessor --[[@as table]])
  self._native = cp.CommandProcessor("native", 0, {
    core_trace = false,
    rsp_enable = false,
    rsp_port = 0,
    rsp_trace = false,
  })
  self._gnd = sc.signal("gnd")
  self._gnd:write(false)
  self._native.sw_int = self._gnd
  self._native.timer_int = self._gnd
  return self
end

---@param entry integer
function CommandProcessor:boot(entry) self._native:set_pc(entry) end

function CommandProcessor:__newindex(key, value)
  if key == "clock" then
    self._native.clock = value
  elseif key == "mem_target" then
    self._native.target = value
  elseif key == "ext_int" then
    self._native.ext_int = value
  elseif key == "reset_n" then
    -- This model has no reset input.
    return
  else
    rawset(self, key, value)
  end
end

setmetatable(CommandProcessor --[[@as table]], {
  __call = function(cls, ...) return cls.new(...) end,
})

return require("lv.sc_module").wrap(CommandProcessor)
