-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Writes into a private stack slot that no launched thread owns. The
-- simulator's private stack check must stop the run (see xmake.lua).

local ffi = require("ffi")
local addr_map = require("ilha.addr_map")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

local args = { ... }
local elf_path = table.remove(args, 1)
local case_name = table.remove(args, 1)
local case_id = assert(({ store = 0, amo = 1 })[case_name], "unknown case " .. tostring(case_name))

---@type kernel-sim.system
local system = require("kernel-sim.system")("stack_slot.lua", args)

ffi.cdef([[
typedef struct {
  uint64_t case_id;
  uint64_t stack_target;
} kargs_t;
]])

-- Launch half of the core's threads, so the last slot of a work-group region
-- belongs to none of them.
local num_threads = math.floor(system.num_threads / 2)
local stack_target = addr_map.stack_base
  + (system.num_threads - 1) * addr_map.per_thread_stack_size
  + 8

local kargs_addr = ksim.scratch_base
ksim.upload_data(system, kargs_addr, ffi.new("kargs_t", { case_id, stack_target }))
lv.info(string.format("stack target 0x%x", stack_target))

ksim.run_kernel(system, elf_path, kargs_addr, num_threads, num_threads)
error("the private stack check did not stop the " .. case_name .. " case")
