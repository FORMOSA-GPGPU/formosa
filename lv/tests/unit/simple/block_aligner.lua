-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- System:
-- ┌─────────┐
-- │Initiator│
-- └────┬────┘
--      │
-- ┌────▼────┐
-- │ Aligner │
-- └────┬────┘
--      │
-- ┌────▼────┐
-- │   mem   │
-- └─────────┘

local bit = require("bit")
local MEM_SIZE = 65536
local ITER_PER_SIZE = 20
local BLOCK_SIZE_TO_TEST = { 2, 4, 8, 16, 32, 64 }

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local function new_system(blk_size, clock)
  local initiator = simple.Initiator("initiator_" .. blk_size)
  local aligner = simple.BlockAligner("BlockAligner_" .. blk_size, blk_size)
  local tick_agent = dbg.TickAgent("tick_agent_" .. blk_size, function() end)
  local memory = simple.Memory("memory_" .. blk_size, { size = MEM_SIZE })

  memory.clock = clock
  initiator.clock = clock
  tick_agent.clock = clock
  initiator.target = aligner.from
  aligner.to = memory.port

  return {
    memory = memory,
    initiator = initiator,
    tick_agent = tick_agent,
    aligner = aligner,
    blk_size = blk_size,
  }
end

local function random_payload(system)
  local sizes = { 1, 2, 4, 8, 16, 32, 64 }
  local size_upper_bound = system.blk_size
  local valid_sizes = {}
  for _, s in ipairs(sizes) do
    if s <= size_upper_bound then table.insert(valid_sizes, s) end
  end

  assert(#valid_sizes > 0, "No valid sizes <= size_upper_bound")

  local size = valid_sizes[math.random(#valid_sizes)]
  local addr = bit.band(math.random(0, system.memory.size - size), bit.bnot(size - 1))
  local data = {}
  for _ = 1, size do
    local byte = bit.band(math.random(0, 0xff), 0xff)
    table.insert(data, byte)
  end
  return {
    addr = addr,
    data = data,
  }
end

local function is_match(out, golden)
  if #out ~= #golden then return false end
  for i = 1, #out do
    if out[i] ~= golden[i] then return false end
  end
  return true
end

local systems = {}
for i, blk_size in ipairs(BLOCK_SIZE_TO_TEST) do
  systems[i] = new_system(blk_size, clock)
end

for i, system in ipairs(systems) do
  for _ = 1, ITER_PER_SIZE do
    local payload = random_payload(system)
    system.initiator:add_payload(payload) -- write payloads
    system.initiator:add_payload({ addr = payload.addr, size = #payload.data }) -- no data given, read payloads
    system.tick_agent.cb = function()
      local bytes = system.initiator:get_read_data()
      if bytes ~= nil then
        local mem_read = system.memory:read_bytes(payload.addr, #payload.data)
        assert(is_match(mem_read, bytes))
        assert(is_match(mem_read, payload.data))
        sc.pause()
        system.tick_agent.cb = function() end
      end
    end
    sc.start()
  end
  print("Passed for blk_size = ", system.blk_size)
end

print("Simulation stops at", sc.time_stamp())
print("------------------------------------------------------")
