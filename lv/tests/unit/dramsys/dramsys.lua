-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

-- Testing DRAMSys
local dut = dramsys.DRAMSys("dram", {
  config = dramsys.RESOURCE_DIR .. "/ddr3-gem5-se.json",
})

-- Use MemoryDebugger for reading/writing DRAMSys
local md = dbg.MemoryDebugger("md")
md.target = dut.port

sc.start(sc.ZERO_TIME)

-- Generate 100 write data
local size = 100
local write_data = {}
for _ = 1, size do
  local byte = bit.band(math.random(0, 0xff), 0xff) -- Make sure byte is 8-bit wide
  table.insert(write_data, byte)
end

-- Make sure size of write data is correct
assert(size == #write_data)

-- Write the data to the memory via WriteBytes
md:write_bytes(0, write_data)

-- Read the data back via ReadBytes
local read_data = md:read_bytes(0, size)

-- Size must match
assert(#read_data == #write_data, "Size of read_data and write_data must match")

-- Content must match
for i, _ in ipairs(read_data) do
  assert(read_data[i] == write_data[i], "Content of read_data and write_data must match")
  print(string.format("0x%02x == 0x%02x", read_data[i], write_data[i]))
end

print("Pass!")
