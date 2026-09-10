-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

trace.settings.streaming = true
trace.settings.file_write_period_ms = 100
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.NS)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = "daemon"

---@type formosa.system
local System = require("formosa.system")
local argparse = require("argparse")
local config = require("formosa.config")
local U = require("posix.unistd")

local sm_kinds = require("formosa.sm_kinds")

local parser = argparse("daemon.lua")
parser:option("-l --limit", "Simulation limit cycles (cycle)"):convert(tonumber)
parser:option("-s --stats", "Output path of the stat"):args(1)
parser
  :option("--sm", "Stream multiprocessor to simulate")
  :args(1)
  :choices(sm_kinds.available())
  :default("simtix.pipelined_sm")
parser
  :option(
    "--heartbeat-frequency",
    "Heartbeat interval in retired warp instructions. Set to 0 to disable."
  )
  :args(1)
  :convert(tonumber)
  :default(0)
parser:option("--drain", "Extra drain cycles after the main run"):convert(tonumber):default(5000)
parser:flag("--keep-alive", "Ignore client Terminate and keep the simulation running")

local args = parser:parse({ ... })

local daemon_pid = U.getpid()
local config_link = os.getenv("LV_FORMOSA_CONFIG_FILE") or ".lv-formosa-config.sh"
local config_dir, config_name = config_link:match("^(.*)/([^/]+)$")
config_dir = config_dir or "."
config_name = config_name or config_link
local config_stem = config_name:match("^(.*)%.sh$") or config_name
local config_target = string.format("%s.%d.sh", config_stem, daemon_pid)
local config_file = config_dir .. "/" .. config_target
local config_link_tmp = config_link .. ".tmp." .. daemon_pid

config.pipelined_core_config = config.pipelined_core_config or {}
config.pipelined_core_config.heartbeat_frequency = args.heartbeat_frequency

local make_sm = require(args.sm)
local system = System("System", "/tmp/formosa.sock", config, make_sm, {
  keep_alive = args.keep_alive,
})

config:dump(config_file)
os.remove(config_link_tmp)
assert(U.link(config_target, config_link_tmp, true))
assert(os.rename(config_link_tmp, config_link))
lv.info(string.format("FORMOSA daemon PID %d published config: %s", daemon_pid, config_file))

local function remove_config()
  if U.readlink(config_link) == config_target then os.remove(config_link) end
  os.remove(config_file)
end

local function exit_hook()
  if args.stats and system.stats and system.stats.dump_toml then
    local stat_file = assert(io.open(args.stats, "w"), "Cannot open " .. args.stats)
    stat_file:write(system.stats:dump_toml())
    stat_file:close()
  end
  remove_config()
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
