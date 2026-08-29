-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("trap.lua", { ... })

ffi.cdef([[
typedef struct {
  uint64_t case_id;
  uint64_t fault_base;
} kargs_t;
]])

local CAUSE_MISALIGNED_FETCH = 0
local CAUSE_ILLEGAL_INSTRUCTION = 2
local CAUSE_MISALIGNED_LOAD = 4
local CAUSE_MISALIGNED_STORE = 6
local WG_EXCEPTION = 1

local kargs_addr = ksim.scratch_base
local fault_base = ksim.scratch_base + 0x1000

local cases = {
  {
    name = "illegal",
    case_id = 0,
    mcause = CAUSE_ILLEGAL_INSTRUCTION,
    mtval = 0xffffffff,
  },
  {
    name = "instr_addr_misaligned",
    case_id = 1,
    mcause = CAUSE_MISALIGNED_FETCH,
    check_mtval = function(info)
      assert(
        info.mtval == info.mepc + 2,
        string.format("expected mtval=mepc+2, mepc=0x%x mtval=0x%x", info.mepc, info.mtval)
      )
    end,
  },
  {
    name = "load_addr_misaligned",
    case_id = 2,
    mcause = CAUSE_MISALIGNED_LOAD,
    mtval = fault_base + 1,
  },
  {
    name = "store_addr_misaligned",
    case_id = 3,
    mcause = CAUSE_MISALIGNED_STORE,
    mtval = fault_base + 1,
  },
}

for _, case in ipairs(cases) do
  local kargs = ffi.new("kargs_t", {
    case_id = case.case_id,
    fault_base = fault_base,
  })
  ksim.upload_data(system, kargs_addr, kargs)

  local infos = ksim.run_kernel(
    system,
    util.runfile("bin/trap.kernel-sim.elf"),
    kargs_addr,
    system.num_threads,
    system.num_threads
  )
  local info = assert(infos[1], "missing dequeue info for " .. case.name)

  print(
    string.format(
      "%s: status=%d mcause=%d mepc=0x%x mtval=0x%x",
      case.name,
      info.status,
      info.mcause,
      info.mepc,
      info.mtval
    )
  )

  assert(info.status == WG_EXCEPTION, case.name .. ": expected WG exception")
  assert(
    info.mcause == case.mcause,
    string.format("%s: expected mcause=%d, got %d", case.name, case.mcause, info.mcause)
  )

  if case.check_mtval then
    case.check_mtval(info)
  else
    assert(
      info.mtval == case.mtval,
      string.format("%s: expected mtval=0x%x, got 0x%x", case.name, case.mtval, info.mtval)
    )
  end
end

print("Pass!")
