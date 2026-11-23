-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Work-item CSRs keep one copy per lane (xlocalid_*) or one copy per warp
-- (xsgsize, xgroupid_*, xlocalsize_*, xglobaloffset_*). The kernel writes a
-- value unique to the lane or warp, then reads every CSR back.

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("workitem-csr.lua", { ... })

local row_size = 15 -- mhartid, xlanes, then the CSRs below
local thread_csrs = { "xlocalid_x", "xlocalid_y", "xlocalid_z" }
local warp_csrs = {
  "xsgsize",
  "xgroupid_x",
  "xgroupid_y",
  "xgroupid_z",
  "xlocalsize_x",
  "xlocalsize_y",
  "xlocalsize_z",
  "xglobaloffset_x",
  "xglobaloffset_y",
  "xglobaloffset_z",
}

local n = system.num_threads
local out_addr = ksim.scratch_base
ksim.upload_data(system, out_addr, ffi.new("uint64_t[?]", n * row_size))

local infos =
  ksim.run_kernel(system, util.runfile("bin/workitem-csr.kernel-sim.elf"), out_addr, n, n)
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

local failures = 0
local function check(tid, name, got, expected)
  if got ~= expected then
    failures = failures + 1
    print(string.format("thread %d: %s = 0x%x, expected 0x%x", tid, name, got, expected))
  end
end

-- Download one row at a time: read_bytes cannot unpack the whole output at once.
local row = ffi.new("uint64_t[?]", row_size)
for tid = 0, n - 1 do
  ksim.download_data(system, out_addr + tid * ffi.sizeof(row), row)
  check(tid, "mhartid", tonumber(row[0]), tid)
  local lanes = tonumber(row[1])
  local warp = math.floor(tid / lanes)
  for i, name in ipairs(thread_csrs) do
    check(tid, name, tonumber(row[1 + i]), 0x1000 * i + tid)
  end
  for i, name in ipairs(warp_csrs) do
    check(tid, name, tonumber(row[4 + i]), 0x10000 * i + warp)
  end
end

assert(failures == 0, string.format("%d work-item CSR values differ", failures))
print("Pass!")
