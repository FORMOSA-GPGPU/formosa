-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

trace.settings.streaming = true
trace.settings.file_write_period_ms = 100
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.NS)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = "daemon"

---@type ilha.system
local System = require("ilha.system")
local argparse = require("argparse")
local ConfigCli = require("ilha.config_cli")

local parser = argparse("daemon.lua")
parser:option("-l --limit", "Simulation limit cycles (cycle)"):convert(tonumber)
parser:option("-s --stats", "Output path of the stat"):args(1)
ConfigCli.add_options(parser, true)
parser:option("--drain", "Extra drain cycles after the main run"):convert(tonumber):default(5000)
parser:flag("--keep-alive", "Ignore client Terminate and keep the simulation running")

local args = parser:parse({ ... })
local config = ConfigCli.load(args)

local system = System("System", config, {
  agent_socket_path = "/tmp/formosa.sock",
  keep_alive = args.keep_alive,
})

local function exit_hook()
  if args.stats and system.stats and system.stats.dump_toml then
    local stat_file = assert(io.open(args.stats, "w"), "Cannot open " .. args.stats)
    stat_file:write(system.stats:dump_toml())
    stat_file:close()
  end
  lv.info("===----- Simulation stops -----===")
end

lv.exit_hook(exit_hook)
if not args.limit then
  sc.start()
  sc.stop()
else
  system:start(args.limit)
  if args.drain > 0 then system:start(args.drain) end
  sc.stop()
end
exit_hook()
