-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

trace.settings.streaming = true
trace.settings.file_write_period_ms = 1000
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.US)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = "WGInitializer"

local track = trace.Track("scenario", false)

local wg_resident_limit = 3

local threads_per_warp = 32
local warps_per_core = 32
local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local initiator = simple.Initiator("initiator")
local tick_agent = dbg.TickAgent("tick_agent", function() end)
local wg_initializer = formosa.WGInitializer("wg_initializer", {
  wg_resident_limit = wg_resident_limit,
  threads_per_warp = threads_per_warp,
  warps_per_core = warps_per_core,
  enable_trace = false,
})
local core = formosa.StubWarpCtrl("stub_core", {
  warps_per_core = warps_per_core,
})
core.clock = clock
initiator.clock = clock
tick_agent.clock = clock
wg_initializer.warp_ctrl_target = core

initiator.target = wg_initializer.port

local function create_write_payload(addr, raw_data)
  local size = 8
  local data = {}
  for _ = 1, size do
    local byte = bit.band(raw_data, 0xff)
    table.insert(data, byte)
    raw_data = bit.arshift(raw_data, 8) -- Shift right by 8 bits
  end
  return { addr = addr, data = data } -- write payload
end

local function create_read_payload(addr)
  return { addr = addr, size = 8 } -- read payload
end

local function get_read_bytes_to_int(bytes)
  local return_value = 0
  local read_data = bytes
  if read_data == nil then
    return nil -- No data available
  end
  for i = 1, 8 do
    local byte = read_data[i]
    if byte < 0 or byte > 255 then error("Byte value out of range: " .. byte) end
    return_value = return_value + byte * (256 ^ (i - 1)) -- Convert byte to integer
  end
  return return_value
end

local function wait_until(addr, value)
  initiator:add_payload({
    poll = true,
    addr = addr,
    value = value,
  })
  sc.start()
end

local function test_hw_info_csr_read()
  print("--- Testing HW Info CSR Read ---")

  local hwinfo_tbl = {
    { addr = 0x58, expected = wg_resident_limit }, -- wg_resident_limit
  }
  for _, entry in ipairs(hwinfo_tbl) do
    initiator:add_payload(create_read_payload(entry.addr))
  end

  local results = {}
  tick_agent.cb = function()
    local data = initiator:get_read_data()
    if data ~= nil then
      table.insert(results, data)
      if #results == #hwinfo_tbl then
        sc.pause()
        tick_agent.cb = function() end
      end
    end
  end

  sc.start()

  for i, entry in ipairs(hwinfo_tbl) do
    local data = results[i]
    assert(data ~= nil, "Expected read data but got nil")
    local value = get_read_bytes_to_int(data)
    print(
      string.format(
        "Read HW Info CSR at 0x%02x: expected %d, got %d",
        entry.addr,
        entry.expected,
        value
      )
    )
    assert(
      value == entry.expected,
      string.format("Expected %d, got %d for addr 0x%02x", entry.expected, value, entry.addr)
    )
  end
end

local function test_scenario(scenario_name)
  trace.event_begin(track, "WGInitializer", scenario_name)
  print("--- Testing Scenario: " .. scenario_name .. " ---")
  core:set_scenario(scenario_name)

  -- Start simulation
  wait_until(0x0, 0)

  -- Enqueue a workgroup
  initiator:add_payload(create_write_payload(0x8, 0x1000)) -- EnqPC
  initiator:add_payload(create_write_payload(0x10, 0x2000)) -- EnqInfoPtr
  initiator:add_payload(create_write_payload(0x18, 256)) -- EnqWGSize
  initiator:add_payload(create_write_payload(0x0, 1)) -- EnqValid

  if scenario_name == "MultipleDispatch" then
    wait_until(0x0, 0)
    -- Enqueue another workgroup
    initiator:add_payload(create_write_payload(0x8, 0x3000)) -- EnqPC
    initiator:add_payload(create_write_payload(0x10, 0x4000)) -- EnqInfoPtr
    initiator:add_payload(create_write_payload(0x18, threads_per_warp * warps_per_core)) -- EnqWGSize
    initiator:add_payload(create_write_payload(0x0, 1)) -- EnqValid
  end

  local num_wgs = (scenario_name == "MultipleDispatch") and 2 or 1

  -- Wait for the deque to complete
  for i = 1, num_wgs do
    print("Waiting for dequeue " .. i .. " of " .. num_wgs)
    wait_until(0x20, 1)

    -- Check the dequeue status
    initiator:add_payload(create_read_payload(0x28))
    initiator:add_payload(create_read_payload(0x30))
    initiator:add_payload(create_read_payload(0x38))
    initiator:add_payload(create_read_payload(0x40))
    initiator:add_payload(create_read_payload(0x48))
    initiator:add_payload(create_read_payload(0x50))

    -- Run until we got 6 results
    local bytes_arr = {}
    tick_agent.cb = function()
      local bytes = initiator:get_read_data()
      if bytes ~= nil then
        table.insert(bytes_arr, bytes)
        if #bytes_arr == 6 then
          sc.pause()
          tick_agent.cb = function() end
        end
      end
    end
    sc.start()

    local status = get_read_bytes_to_int(bytes_arr[1])
    local kernel_pc = get_read_bytes_to_int(bytes_arr[2])
    local info_ptr = get_read_bytes_to_int(bytes_arr[3])
    local mcause = get_read_bytes_to_int(bytes_arr[4])
    local mepc = get_read_bytes_to_int(bytes_arr[5])
    local mtval = get_read_bytes_to_int(bytes_arr[6])
    print("Dequeue Info:")
    print("  Status: " .. status)
    print("  Kernel PC: " .. kernel_pc)
    print("  Info Ptr: " .. info_ptr)
    print("  mcause: " .. mcause)
    print("  mepc: " .. mepc)
    print("  mtval: " .. mtval)

    if
      scenario_name == "AllEcall"
      or scenario_name == "AllBarrier"
      or scenario_name == "CannotRelease"
      or scenario_name == "CannotResume"
      or scenario_name == "CannotActivate"
    then
      assert(status == 0, "Expected status 0 for " .. scenario_name .. " scenario, got " .. status)
      assert(kernel_pc == 0x1000, "Expected kernel PC 0x1000, got " .. kernel_pc)
      assert(info_ptr == 0x2000, "Expected info_ptr 0x2000, got " .. info_ptr)
      assert(mcause == 11)
    elseif scenario_name == "MixedError" then
      assert(status == 1, "Expected status 1 for MixedError scenario, got " .. status)
      assert(mcause == 2, "Expected mcause 2 for MixedError scenario, got " .. mcause)
    elseif scenario_name == "MultipleDispatch" then
      assert(status == 0, "Expected status 0 for MultipleDispatch scenario, got " .. status)
      assert(mcause == 11)
    end

    initiator:add_payload(create_write_payload(0x20, 0)) -- Ack
    wait_until(0x20, 0)
  end

  trace.event_end(track)
  print("--- End Scenario: " .. scenario_name .. " ---\n")
end

test_hw_info_csr_read()

test_scenario("AllEcall")
test_scenario("AllBarrier")
test_scenario("MixedError")
test_scenario("MultipleDispatch")
test_scenario("CannotActivate")
test_scenario("CannotRelease")
test_scenario("CannotResume")
print("Pass!")
