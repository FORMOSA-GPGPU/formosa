-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Testing a simple.PrintBuf
local log_file = os.tmpname()
local dut = simple.PrintBuf("print_buf", 1024, log_file)

-- Generate 100 write data
local shared_str = "Hello, World!"
local wrote_addr = {}
local size = 10
for _ = 1, size do
  local addr = math.random(0, 1024)
  table.insert(wrote_addr, addr)
  local test_data = shared_str .. ", from address " .. addr .. "\n"
  -- Write the data to the memory via WriteBytes
  dut:write_bytes(addr, { string.byte(test_data, 1, #test_data) })
end
dut:flush()
dut:close_file()

-- Check the data
local file = io.open(log_file, "r")
if not file then error("Failed to open the log file") end
local read_data = file:read("*a")
file:close()
for _, addr in ipairs(wrote_addr) do
  local test_data = shared_str .. ", from address " .. addr .. "\n"
  local found = string.find(read_data, test_data)
  if not found then error("Failed to find the data at address " .. addr) end
end

-- Delete the log file if the test passes
os.remove(log_file)

print("Pass!")
