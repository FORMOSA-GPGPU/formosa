-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")

-- Memory layout (256 KiB flat mem attached to SM system port).
-- Addresses must sit at/above SM dmem identity cutover (lmem_window =
-- 0x10000) so loads/stores and atomic ifetch do not hit the LMEM hole
-- [lmem_size, lmem_window) = [0xC000, 0x10000). 0x10000 is reserved for
-- sm_printbuf; kernel image starts just after that window.
--
-- 0x0003FFFF +----------------------------+
--            |                            |
--            |      Pointer to kargs      |
--            |           (8 B)            |
--            |                            |
-- 0x0003FFF8 +----------------------------+
-- 0x0003FFF7 +----------------------------+
--            |                            |
--            |  Number of active threads  |
--            |           (8 B)            |
--            |                            |
-- 0x0003FFF0 +----------------------------+
-- 0x0003FFEF +----------------------------+
--            |                            |
--            |         Local size         |
--            |           (8 B)            |
--            |                            |
-- 0x0003FFE8 +----------------------------+
-- 0x0003FFE7 +----------------------------+
--            |                            |
--            |          Group id          |
--            |           (8 B)            |
--            |                            |
-- 0x0003FFE0 +----------------------------+
-- 0x0003FFDF +----------------------------+
--            |                            |
--            |    Scratch/data memory     |
--            |       (~116.0 KB)          |
--            |                            |
-- 0x00023000 +----------------------------+
-- 0x00022FFF +----------------------------+
--            |                            |
--            |         Kernel ELF         |
--            |         (72.0 KB)          |
--            |                            |
-- 0x00011000 +----------------------------+
-- 0x00010FFF +----------------------------+
--            |     SM printbuf window     |
--            |          (4 KiB)           |
-- 0x00010000 +----------------------------+
--            |   LMEM window (unused in   |
--            |   kernel-sim flat harness) |
-- 0x00000000 +----------------------------+

---@class kernel-sim.runtime
local Runtime = {}

-- First system-path address after sm_printbuf (0x10000 + 4 KiB).
Runtime.kernel_base = 0x11000
-- Immediately after the common kernel image window (0x11000 + 0x12000).
Runtime.scratch_base = 0x23000
Runtime.info_ptr = 0x3ffe0

local WG_OKAY = 0 -- Work-group completed normally (no traps or exceptions)

--- Uploads data from FFI cdata to the system memory.
---@param system kernel-sim.system
---@param addr integer
---@param data ffi.cdata*
function Runtime.upload_data(system, addr, data)
  local bytes = ffi.string(ffi.cast("uint8_t*", data), ffi.sizeof(data))
  system:write_bytes(addr, bytes)
end

--- Downloads data from system memory into FFI cdata.
---@param system kernel-sim.system
---@param addr integer
---@param data ffi.cdata*
function Runtime.download_data(system, addr, data)
  local size = ffi.sizeof(data) or 0
  local bytes = system:read_bytes(addr, size)
  ffi.copy(data, bytes, size)
end

--- Loads an ELF and launches kernels in groups.
---@param system kernel-sim.system
---@param elf_path string
---@param kargs integer
---@param global_size integer
---@param local_size integer
---@return kernel-sim.dequeue_info[]
function Runtime.run_kernel(system, elf_path, kargs, global_size, local_size)
  local n = math.ceil(global_size / local_size)
  local rem = global_size
  local elf = workload.ELF(elf_path)

  system:load_elf(elf)

  -- Return per-work-group dequeue records for tests that need trap details.
  local dequeue_infos = {}
  for i = 0, n - 1 do
    local num_threads = math.min(rem, local_size)
    local group_id = ffi.new("uint64_t[1]", i)
    local clocal_size = ffi.new("uint64_t[1]", local_size)
    local cnum_threads = ffi.new("uint64_t[1]", num_threads)
    local ckargs = ffi.new("uint64_t[1]", kargs)

    -- Setup info data structure
    local info_ptr = Runtime.info_ptr
    system:write_bytes(info_ptr, ffi.string(ffi.cast("uint8_t*", group_id), 8))
    system:write_bytes(info_ptr + 0x8, ffi.string(ffi.cast("uint8_t*", clocal_size), 8))
    system:write_bytes(info_ptr + 0x10, ffi.string(ffi.cast("uint8_t*", cnum_threads), 8))
    system:write_bytes(info_ptr + 0x18, ffi.string(ffi.cast("uint8_t*", ckargs), 8))

    local dequeue_info = system:launch(elf.entry, info_ptr, num_threads)
    table.insert(dequeue_infos, dequeue_info)
    rem = rem - num_threads

    -- Stop dispatching remaining work-groups after an abnormal completion, but
    -- return the dequeue record so tests can inspect trap CSRs.
    if dequeue_info.status ~= WG_OKAY then return dequeue_infos end
  end
  assert(rem == 0)
  return dequeue_infos
end

return Runtime
