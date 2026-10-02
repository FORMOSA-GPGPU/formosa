-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Exercise concurrent work-group dispatch, barrier release, and completion
-- backpressure through the real WG initializer and pipelined core.
local lanes_per_warp = 32
local arch = { num_warps = 32, num_lanes = lanes_per_warp }
local clock = sc.clock("clock", sc.time(1, sc.time_unit.NS))
local core = simtix.PipelinedCore("core", arch, { num_subcores = 1 })
local imem = simple.Memory("imem", { size = 0x4000, latency = 0 })
local dmem = simple.Memory("dmem", { size = 64, latency = 0 })
local initiator = simple.Initiator("initiator")
local wgi = ilha.WGInitializer("wgi", {
  warps_per_core = arch.num_warps,
  threads_per_warp = lanes_per_warp,
  wg_resident_limit = 3,
  enable_trace = false,
})

core.clock = clock
imem.clock = clock
dmem.clock = clock
initiator.clock = clock
core.imem = imem.port
local subcore = core.subcores[1]
subcore:sched_init(function() return simtix.Lrr(arch) end)
subcore:lsu_init(function(name) return simtix.SimpleLsu(name, arch) end)
subcore:arbitrator_init(function(name) return simtix.SimpleArbitrator(name, arch) end)
subcore.dmem = dmem.port
wgi.warp_ctrl_target = core.warp_ctrl
initiator.target = wgi.port

local function bytes_le(value, size)
  local bytes = {}
  for i = 1, size do
    bytes[i] = value % 256
    value = math.floor(value / 256)
  end
  return bytes
end

local function load_program(pc, work_instructions, barrier_count)
  local instructions = {}
  local function emit(word)
    for _, byte in ipairs(bytes_le(word, 4)) do
      instructions[#instructions + 1] = byte
    end
  end
  for _ = 1, work_instructions do
    emit(0x00000013) -- nop
  end
  for _ = 1, barrier_count do
    emit(0x0000102b) -- fsa.bar 0, 0
  end
  emit(0x00000073) -- ecall
  imem:write_bytes(pc, instructions)
end

local fast_pc, work_pc, barrier_pc = 0x1000, 0x2000, 0x3000
load_program(fast_pc, 0, 0)
load_program(work_pc, 128, 0)
load_program(barrier_pc, 256, 2)

local function run(ns) sc.start(sc.time(ns, sc.time_unit.NS)) end

local function write_csr(addr, value)
  initiator:add_payload({ addr = addr, data = bytes_le(value, 8) })
end

local function read_csr(addr)
  initiator:add_payload({ addr = addr, size = 8 })
  run(20)
  local bytes = assert(initiator:get_read_data(), "CSR read did not complete")
  local value = 0
  for i = 8, 1, -1 do
    value = value * 256 + bytes[i]
  end
  return value
end

local function wait_csr(addr, expected)
  for _ = 1, 500 do
    if read_csr(addr) == expected then return end
  end
  error(string.format("CSR 0x%x did not become %d", addr, expected))
end

local function launch(pc, id, group_size)
  write_csr(0x08, pc)
  write_csr(0x10, id)
  write_csr(0x18, group_size)
  write_csr(0x00, 1)
  wait_csr(0x00, 0)
end

local function drain(first_id, last_id)
  local seen = {}
  for _ = first_id, last_id do
    wait_csr(0x20, 1)
    local status = read_csr(0x28)
    local id = read_csr(0x38)
    local cause = read_csr(0x40)
    assert(status == 0, "work-group " .. id .. " failed")
    assert(cause == 11, "work-group " .. id .. " did not complete by ecall")
    assert(
      id >= first_id and id <= last_id and not seen[id],
      "unexpected or duplicate work-group " .. id
    )
    seen[id] = true
    write_csr(0x20, 0)
    run(1000)
  end
  assert(read_csr(0x20) == 0, "unexpected extra completion")
end

-- Keep the first completion unacknowledged while later work-groups run.
-- This exercises the normal dequeue backpressure path without inspecting
-- the initializer's internal state.
for id = 1, 5 do
  launch(fast_pc, id, lanes_per_warp)
  run(1000)
end
wait_csr(0x20, 1)
launch(work_pc, 6, lanes_per_warp)
launch(barrier_pc, 7, lanes_per_warp)
run(5000)
drain(1, 7)

-- Reuse the released warp/WG slots with multi-warp groups. Each group must
-- pass both barriers independently before publishing its completion.
launch(barrier_pc, 8, 2 * lanes_per_warp)
launch(barrier_pc, 9, 3 * lanes_per_warp)
drain(8, 9)
print("Pass: concurrent work-groups completed exactly once")
