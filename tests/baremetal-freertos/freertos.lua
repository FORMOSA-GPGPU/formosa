local args = { ... }

local cpu = cp.CommandProcessor(
  "cp",
  0,
  { core_trace = false, rsp_enable = false, rsp_port = 1234, rsp_trace = false }
)

local mem_size = 0x20000000
local mem = simple.Memory("memory", {
  size = mem_size,
  latency = 1,
  fifo_size = 1,
})

local printbuf = simple.PrintBuf("printbuf", 1)
local clint = simple.Clint("clint", 1)
local md = dbg.MemoryDebugger("md")

local xbar = simple.XBar("xbar", 2, {
  { addr = 0x02000000, size = 0xC000 }, -- CLINT
  { addr = 0x10000000, size = 0x4 }, -- PRINTBUF
  { addr = 0x20000000, size = 0x38 }, -- DUMMY address for cpu.csr_slave
  { addr = 0x80000000, size = mem_size }, -- MEMORY
})

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

print("Bind ports")
xbar.clock = clock
cpu.clock = clock
mem.clock = clock
printbuf.clock = clock
clint.clock = clock

xbar.mem_side[1].target = clint.port
xbar.mem_side[2].target = printbuf.port
xbar.mem_side[3].target = cpu.csr_slave
xbar.mem_side[4].target = mem.port
cpu.target = xbar.core_side[1].port
md.target = xbar.core_side[2].port

local elf = workload.ELF(args[1])
print("Load elf to mem")
md:load_elf(elf)
print("Set PC")
cpu:set_pc(elf.entry)

local gnd = sc.signal("null")

cpu.ext_int = gnd
cpu.sw_int = clint.msip_irq[1]
cpu.timer_int = clint.timer_irq[1]

print("Start simulation")
sc.start()
print("End simulation")
