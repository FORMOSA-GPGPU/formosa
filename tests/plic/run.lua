-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

require("cp.cores")
local Core = require("cp.command_processor")
local platform = require("plic.platform")

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local cpu = Core("cpu")
local ram = simple.Memory("ram", { size = platform.ram_size, latency = 1 })
local plic = simple.Plic("plic", { num_sources = 2, num_contexts = 1 })
local bus = simple.XBar("bus", 2, {
  { addr = platform.ram_base, size = platform.ram_size },
  { addr = platform.plic_base, size = platform.plic_size },
})

cpu.clock, ram.clock, plic.clock, bus.clock = clock, clock, clock, clock
cpu.mem_target = bus.core_side[1].port
local external_irq = sc.signal("external_irq", true)
plic.irq[1](external_irq)
cpu.ext_int(external_irq)
bus.mem_side[1].target = ram.port
bus.mem_side[2].target = plic.port
local debugger = dbg.MemoryDebugger("debugger")
debugger.target = bus.core_side[2].port

-- The platform owns and retains the channels connecting the module ports.
local sources = {}
local source_ports = plic.sources
for id = 1, 2 do
  sources[id] = sc.signal("source_" .. id, false)
  source_ports[id](sources[id])
end

local function read_mailbox(offset)
  local bytes = debugger:read_bytes(platform.mailbox + offset, 4)
  return bytes[1] + bytes[2] * 256 + bytes[3] * 65536 + bytes[4] * 16777216
end
local function write_mailbox(offset, value)
  assert(debugger:write_bytes(platform.mailbox + offset, {
    value % 256,
    math.floor(value / 256) % 256,
    math.floor(value / 65536) % 256,
    math.floor(value / 16777216) % 256,
  }) == 4)
end

local function wait_for(check)
  for _ = 1, 8192 do
    sc.start(16 * period)
    assert(
      read_mailbox(platform.error) == 0,
      "CPU PLIC failure code " .. read_mailbox(platform.error)
    )
    if check() then return end
  end
  error("CPU PLIC timeout")
end

local elf = workload.ELF(require("lv.util").runfile("tests/plic/rv64.elf"))
assert(debugger:load_elf(elf))
cpu:boot(elf.entry)
assert(external_irq:read())
sc.start(sc.ZERO_TIME)
assert(not external_irq:read(), "PLIC output did not initialize the shared IRQ low")

wait_for(function() return read_mailbox(platform.ready) == 1 end)
sources[1]:write(true)
sources[2]:write(true)
for seq, expected in ipairs({ 2, 1, 1 }) do
  if seq == 3 then
    -- Require a return to the foreground before triggering a fresh interrupt.
    wait_for(function() return read_mailbox(platform.returned) == 1 end)
    assert(read_mailbox(platform.handled) == 2)
    sources[1]:write(true)
  end

  wait_for(function() return read_mailbox(platform.claim_seq) == seq end)
  local id = read_mailbox(platform.claimed_id)
  assert(
    id == expected and read_mailbox(platform.log + (seq - 1) * 4) == expected,
    "wrong claim order"
  )

  -- Acknowledge only after PLIC has sampled the deasserted source level.
  sources[id]:write(false)
  sc.start(period)
  write_mailbox(platform.ack_seq, seq)
end

wait_for(function() return read_mailbox(platform.done) == 1 end)
assert(read_mailbox(platform.irq_entries) == 2, "expected two ISR entries")

-- Idle observation catches extra ISR entries even if they claim no source.
sc.start(256 * period)
assert(read_mailbox(platform.error) == 0 and read_mailbox(platform.claim_seq) == 3)
assert(read_mailbox(platform.irq_entries) == 2, "unexpected extra ISR")
assert(read_mailbox(platform.handled) == 3 and not external_irq:read())
print("CPU PLIC driver: claims 2, 1, 1; all completed and returned to main")
