-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local TIMEOUT = 64

local initiator = simple.Initiator("initiator")
initiator.clock = clock

local cache_controller = ilha.CacheController("cache_controller", {
  verbose = true,
  num_banks = 2,
})
cache_controller.clock = clock

local banks = {
  ilha.CacheCommandEndpoint("bank0"),
  ilha.CacheCommandEndpoint("bank1"),
}
cache_controller.bank = banks[1].port
cache_controller.bank = banks[2].port

initiator.target = cache_controller.mmio_port

local function to_le_bytes(value, n)
  local bytes = {}
  for _ = 1, n do
    table.insert(bytes, value % 0x100)
    value = math.floor(value / 0x100)
  end
  return bytes
end

local function from_le_bytes(bytes)
  assert(bytes ~= nil, "missing read response")
  local value = 0
  for i = #bytes, 1, -1 do
    value = value * 256 + bytes[i]
  end
  return value
end

local function wait_completed(target, label)
  for _ = 1, TIMEOUT do
    if initiator:completed_count() >= target then return end
    sc.start(period)
  end
  error(
    string.format(
      "%s: timed out waiting for completions got=%d expected=%d",
      label,
      initiator:completed_count(),
      target
    )
  )
end

local function wait_read(label)
  for _ = 1, TIMEOUT do
    local data = initiator:get_read_data()
    if data ~= nil then return data end
    sc.start(period)
  end
  error(label .. ": timed out waiting for read response")
end

local function write_bytes(addr, bytes, label)
  local target = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, data = bytes })
  wait_completed(target, label)
end

local function read_bytes(addr, size, label)
  local target = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, size = size })
  wait_completed(target, label)
  return wait_read(label)
end

local function write_u64(addr, value, label) write_bytes(addr, to_le_bytes(value, 8), label) end

local function read_u64(addr, label) return from_le_bytes(read_bytes(addr, 8, label)) end

local function wait_until(predicate, label)
  for _ = 1, TIMEOUT do
    if predicate() then return end
    sc.start(period)
  end
  assert(predicate(), label .. ": timed out")
end

local function wait_start(value, label)
  wait_until(function() return read_u64(0x00, label) == value end, label)
end

local function take_cmd(bank, label)
  local cmd
  wait_until(function()
    cmd = bank:try_take()
    return cmd ~= nil
  end, label)
  return cmd
end

local function expect_no_cmd(bank, cycles, label)
  for _ = 1, cycles do
    assert(bank:try_take() == nil, label .. ": unexpected extra command")
    sc.start(period)
  end
end

local function assert_cmd(cmd, addr, size, opcode, label)
  assert(cmd ~= nil, label .. ": missing command")
  assert(
    cmd.addr == addr,
    string.format("%s: addr got=%s expected=%s", label, tostring(cmd.addr), tostring(addr))
  )
  assert(
    cmd.size == size,
    string.format("%s: size got=%s expected=%s", label, tostring(cmd.size), tostring(size))
  )
  assert(
    cmd.opcode == opcode,
    string.format("%s: opcode got=%s expected=%s", label, tostring(cmd.opcode), tostring(opcode))
  )
end

local function program_command(addr, size, opcode, label)
  write_u64(0x08, addr, label .. " addr")
  write_u64(0x10, size, label .. " size")
  write_u64(0x18, opcode, label .. " opcode")
  write_bytes(0x00, { 0x1 }, label .. " start")
end

local function expect_csr(start, addr, size, opcode, label)
  assert(read_u64(0x00, label .. " start") == start, label .. ": unexpected START")
  assert(read_u64(0x08, label .. " addr") == addr, label .. ": unexpected ADDR")
  assert(read_u64(0x10, label .. " size") == size, label .. ": unexpected SIZE")
  assert(read_u64(0x18, label .. " opcode") == opcode, label .. ": unexpected OPCODE")
end

local function complete_all(ok)
  for _, bank in ipairs(banks) do
    bank:complete(ok)
  end
end

-- Illegal MMIO must not modify CSR, and the next legal request must still complete.
write_bytes(0x04, to_le_bytes(0xdeadbeef, 8), "unaligned write")
write_bytes(0x20, to_le_bytes(1, 8), "out-of-range write")
write_bytes(0x08, {}, "zero-length write")
write_bytes(0x08, { 1, 2, 3 }, "3-byte write")
write_bytes(0x01, { 0x1 }, "unaligned start write")
expect_csr(0, 0, 0, 0, "after illegal MMIO")

-- Legal short accesses only update the addressed bytes of one register.
write_u64(0x08, 0x1122334455667788, "full addr seed")
write_bytes(0x08, { 0xdd, 0xcc, 0xbb, 0xaa }, "4-byte addr write")
assert(
  read_u64(0x08, "short addr read") == 0x11223344aabbccdd,
  "4-byte write must not clobber high bytes"
)
write_bytes(0x10, { 0x22, 0x11 }, "2-byte size write")
assert(read_u64(0x10, "short size read") == 0x1122, "2-byte write only updates low bytes")
write_bytes(0x18, { 0x01 }, "1-byte opcode write")
assert(read_u64(0x18, "short opcode read") == 0x01, "1-byte write only updates the first byte")
assert(from_le_bytes(read_bytes(0x08, 4, "4-byte addr read")) == 0xaabbccdd, "4-byte read")
assert(from_le_bytes(read_bytes(0x10, 2, "2-byte size read")) == 0x1122, "2-byte read")
assert(from_le_bytes(read_bytes(0x18, 1, "1-byte opcode read")) == 0x01, "1-byte read")

