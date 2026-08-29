-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@class lv.transactor_sm.config : formosa.system.config
---@field sys_config table

---@class lv.transactor_sm : formosa.system.sm
---@field protected _clock sc.clock
---@field protected _sc_module sc.Module
---@field protected _transactor formosa.SMTransactor
---@field protected _dummy_initiator simple.Initiator
---@overload fun(name: string, config: lv.transactor_sm.config, clock: sc.clock, reset_n: sc.signal, id: integer): lv.transactor_sm
local TransactorSM = {}

---@param name string
---@param config lv.transactor_sm.config
---@param clock sc.clock
---@param reset_n sc.signal
---@param id integer
---@return lv.transactor_sm
function TransactorSM.new(name, config, clock, reset_n, id)
  assert(config and config.sys_config, "config.sys_config is required")
  ---@type lv.transactor_sm
  local self = setmetatable({}, TransactorSM --[[@as table]])
  self._clock = clock
  self._transactor = formosa.SMTransactor("transactor", {})
  self._dummy_initiator = simple.Initiator("initiator")
  self._transactor.clock = clock
  self._dummy_initiator.clock = clock
  return self
end

function TransactorSM:set_target(target) self._dummy_initiator.target = target end

function TransactorSM:get_port() return self._transactor.slave_port end

function TransactorSM:__index(key)
  if key == "port" then
    return self:get_port()
  else
    return TransactorSM[key]
  end
end

function TransactorSM:__newindex(key, value)
  if key == "target" then
    self:set_target(value)
  else
    rawset(self, key, value)
  end
end

setmetatable(TransactorSM --[[@as table]], {
  __call = function(cls, ...) -- Call constructor
    return cls.new(...)
  end,
})

return require("lv.sc_module").wrap(TransactorSM)
