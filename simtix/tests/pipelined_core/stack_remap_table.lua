-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local initiator = simple.Initiator("initiator")
local table = simtix.StackRemapTable("stack_remap_table", {
  entries = 4,
  region_size = 0x10000,
})

initiator.clock = clock
initiator.target = table.mmio_port

local function u64_le(value)
  local bytes = {}
  for i = 1, 8 do
    bytes[i] = value % 256
    value = math.floor(value / 256)
  end
  return bytes
end

local function wait_for_completion(target)
  for _ = 1, 32 do
    if initiator:completed_count() >= target then return end
    sc.start(period)
  end
  error("Timed out waiting for stack remap MMIO transaction")
end

local stack_base = 0x81000000
assert(not table:matches(stack_base))

local target = initiator:completed_count() + 1
initiator:add_payload({
  addr = 0,
  data = u64_le(stack_base + 1),
})
wait_for_completion(target)

assert(table:matches(stack_base))
assert(table:matches(stack_base + 0xFFFF))
assert(not table:matches(stack_base + 0x10000))
assert(not table:matches(0x82000000))

target = initiator:completed_count() + 1
initiator:add_payload({
  addr = 0,
  data = u64_le(0),
})
wait_for_completion(target)
assert(not table:matches(stack_base))
