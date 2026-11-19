-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local args = { ... }
local util = require("lv.util")
local runfile = util.runfile

local ok, err = xpcall(function()
  -- Use the test ROM and a scripted Agent to read capabilities through the
  -- existing Ilha constructor, address map, fabric and SM implementations.
  util.runfile = function(path)
    if path == "bin/fwrom.elf" then return args[1] end
    return runfile(path)
  end
  -- A third argument, "command_trace", puts the command-trace watcher on the
  -- scratch link.
  local command_trace = args[3] == "command_trace"
  local config = require("ilha.config"):override({
    sm = { module = args[2] },
    system = { trace = { commands = command_trace } },
  })
  local reader
  local function scripted_host()
    local name = "agent"
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
  local system = require("ilha.system")("ilha", config, { host = scripted_host })
  util.runfile = runfile
  local addr = require("ilha.addr_map")
  for _, offset in ipairs({
    addr.system_info_off_num_sm,
    addr.system_info_off_threads_per_warp,
    addr.system_info_off_global_alloc_base,
  }) do
    reader:add_payload({ addr = addr.system_info_base + offset, size = 8 })
  end
  -- With the command trace, scratch accesses pass through its watcher.
  local ring_entry = { 1, 2, 3, 4, 5, 6, 7, 8 }
  if command_trace then
    reader:add_payload({ addr = addr.cmd_ring_base, data = ring_entry })
    reader:add_payload({ addr = addr.cmd_ring_base, size = #ring_entry })
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
  assert(u64(assert(reader:get_read_data())) == config.system:global_alloc_base())
  if command_trace then
    assert(system._scratch_watcher ~= nil, "system.trace.commands did not add the scratch watcher")
    local read = assert(reader:get_read_data())
    for i, byte in ipairs(ring_entry) do
      assert(read[i] == byte, "the scratch watcher did not pass a write on")
    end
  end
  print(
    "Ilha system/SM regression PASS: " .. args[2] .. (command_trace and " (command trace)" or "")
  )
end, debug.traceback)

-- Construction and capability assertions must leave no test resources behind.
util.runfile = runfile
if not ok then error(err, 0) end
