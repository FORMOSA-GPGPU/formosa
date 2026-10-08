-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- A pure-Lua composition of `nic.BankingRouter` plus interleaved
-- `simtix.Cache` banks. Requests keep their original addresses so each bank
-- can index `[tag | set | bank_id | offset]`. Memory-side traffic from every
-- bank is merged through `simple.Mux`. Cache maintenance CSRs enter through
-- `ilha.CacheController`, which broadcasts one decoded command to every bank
-- and waits for all completions.

-- BankedCache-only keys, plus Cache size aliases that would fight per-bank
-- `cache_size_bytes`. `set_index_shift` is derived from `num_banks`.
local COMPOSITION_KEYS = {
  cache_size_bytes = true,
  size_bytes = true,
  size = true,
  num_banks = true,
  num_froms = true,
  total_size = true,
  set_index_shift = true,
}

---@param num_banks integer
---@return integer
local function bank_id_bits(num_banks)
  assert(num_banks >= 1, "BankedCache: `num_banks` must be positive")
  local bits = 0
  local remaining = num_banks
  while remaining > 1 do
    assert(
      remaining % 2 == 0,
      string.format("BankedCache: `num_banks` (%d) must be a power of two", num_banks)
    )
    remaining = math.floor(remaining / 2)
    bits = bits + 1
  end
  return bits
end

---@param param simtix.banked_cache.param
---@param bank_size integer
---@param block_size integer
---@param set_index_shift integer
---@return simtix.Cache.Param
local function make_bank_param(param, bank_size, block_size, set_index_shift)
  local bank_param = {}
  for key, value in pairs(param) do
    if not COMPOSITION_KEYS[key] then bank_param[key] = value end
  end
  bank_param.cache_size_bytes = bank_size
  bank_param.block_size_bytes = block_size
  bank_param.set_index_shift = set_index_shift
  return bank_param
end

-- Keep cache-specific counters and derived formulas in the Lua composite.
local function add_aggregate_stats(group, banks)
  local counters = {
    { "total_reads", "Total number of non-atomic read requests" },
    { "total_writes", "Total number of non-atomic write requests" },
    { "total_atomics", "Total number of atomic requests" },
    { "total_cacheable_requests", "Total number of requests routed through the cache" },
    { "total_non_cacheable_requests", "Total number of requests routed around the cache" },
    { "total_hits", "Total number of hits in cache" },
    { "total_misses", "Total number of misses in cache" },
    { "primary_misses", "Number of misses that allocate a new MSHR entry" },
    { "secondary_misses", "Number of misses merged into an existing MSHR entry" },
    { "total_evictions", "Total number of cache lines evicted" },
    { "total_requests", "Total number of requests" },
  }
  local totals = {}
  for _, counter in ipairs(counters) do
    local name, description = counter[1], counter[2]
    local total = banks[1].stats:get(name)
    for i = 2, #banks do
      total = total + banks[i].stats:get(name)
    end
    totals[name] = group:add_integer_formula(name, description, total)
  end

  -- Weight by cache-routed requests; idle banks and bypass traffic do not
  -- dilute the rate. An entirely idle cache retains the native NaN result.
  group:add_real_formula(
    "hit_rate",
    "The percentage of cache-routed requests hit in the cache",
    totals.total_hits * 100 / totals.total_cacheable_requests
  )
end

---@class simtix.banked_cache.param : simtix.Cache.Param
---@field num_banks? integer   Number of interleaved banks (default: 4)
---@field num_froms? integer   Number of master (from) ports (default: 1)
---@field total_size? integer  Router address-space size in bytes

---@class simtix.BankedCache
---@field protected _sc_module sc.Module
---@field protected _router nic.BankingRouter
---@field protected _mux simple.Mux
---@field protected _banks simtix.Cache[]
---@field protected _controller ilha.CacheController
---@field protected _clock sc.clock
---@field port sc.Socket
---@field mmio_port sc.Socket
---@field target sc.Socket
---@field clock sc.clock
---@field stats stats.Group Aggregate cache counters and bankN details.
---@overload fun(name: string, param: simtix.banked_cache.param): simtix.BankedCache
local BankedCache = {}

---@param name string
---@param param simtix.banked_cache.param
---@return simtix.BankedCache
function BankedCache.new(name, param)
  assert(type(param) == "table", "BankedCache requires a parameter table")

  local size = param.cache_size_bytes or 4096
  local block_size = param.block_size_bytes or 64
  local num_banks = param.num_banks or 4
  local num_froms = param.num_froms or 1
  local ways = param.ways or 4
  local set_index_shift = bank_id_bits(num_banks)

  assert(size > 0, "BankedCache: `cache_size_bytes` must be positive")
  assert(num_froms > 0, "BankedCache: `num_froms` must be positive")
  assert(block_size > 0, "BankedCache: `block_size_bytes` must be positive")
  assert(
    param.total_size == nil or param.total_size > 0,
    "BankedCache: `total_size` must be positive"
  )
  assert(
    size % num_banks == 0,
    string.format(
      "BankedCache: `cache_size_bytes` (%d) must be divisible by num_banks (%d)",
      size,
      num_banks
    )
  )

  local bank_size = math.floor(size / num_banks)
  local bank_geometry = block_size * ways
  assert(
    bank_size % bank_geometry == 0,
    string.format(
      "BankedCache: per-bank size (%d) must be divisible by block_size*ways (%d)",
      bank_size,
      bank_geometry
    )
  )

  ---@type simtix.BankedCache
  local self = setmetatable({}, BankedCache --[[@as table]])

  self._router = nic.BankingRouter("router", {
    num_froms = num_froms,
    total_size = param.total_size,
    num_tos = num_banks,
    bank_line_size = block_size,
    preserve_original_address = true,
  })
  self._mux = simple.Mux("mux", {
    fifo_size = param.pipeline_queue_size or 16,
  })
  self._controller = ilha.CacheController("controller", {
    num_banks = num_banks,
  })

  self.stats = stats.Group(name)

  local bank_param = make_bank_param(param, bank_size, block_size, set_index_shift)
  self._banks = {}
  for i = 1, num_banks do
    local bank = simtix.Cache(string.format("bank%d", i - 1), bank_param)
    self._banks[i] = bank
    -- Property setter `to` on the router binds the next bank target socket.
    self._router.to = bank.port
    bank.target = self._mux.from
    self._controller.bank = bank.cmd_port
    self.stats:add_sub_group(bank.stats)
  end

  add_aggregate_stats(self.stats, self._banks)

  return self
end

function BankedCache:get_port() return self._router.from end

function BankedCache:get_mmio_port() return self._controller.mmio_port end

function BankedCache:set_target(target) self._mux.to = target end

function BankedCache:set_clock(clock)
  self._clock = clock
  self._controller.clock = clock
  for _, bank in ipairs(self._banks) do
    bank.clock = clock
  end
end

function BankedCache:__index(key)
  if key == "port" then
    return self:get_port()
  elseif key == "mmio_port" then
    return self:get_mmio_port()
  elseif key == "clock" then
    return self._clock
  else
    return BankedCache[key]
  end
end

function BankedCache:__newindex(key, value)
  if key == "target" then
    self:set_target(value)
  elseif key == "clock" then
    self:set_clock(value)
  else
    rawset(self, key, value)
  end
end

setmetatable(BankedCache --[[@as table]], {
  __call = function(cls, ...) -- Call constructor
    return cls.new(...)
  end,
})

return require("lv.sc_module").wrap(BankedCache)
