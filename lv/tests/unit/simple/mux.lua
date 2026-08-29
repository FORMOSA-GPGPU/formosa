-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

-- System Configuration
local SEGMENT_SIZE = 1024
local NUM_INITIATORS = 3
local MEM_SIZE = SEGMENT_SIZE * NUM_INITIATORS

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clk", period)

-- Instantiate components
local memory = simple.Memory("memory", { size = MEM_SIZE })
memory.clock = clock

local mux = simple.Mux("mux", { fifo_size = 32 })

local mon = dbg.Monitor("mon")
mux.to = mon.from
mon.to = memory.port

local initiators = {}
local payloads_per_initiator = {}

for i = 0, NUM_INITIATORS - 1 do
  local name = "initiator_" .. i
  local init = simple.Initiator(name)
  init.clock = clock
  init.target = mux.from -- multi_passthrough_target_socket allows this

  table.insert(initiators, init)
  payloads_per_initiator[i + 1] = {}
end

-- Function to generate random payload for a specific address range
local function generate_payload(addr, size)
  local data = {}
  for _ = 1, size do
    table.insert(data, bit.band(math.random(0, 0xff), 0xff))
  end
  return { addr = addr, data = data }
end

-- Phase 1: Issue Writes
print("Phase 1: Issuing writes from multiple initiators...")
for i = 1, NUM_INITIATORS do
  local init = initiators[i]
  -- Each initiator writes to its own 1KB segment
  local base_addr = (i - 1) * SEGMENT_SIZE
  local line_size = 64

  for j = 1, (SEGMENT_SIZE / line_size) do
    local addr = base_addr + (j - 1) * line_size
    local p = generate_payload(addr, line_size)
    init:add_payload(p)
    table.insert(payloads_per_initiator[i], p)
  end
end

-- Run simulation for writes
sc.start(100 * period)

-- Phase 2: Issue Reads
print("Phase 2: Issuing reads from multiple initiators...")
for i = 1, NUM_INITIATORS do
  local init = initiators[i]
  local saved_payloads = payloads_per_initiator[i]

  for _, p in ipairs(saved_payloads) do
    init:add_payload({ addr = p.addr, size = #p.data })
  end
end

-- Run simulation for reads
sc.start(100 * period)

print(sc.time_stamp())

-- Phase 3: Verification
print("Phase 3: Verifying data...")
for i = 1, NUM_INITIATORS do
  local init = initiators[i]
  local expected_payloads = payloads_per_initiator[i]

  print(string.format("Checking Initiator %d...", i - 1))

  for _, expected in ipairs(expected_payloads) do
    local actual_data = init:get_read_data()

    assert(
      actual_data ~= nil,
      string.format("Initiator %d: Missing read data for addr 0x%x", i - 1, expected.addr)
    )
    assert(
      #actual_data == #expected.data,
      string.format("Initiator %d: Size mismatch for addr 0x%x", i - 1, expected.addr)
    )

    for k = 1, #expected.data do
      print(string.format("    0x%02x == 0x%02x", actual_data[k], expected.data[k]))
      if actual_data[k] ~= expected.data[k] then
        error(
          string.format(
            "Initiator %d: Data mismatch at addr 0x%x index %d. Expected 0x%02x, got 0x%02x",
            i - 1,
            expected.addr,
            k,
            expected.data[k],
            actual_data[k]
          )
        )
      end
    end
    print(string.format("  Addr 0x%x: OK", expected.addr))
  end

  -- Ensure no extra data
  assert(
    init:get_read_data() == nil,
    string.format("Initiator %d has unexpected extra read data", i - 1)
  )
end

print("------------------------------------------------------")
print("Mux Test Pass!")
print("Simulation stops at", sc.time_stamp())
