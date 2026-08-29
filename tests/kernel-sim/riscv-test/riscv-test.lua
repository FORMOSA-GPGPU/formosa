local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

local args = { ... }
local elf_path = util.runfile("bin/riscv-test.kernel-sim.elf")
if args[1] ~= nil and string.sub(args[1], 1, 1) ~= "-" then elf_path = table.remove(args, 1) end

---@type kernel-sim.system
local system = require("kernel-sim.system")("riscv-test.lua", args)

local WG_OKAY = 0

local infos = ksim.run_kernel(system, elf_path, 0, 1, 1)

local info = assert(infos[1], "missing dequeue info")
print(
  string.format(
    "riscv-test: status=%d mcause=%d mepc=0x%x mtval=0x%x",
    info.status,
    info.mcause,
    info.mepc,
    info.mtval
  )
)

if info.status ~= WG_OKAY then
  error(
    string.format(
      "riscv-test failed: status=%d mcause=%d mepc=0x%x mtval=0x%x",
      info.status,
      info.mcause,
      info.mepc,
      info.mtval
    ),
    0
  )
end

print("Pass!")
print(system.stats)
