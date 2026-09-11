-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clk", period)

local clint_base = 0x0

local md = dbg.MemoryDebugger("md")
local clint = simple.VClint("clint")

clint.clock = clock

md.target = clint.port

local function msip_addr(hart) return clint_base + hart * 4 end

local function mtimecmp_addr(hart) return clint_base + 0x4000 + hart * 8 end

local function mtime_addr() return clint_base + 0xBFF8 end

sc.start(sc.time(0, sc.time_unit.NS)) -- elaboration

print("Starting msip tests...")

md:write_bytes(msip_addr(0), { 0x01, 0x00, 0x00, 0x00 })
sc.start(5 * period)
assert(clint.msip_irq[1]:read() == true, "msip[0] set must raise irq")

md:write_bytes(msip_addr(0), { 0x00, 0x00, 0x00, 0x00 })
sc.start(5 * period)
assert(clint.msip_irq[1]:read() == false, "msip[0] clear must drop irq")

sc.start(sc.time(5, sc.time_unit.NS))
md:write_bytes(msip_addr(0), { 0x01, 0x00, 0x00, 0x00 })
sc.start(sc.ZERO_TIME)
assert(clint.msip_irq[1]:read() == true, "msip[0] set must raise irq immediately")
md:write_bytes(msip_addr(0), { 0x00, 0x00, 0x00, 0x00 })
sc.start(sc.ZERO_TIME)
assert(clint.msip_irq[1]:read() == false, "msip[0] clear must drop irq immediately")

print("MSIP tests passed!")

print("Starting timer tests...")

md:write_bytes(mtimecmp_addr(0), { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }) --- mtimecmp[0] = 0x100
md:write_bytes(mtime_addr(), { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }) --- mtime = 0
local mtime_before = md:read_bytes(mtime_addr(), 8)
local mtime_after = md:read_bytes(mtime_addr(), 8)
for i = 1, 8 do
  assert(mtime_before[i] == 0, "writing mtime must not advance it")
  assert(mtime_after[i] == mtime_before[i], "reading mtime must not advance it")
end

local wait = 0
while clint.timer_irq[1]:read() == false do
  sc.start(period)
  wait = wait + 1
end
assert(wait == 256, string.format("Timer IRQ[0] fired after %d cycles, expected 256", wait))

md:write_bytes(mtimecmp_addr(0), { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }) --- mtimecmp[0] = max
sc.start(1 * period)
assert(clint.timer_irq[1]:read() == false, "Timer IRQ[0] should be false after reset")

print("Timer tests passed!")
