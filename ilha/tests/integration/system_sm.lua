-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local args = { ... }
local util = require("lv.util")
local runfile = util.runfile
local socket_path = os.tmpname()
os.remove(socket_path)

local ok, err = xpcall(function()
  -- Substitute only the test ROM image. Config, constructor, map, SMs and
  -- replay/public capability path below are the existing Ilha implementations.
  util.runfile = function(path)
    if path == "bin/fwrom.elf" then return args[1] end
    return runfile(path)
  end
  local config = require("ilha.config"):override({ sm = { module = args[2] } })
  local system = require("ilha.system")("ilha", config, {
    replay = true,
    replay_host_mem_size = 4096,
    agent_socket_path = socket_path,
  })
  util.runfile = runfile
  local addr = require("ilha.addr_map")
  for _, offset in ipairs({ addr.system_info_off_num_sm, addr.system_info_off_threads_per_warp }) do
    system._replay_initiator:add_payload({ addr = addr.system_info_base + offset, size = 8 })
  end
  system:start(100)
  local function u64(bytes)
    local value = 0
    for i = 8, 1, -1 do
      value = value * 256 + bytes[i]
    end
    return value
  end
  assert(u64(assert(system._replay_initiator:get_read_data())) == config.system.num_sm)
  assert(u64(assert(system._replay_initiator:get_read_data())) == config.system.threads_per_warp)
  print("Ilha system/SM regression PASS: " .. args[2])
end, debug.traceback)

-- Construction and replay assertions must leave no test resources behind.
util.runfile = runfile
os.remove(socket_path)
if not ok then error(err, 0) end
