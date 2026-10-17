-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Runs one barrier kernel over a full work-group. Each case's ctest regex
-- checks what the simulator reports (see CMakeLists.txt).

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

local args = { ... }
local elf_path = table.remove(args, 1)

---@type kernel-sim.system
local system = require("kernel-sim.system")("barrier.lua", args)

local infos = ksim.run_kernel(system, elf_path, 0, system.num_threads, system.num_threads)
local info = assert(infos[1], "missing dequeue info")
assert(
  info.status == 0,
  string.format(
    "work-group failed: status=%d mcause=%d mepc=0x%x",
    info.status,
    info.mcause,
    info.mepc
  )
)

-- Log through lv so this line stays ordered after the simulator's reports.
lv.info("Kernel completed")