-- Restore a known CSR image before command tests.
write_u64(0x08, 0, "clear addr")
write_u64(0x10, 0, "clear size")
write_u64(0x18, 0, "clear opcode")

-- 1. NOP does not dispatch to banks and still clears START.
program_command(0x100, 0x40, 0, "nop")
wait_start(0, "nop idle")
expect_no_cmd(banks[1], 4, "nop bank0")
expect_no_cmd(banks[2], 1, "nop bank1")
assert(banks[1]:received_count() == 0 and banks[2]:received_count() == 0, "NOP must not dispatch")

-- 2. Invalid opcode keeps the same contract as NOP.
program_command(0x200, 0x40, 87, "invalid-opcode")
wait_start(0, "invalid-opcode idle")
expect_no_cmd(banks[1], 4, "invalid-opcode bank0")
expect_no_cmd(banks[2], 1, "invalid-opcode bank1")
assert(
  banks[1]:received_count() == 0 and banks[2]:received_count() == 0,
  "invalid opcode must not dispatch"
)

-- 3. Flush is broadcast once to every bank; START stays set until the last completion.
program_command(0x100, 0x80, 1, "flush")
local flush0 = take_cmd(banks[1], "flush bank0")
local flush1 = take_cmd(banks[2], "flush bank1")
assert_cmd(flush0, 0x100, 0x80, 1, "flush bank0")
assert_cmd(flush1, 0x100, 0x80, 1, "flush bank1")
assert(banks[1]:received_count() == 1, "flush bank0 must receive one command")
assert(banks[2]:received_count() == 1, "flush bank1 must receive one command")
assert(read_u64(0x00, "flush start after dispatch") == 1, "START must stay set after dispatch")

write_u64(0x00, 0, "busy clear start")
write_bytes(0x00, { 0x0 }, "busy 1-byte clear start")
assert(read_u64(0x00, "busy start remains") == 1, "software cannot clear START while busy")

banks[1]:complete(true)
for _ = 1, 8 do
  assert(read_u64(0x00, "partial flush start") == 1, "START must wait for the last bank")
  assert(banks[2]:try_take() == nil, "flush must not redispatched after first completion")
  sc.start(period)
end
banks[2]:complete(true)
wait_start(0, "flush idle")
expect_no_cmd(banks[1], 4, "flush extra bank0")
expect_no_cmd(banks[2], 1, "flush extra bank1")
assert(
  banks[1]:received_count() == 1 and banks[2]:received_count() == 1,
  "flush must not duplicate dispatch"
)

-- 4. Invalidate uses the same broadcast/completion path.
program_command(0x180, 0x80, 2, "invalidate")
assert_cmd(take_cmd(banks[1], "invalidate bank0"), 0x180, 0x80, 2, "invalidate bank0")
assert_cmd(take_cmd(banks[2], "invalidate bank1"), 0x180, 0x80, 2, "invalidate bank1")
complete_all(true)
wait_start(0, "invalidate idle")

-- 5. Backpressure retries must not duplicate the command.
banks[1].accept = false
banks[2].accept = false
program_command(0x300, 0x40, 1, "backpressure")
expect_no_cmd(banks[1], 8, "backpressure bank0")
expect_no_cmd(banks[2], 1, "backpressure bank1")
assert(read_u64(0x00, "backpressure start") == 1, "START stays set while banks apply backpressure")
assert(
  banks[1]:received_count() == 2 and banks[2]:received_count() == 2,
  "blocked banks must not take a command"
)

banks[1].accept = true
banks[2].accept = true
assert_cmd(take_cmd(banks[1], "backpressure bank0 take"), 0x300, 0x40, 1, "backpressure bank0")
assert_cmd(take_cmd(banks[2], "backpressure bank1 take"), 0x300, 0x40, 1, "backpressure bank1")
complete_all(true)
wait_start(0, "backpressure idle")
expect_no_cmd(banks[1], 4, "backpressure extra bank0")
expect_no_cmd(banks[2], 1, "backpressure extra bank1")
assert(
  banks[1]:received_count() == 3 and banks[2]:received_count() == 3,
  "backpressure retry must dispatch once"
)

-- Illegal MMIO after a completed command must not prevent the next legal command.
write_bytes(0x04, to_le_bytes(0x1, 8), "late unaligned write")
expect_csr(0, 0x300, 0x40, 1, "late illegal MMIO")
program_command(0x400, 0x20, 1, "after-error flush")
assert_cmd(take_cmd(banks[1], "after-error bank0"), 0x400, 0x20, 1, "after-error bank0")
assert_cmd(take_cmd(banks[2], "after-error bank1"), 0x400, 0x20, 1, "after-error bank1")
complete_all(true)
wait_start(0, "after-error idle")

print("Pass!")
