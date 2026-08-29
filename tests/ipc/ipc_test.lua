-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- ipc_test.lua
--
-- Agent utilizes libcomm V2 and can handle bi-directional transactions (full duplex), which is
-- tested using this script. Both device side and host side perform the same task of copying memory
-- blocks from [addr] to [128 + addr]. They test different transaction sizes, ranging from 1 byte to
-- 64 bytes, incremented by the power of 2, as specified by `sizes`. At the end of the test, both
-- device and host expect that their memory contains duplicated data at [addr] and [128 + addr], as
-- they are already moved by each other.

local args = { ... }
local agent_socket_path = os.tmpname()

local function test_dev()
  local sizes = { 1, 2, 4, 8, 16, 32, 64 }
  local resp_cnt = 0

  local period = sc.time(10, sc.time_unit.NS)
  local clock = sc.clock("clock", period)

  local initiator = simple.Initiator("initiator")
  initiator.clock = clock

  local agent = ipc.Agent("agent", {
    socket_path = agent_socket_path,
    timeout_ms = 1000,
    debug = false,
    probe_hook = function()
      -- Add read payloads when the agent is probed.
      for _, size in ipairs(sizes) do
        initiator:add_payload({ addr = size, size = size })
      end
    end,
  })

  local memory = simple.Memory("memory", {
    latency = 2,
    size = 256,
  })
  memory.clock = clock

  -- Fill the memory with random data
  memory:write_bytes(
    0,
    (function()
      local bytes = {}
      for i = 1, 256 do
        bytes[i] = math.random(0, 255)
      end
      return bytes
    end)()
  )

  local tick_agent = dbg.TickAgent("tick_agent", function()
    local bytes = initiator:get_read_data()
    if bytes ~= nil then
      -- Once read, copy the data to 128 + addr with write payloads
      local addr = 128 + #bytes
      initiator:add_payload({ addr = addr, data = bytes })
      resp_cnt = resp_cnt + 1
    end

    if resp_cnt == #sizes then
      local terminate = { addr = 0, data = {} }
      initiator:add_payload(terminate)
      resp_cnt = -1
    end
  end)

  tick_agent.clock = clock

  initiator.target = agent.port
  agent.target = memory.port

  sc.start()

  -- sc.start returns after host issues the Terminate command
  local m = memory:read_bytes(0, 256)
  for i = 1, 127 do
    local addr = i + 1 -- Lua table index starts from 1
    print(string.format("Dev:  0x%02x: 0x%02x == 0x%02x", i, m[addr], m[128 + addr]))
    if m[addr] ~= m[128 + addr] then return false end
  end

  print("Dev: pass!!")
  return true
end

local function test_host()
  local test_program = args[1]
  local exit_code = os.execute(string.format("%s %s", test_program, agent_socket_path))
  return exit_code == 0
end

local childpid = require("posix.unistd").fork()
if childpid == 0 then
  -- Child
  assert(test_host(), "Host test failed")
else
  -- Parent
  assert(test_dev(), "Device test failed")
  -- If parent isn't failing, return the child's return value so that child's return value can be
  -- reflected on the parent process.
  local _, _, ret = require("posix.sys.wait").wait(childpid)
  return ret
end
