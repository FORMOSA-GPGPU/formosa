-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local argparse = require("argparse")

---@type formosa.system.config
local base_config = require("formosa.config")

---@class kernel-sim.system
---@field num_threads integer
---@field stats stats.Group
---@field protected _initiator simple.Initiator
---@field protected _tick_agent dbg.TickAgent
---@field protected _period sc.time
---@field protected _clock sc.clock
---@field protected _reset_n sc.signal
---@field protected _mem simple.Memory
---@field protected _sm formosa.system.sm
---@field protected _config formosa.system.config
local System = {}
System.__index = System

---@param prog_name string
---@param args string[]
---@return kernel-sim.system
function System.new(prog_name, args)
  local parser = argparse(prog_name)
  parser:option("--sm", "Stream multiprocessor to simulate"):default("simtix.atomic_sm")
  parser:option("--num_warps", "Number of warps"):convert(tonumber):default(16)
  parser:option("--num_lanes", "Number of lanes"):convert(tonumber):default(4)
  local param = parser:parse(args)

  ---@type kernel-sim.system
  local self = setmetatable({}, System --[[@as table]])
  self._initiator = simple.Initiator("initiator")
  self._tick_agent = dbg.TickAgent("tick_agent", function() end)
  self._period = sc.time(10, sc.time_unit.NS)
  self._clock = sc.clock("clock", self._period)
  self._reset_n = sc.signal("reset_n")

  self._mem = simple.Memory("mem", { size = 0x40000, latency = 0 })

  --- Configure the SM based on the provided parameters and base configuration
  self._config = base_config
  self._config.warps_per_core = param.num_warps
  self._config.threads_per_warp = param.num_lanes
  self._config.threads_per_core = param.num_warps * param.num_lanes
  -- Scratch/info live above kernel image (see kernel-sim.runtime layout).
  self._config.non_cacheable_regions = {
    { addr = 0x23000, size = 0x1d000 }, -- scratch/data + WG info to 0x40000
  }

  ---@type formosa.system.sm_ctor
  local make_sm = require(param.sm)
  self._sm = make_sm("SM0", self._config, self._clock, self._reset_n, 0)

  self._initiator.clock = self._clock
  self._tick_agent.clock = self._clock
  self._initiator.target = self._sm.port

  self._mem.clock = self._clock
  self._sm.target = self._mem.port

  self.num_threads = param.num_lanes * param.num_warps
  self.stats = stats.Group("kernel-sim")

  if self._sm.stats then self.stats:add_sub_group(self._sm.stats) end

  if self._mem.stats then self.stats:add_sub_group(self._mem.stats) end

  self._reset_n:write(false)
  sc.start(5 * self._period)
  self._reset_n:write(true)

  return self
end

---@param elf workload.ELF
function System:load_elf(elf) self._mem:load_elf(elf) end

---@param addr integer
---@param bytes string
function System:write_bytes(addr, bytes)
  local byte_arr = {}
  for i = 1, #bytes do
    byte_arr[i] = string.byte(bytes, i)
  end
  self._mem:write_bytes(addr, byte_arr)
end

---@param addr integer
---@param size integer
---@return string
function System:read_bytes(addr, size)
  local byte_arr = self._mem:read_bytes(addr, size)
  return string.char(unpack(byte_arr))
end

local function bytes_to_u64(bytes)
  local value = 0
  for i = 8, 1, -1 do
    value = value * 256 + bytes[i]
  end
  return value
end

---@class kernel-sim.dequeue_info
---@field status integer
---@field kernel_pc integer
---@field info_ptr integer
---@field mcause integer
---@field mepc integer
---@field mtval integer

---@param kernel_pc integer
---@param info_ptr integer
---@param group_size integer
---@return kernel-sim.dequeue_info dequeue_info
function System:launch(kernel_pc, info_ptr, group_size)
  local function create_write_payload(addr, raw_data)
    local size = 8
    local data = {}
    for _ = 1, size do
      local byte = bit.band(raw_data, 0xff)
      table.insert(data, byte)
      raw_data = bit.arshift(raw_data, 8) -- Shift right by 8 bits
    end
    return { addr = addr, data = data } -- write payload
  end

  local function read_dequeue_info(wgi_base)
    -- Capture the WGI-visible trap result before acknowledging DEQ_VALID.
    local fields = {
      { name = "status", offset = 0x28 },
      { name = "kernel_pc", offset = 0x30 },
      { name = "info_ptr", offset = 0x38 },
      { name = "mcause", offset = 0x40 },
      { name = "mepc", offset = 0x48 },
      { name = "mtval", offset = 0x50 },
    }
    for _, field in ipairs(fields) do
      self._initiator:add_payload({ addr = wgi_base + field.offset, size = 8 })
    end

    local bytes_arr = {}
    self._tick_agent.cb = function()
      local bytes = self._initiator:get_read_data()
      if bytes ~= nil then
        table.insert(bytes_arr, bytes)
        if #bytes_arr == #fields then
          sc.pause()
          self._tick_agent.cb = function() end
        end
      end
    end
    sc.start()

    local info = {}
    for i, field in ipairs(fields) do
      assert(bytes_arr[i] ~= nil, "missing dequeue field " .. field.name)
      info[field.name] = bytes_to_u64(bytes_arr[i])
    end
    return info
  end

  local wgi_base = self._config.wgi_csr_base

  self._initiator:add_payload(create_write_payload(wgi_base + 0x08, kernel_pc))
  self._initiator:add_payload(create_write_payload(wgi_base + 0x10, info_ptr))
  self._initiator:add_payload(create_write_payload(wgi_base + 0x18, group_size))
  self._initiator:add_payload(create_write_payload(wgi_base + 0x0, 1))
  self._initiator:add_payload({ addr = wgi_base + 0x20, value = 1, poll = true })
  sc.start()

  local dequeue_info = read_dequeue_info(wgi_base)

  -- Ack the completion
  self._initiator:add_payload(create_write_payload(wgi_base + 0x20, 0))
  -- Wait for DEQ_VALID to clear so a following launch starts from a clean slot.
  self._initiator:add_payload({ addr = wgi_base + 0x20, value = 0, poll = true })
  sc.start()

  return dequeue_info
end

setmetatable(System --[[@as table]], {
  __call = function(cls, ...) --Call constructor
    return cls.new(...)
  end,
})

return System
