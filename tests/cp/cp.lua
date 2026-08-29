-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local args = { ... }

trace.settings.streaming = true
trace.settings.file_write_period_ms = 1000
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.US)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = "cp"

local tb = trace.Track("tb", false)

local cpu = cp.CommandProcessor("cp", 0, {
  core_trace = false,
  rsp_enable = false,
  rsp_port = 0,
  rsp_trace = false,
})

local mem_size = 0x200000
local mem = simple.Memory("memory", {
  size = mem_size,
  latency = 1,
  fifo_size = 1,
  trace = true,
})

local md = dbg.MemoryDebugger("md")

local xbar = simple.XBar("xbar", 2, {
  { addr = 0x0, size = mem_size },
  { addr = mem_size, size = 0x4 },
})

local printbuf = simple.PrintBuf("printbuf", 1)

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

print("Bind ports")
xbar.clock = clock
cpu.clock = clock
mem.clock = clock
printbuf.clock = clock

xbar.mem_side[1].target = mem.port
xbar.mem_side[2].target = printbuf.port
cpu.target = xbar.core_side[1].port
md.target = xbar.core_side[2].port

local elf = workload.ELF(args[1])
print("Load elf to mem")
md:load_elf(elf)
print("Set PC")
cpu:set_pc(elf.entry)

-- Bind interrupt source
local sig = sc.signal("int")
local gnd = sc.signal("null")
gnd:write(false)
cpu.ext_int = sig
cpu.sw_int = gnd
cpu.timer_int = gnd

local function interrupt()
  trace.event_begin(tb, "tb", "interrupt")
  sig:write(true)
  sc.start(64 * period)
  sig:write(false)
  trace.event_end(tb)
end

print("Start simulation")
trace.event_instant(tb, "tb", "Start simulation")
sc.start(512 * 1024 * period)
interrupt()
sc.start(512 * 1024 * period)
interrupt()
sc.start(512 * 1024 * period)
trace.event_instant(tb, "tb", "End sim")

print("End sim")
