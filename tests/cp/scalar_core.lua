-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local raw_args = { ... }
local args = {
  elf = nil,
  enable_konata_trace = true,
  konata_trace_out = "scalar_konata.log",
}

local i = 1
while i <= #raw_args do
  local arg = raw_args[i]
  if arg == "--konata-trace-out" then
    i = i + 1
    assert(raw_args[i], "missing value for --konata-trace-out")
    args.konata_trace_out = raw_args[i]
  elseif arg == "--enable-konata-trace" then
    args.enable_konata_trace = true
  elseif arg == "--disable-konata-trace" then
    args.enable_konata_trace = false
  elseif arg:sub(1, 2) == "--" then
    error("unknown option: " .. arg)
  elseif args.elf == nil then
    args.elf = arg
  else
    error("unexpected positional argument: " .. arg)
  end
  i = i + 1
end

assert(args.elf, "missing ELF path")

trace.settings.streaming = true
trace.settings.file_write_period_ms = 1000
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.US)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = "scalar_core"

local cpu = simtix.ScalarCore("scalar_core")
if args.enable_konata_trace then cpu:enable_konata_trace(args.konata_trace_out) end

local mem_size = 0x200000
local mem = simple.Memory("memory", {
  size = mem_size,
  latency = 1,
  fifo_size = 1,
  trace = true,
})

local printbuf = simple.PrintBuf("printbuf", 1, "printf.log")

local xbar = simple.XBar("xbar", 2, {
  { addr = 0x0, size = mem_size },
  { addr = 0x200000, size = 0x4 },
})

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

print("Bind ports")
cpu.clock = clock
mem.clock = clock
printbuf.clock = clock
xbar.clock = clock

xbar.mem_side[1].target = mem.port
xbar.mem_side[2].target = printbuf.port

-- ScalarCore binding now expects lv::TlmSink objects.
cpu.imem = xbar.core_side[1].port
cpu.dmem = xbar.core_side[2].port

local elf = workload.ELF(args.elf)
print("Load elf to mem")
mem:load_elf(elf)
print("Set PC")
cpu.pc = elf.entry

print("Start simulation")
sc.start(512 * 1024 * period)
sc.start(512 * 1024 * period)
sc.start(512 * 1024 * period)
print("End sim")
