-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local args = { ... }
local util = require("lv.util")
local runfile = util.runfile
local make_agent = ipc.Agent
local socket_path = os.tmpname()
os.remove(socket_path)

local ok, err = xpcall(function()
  -- Use the test ROM and a scripted Agent to read capabilities through the
  -- existing Ilha constructor, address map, fabric and SM implementations.
  util.runfile = function(path)
    if path == "bin/fwrom.elf" then return args[1] end
    return runfile(path)
  end
  local config = require("ilha.config"):override({ sm = { module = args[2] } })
  local reader
  ipc.Agent = function(name)
    local clock =
      sc.clock(name .. "_clock", sc.time(config.system.clock_period_ns, sc.time_unit.NS))
    reader = simple.Initiator(name .. "_reader")
    reader.clock = clock
    local host_memory = simple.Memory(name .. "_host_memory", { size = 4096 })
    host_memory.clock = clock
    return setmetatable({
      _clock = clock,
      _reader = reader,
      _host_memory = host_memory,
      port = host_memory.port,
      start = function() end,
    }, {
      __newindex = function(_, key, target)
        assert(key == "target", "unexpected scripted Agent property: " .. key)
        reader.target = target
      end,
    })
  end
  local system = require("ilha.system")("ilha", config, {
    agent_socket_path = socket_path,
  })
  util.runfile = runfile
  ipc.Agent = make_agent
  local addr = require("ilha.addr_map")
  for _, offset in ipairs({ addr.system_info_off_num_sm, addr.system_info_off_threads_per_warp }) do
    reader:add_payload({ addr = addr.system_info_base + offset, size = 8 })
  end
  system:start(100)
  local function u64(bytes)
    local value = 0
    for i = 8, 1, -1 do
      value = value * 256 + bytes[i]
    end
    return value
  end
  assert(u64(assert(reader:get_read_data())) == config.system.num_sm)
  assert(u64(assert(reader:get_read_data())) == config.system.threads_per_warp)
  print("Ilha system/SM regression PASS: " .. args[2])
end, debug.traceback)

-- Construction and capability assertions must leave no test resources behind.
util.runfile = runfile
ipc.Agent = make_agent
os.remove(socket_path)
if not ok then error(err, 0) end
