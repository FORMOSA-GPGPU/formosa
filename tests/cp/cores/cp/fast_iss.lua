-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

---@class cp.fast_iss : cp.core
---@field private _native cp.FastISS
---@field private _gnd sc.signal
local FastISS = { isa = "rv64im", num_mem_ports = 1, has_ext_int = true, has_reset = false }

if xmake then
  function main() return FastISS end
  return
end

function FastISS:__index(key)
  if key == "ext_int" then return self._native.ext_int end
  return FastISS[key]
end

function FastISS.new(_)
  assert(cp and cp.FastISS, "cp.FastISS is not available in this lv build")
  local self = setmetatable({}, FastISS)
  self._native = cp.FastISS("native", 0, {
    instruction_cost = sc.time(10, sc.time_unit.NS),
    quantum = sc.time(160, sc.time_unit.NS),
    local_regions = require("cp.memory_policy"),
  })
  self._gnd = sc.signal("gnd", false)
  self._native.sw_int(self._gnd)
  self._native.timer_int(self._gnd)
  return self
end

function FastISS:boot(entry) self._native:set_pc(entry) end

function FastISS:__newindex(key, value)
  if key == "clock" then
    self._native.clock = value
  elseif key == "mem_target" then
    self._native.target = value
  elseif key == "reset_n" then
    return
  elseif key == "ext_int" then
    error("ext_int is a port; use ext_int(signal)")
  else
    rawset(self, key, value)
  end
end

setmetatable(FastISS, { __call = function(cls, ...) return cls.new(...) end })
return require("lv.sc_module").wrap(FastISS)
