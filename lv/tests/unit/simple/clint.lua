-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clk", period)

local clint_base = 0x0
local clint_num_cores = 3

local md = dbg.MemoryDebugger("md")
local clint = simple.Clint("clint", clint_num_cores)

clint.clock = clock

md.target = clint.port

local function msip_addr(hart) return clint_base + hart * 4 end

local function mtimecmp_addr(hart) return clint_base + 0x4000 + hart * 8 end

local function mtime_addr() return clint_base + 0xBFF8 end

local msip_tests = {
  -- hart 0
  { addr = msip_addr(0), data = { 0x01, 0x00, 0x00, 0x00 }, irq_exp = { true, false, false } }, --- set hart 0
  { addr = msip_addr(0), data = { 0x00, 0x00, 0x00, 0x00 }, irq_exp = { false, false, false } }, --- clear hart 0
  -- hart 1
  { addr = msip_addr(1), data = { 0x01, 0x00, 0x00, 0x00 }, irq_exp = { false, true, false } }, --- set hart 1
  { addr = msip_addr(1), data = { 0x00, 0x00, 0x00, 0x00 }, irq_exp = { false, false, false } }, --- clear hart 1
  -- hart 2
  { addr = msip_addr(2), data = { 0x01, 0x00, 0x00, 0x00 }, irq_exp = { false, false, true } }, --- set hart 2
  { addr = msip_addr(2), data = { 0x00, 0x00, 0x00, 0x00 }, irq_exp = { false, false, false } }, --- clear hart 2
}

sc.start(sc.time(0, sc.time_unit.NS)) -- elaboration

print("Starting msip tests...")

for i, t in ipairs(msip_tests) do
  md:write_bytes(t.addr, t.data)
  sc.start(5 * period)

  for j = 1, clint_num_cores do
    local irq = clint.msip_irq[j]:read()
    local irq_exp = t.irq_exp[j]
    assert(
      irq == irq_exp,
      string.format(
        "Test %d failed: expected msip[%d]=%s, got %s",
        i,
        j - 1,
        tostring(irq_exp),
        tostring(irq)
      )
    )
  end
end

print("MSIP tests passed!")

local timer_tests = {
  { addr = mtimecmp_addr(0), data = { 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- set mtimecmp[0] = 0x1000
  { addr = mtimecmp_addr(1), data = { 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- set mtimecmp[1] = 0x2000
  { addr = mtimecmp_addr(2), data = { 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- set mtimecmp[2] = 0x3000
  { addr = mtime_addr(), data = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- set mtime = 0x00
}

print("Starting timer tests...")

for _, t in ipairs(timer_tests) do
  md:write_bytes(t.addr, t.data)
end

sc.start(1 * period)
local wait = 0
for i = 1, clint_num_cores do
  while clint.timer_irq[i]:read() == false do
    sc.start(period)
    wait = wait + 1
  end
  assert(
    wait == 0x1000 * i,
    string.format("Timer IRQ[%d] did not trigger at expected time, waited %d cycles", i - 1, wait)
  )
end

local timer_reset_tests = {
  { addr = mtimecmp_addr(0), data = { 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- reset mtimecmp[0] to 0
  { addr = mtimecmp_addr(1), data = { 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- reset mtimecmp[1] to 0
  { addr = mtimecmp_addr(2), data = { 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, --- reset mtimecmp[2] to
}

--- Clear one timer_irq at a time and check the others remain true
for i, t in ipairs(timer_reset_tests) do
  md:write_bytes(t.addr, t.data)
  sc.start(1 * period)

  for j = 1, i do
    local irq = clint.timer_irq[j]:read()
    assert(
      irq == false,
      string.format("Timer IRQ[%d] should be false after reset, got %s", j - 1, tostring(irq))
    )
  end
  for j = i + 1, clint_num_cores do
    local irq = clint.timer_irq[j]:read()
    assert(
      irq == true,
      string.format("Timer IRQ[%d] should still be true after reset, got %s", j - 1, tostring(irq))
    )
  end
end

print("Timer tests passed!")
