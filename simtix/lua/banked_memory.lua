-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- A pure-Lua composition of `nic.BankingRouter` plus a set of
-- `simtix.AtomicMemory` bank instances. It reproduces the behavior of the
-- native `simtix.BankedAtomicMemory` module without depending on it, letting
-- the banking topology be assembled entirely from the two exposed bindings.

---@class simtix.banked_memory.param
---@field size integer            Total memory size in bytes
---@field num_banks integer       Number of interleaved banks
---@field num_froms integer       Number of master (from) ports
---@field bank_line_size integer  Bank interleave granularity in bytes
---@field latency? integer        Per-bank latency in clock periods
---@field fifo_size? integer      Per-bank output FIFO size
---@field pftrace? boolean        Enable Perfetto trace on each bank

---@class simtix.BankedMemory
---@field protected _sc_module sc.Module
---@field protected _router nic.BankingRouter
---@field protected _banks simtix.AtomicMemory[]
---@field protected _clock sc.clock
---@field port sc.Socket
---@field clock sc.clock
---@field stats stats.Group
---@overload fun(name: string, param: simtix.banked_memory.param): simtix.BankedMemory
local BankedMemory = {}

---@param name string
---@param param simtix.banked_memory.param
---@return simtix.BankedMemory
function BankedMemory.new(name, param)
  assert(type(param) == "table", "BankedMemory requires a parameter table")

  local size = assert(param.size, "BankedMemory: `size` is required")
  local num_banks = assert(param.num_banks, "BankedMemory: `num_banks` is required")
  local num_froms = assert(param.num_froms, "BankedMemory: `num_froms` is required")
  local bank_line_size = assert(param.bank_line_size, "BankedMemory: `bank_line_size` is required")

  assert(size > 0, "BankedMemory: `size` must be positive")
  assert(num_banks > 0, "BankedMemory: `num_banks` must be positive")
  assert(num_froms > 0, "BankedMemory: `num_froms` must be positive")
  assert(bank_line_size > 0, "BankedMemory: `bank_line_size` must be positive")

  -- Stripe layout requires every bank to own a whole number of lines and the
  -- total size to be an integer multiple of `bank_line_size * num_banks`.
  assert(
    num_banks <= math.floor(size / bank_line_size),
    "BankedMemory: `size` too small to hold one line per bank"
  )
  local stripe_size = bank_line_size * num_banks
  assert(
    size % stripe_size == 0,
    string.format(
      "BankedMemory: `size` (%d) must be divisible by bank_line_size*num_banks (%d)",
      size,
      stripe_size
    )
  )

  local latency = param.latency or 1
  local fifo_size = param.fifo_size or 1
  local pftrace = param.pftrace or false

  ---@type simtix.BankedMemory
  local self = setmetatable({}, BankedMemory --[[@as table]])

  self._router = nic.BankingRouter("router", {
    num_froms = num_froms,
    total_size = size,
    num_tos = num_banks,
    bank_line_size = bank_line_size,
  })

  self.stats = stats.Group(name)

  local bank_size = math.floor(size / num_banks)
  self._banks = {}
  for i = 1, num_banks do
    local bank = simtix.AtomicMemory(string.format("bank%d", i - 1), {
      size = bank_size,
      latency = latency,
      fifo_size = fifo_size,
      pftrace = pftrace,
    })
    self._banks[i] = bank
    -- Property setter `to` on the router binds the next bank target socket.
    self._router.to = bank.port
    self.stats:add_sub_group(bank.stats)
  end

  return self
end

function BankedMemory:get_port() return self._router.from end

function BankedMemory:set_clock(clock)
  self._clock = clock
  for _, bank in ipairs(self._banks) do
    bank.clock = clock
  end
end

function BankedMemory:__index(key)
  if key == "port" then
    return self:get_port()
  elseif key == "clock" then
    return self._clock
  else
    return BankedMemory[key]
  end
end

function BankedMemory:__newindex(key, value)
  if key == "clock" then
    self:set_clock(value)
  else
    rawset(self, key, value)
  end
end

setmetatable(BankedMemory --[[@as table]], {
  __call = function(cls, ...) -- Call constructor
    return cls.new(...)
  end,
})

return require("lv.sc_module").wrap(BankedMemory)
