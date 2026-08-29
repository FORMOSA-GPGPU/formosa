-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@type formosa.system.config
local config = require("formosa.config")
---@class formosa.system.config
---@field pipelined_core_config? table

---@type formosa.system
local System = require("formosa.system")

local stdlib = require("posix.stdlib")
local S = require("posix.signal")
local U = require("posix.unistd")
local W = require("posix.sys.wait")

local function make_agent_socket_path()
  local tmpdir = os.getenv("TMPDIR") or "/tmp"
  local fd, path = stdlib.mkstemp(tmpdir:gsub("/+$", "") .. "/formosa-XXXXXX")
  U.close(fd)
  os.remove(path)
  return path
end

local agent_socket_path = make_agent_socket_path()

local argparse = require("argparse")
local sm_kinds = require("formosa.sm_kinds")
local parser = argparse("run_opencl.lua")
parser:option("-s --stats", "Output path of the stat"):args(1)
parser
  :option("--ghost", "Override the GhOST scheduler configuration")
  :args(1)
  :choices({ "off", "on" })
parser
  :option("--sm", "Stream multiprocessor to simulate")
  :args(1)
  :choices(sm_kinds.available())
  :default("simtix.pipelined_sm")
parser:option("-t --trace", "Output prefix of the Perfetto trace"):args(1):default("run_opencl")
parser
  :option(
    "--heartbeat-frequency",
    "Heartbeat interval in retired warp instructions. Set to 0 to disable."
  )
  :args(1)
  :convert(tonumber)
  :default(0)
parser:option("--replay-capture", "Output directory for replay capture"):args(1)
parser:argument("host_program", "Host program to run with the simulator"):args("1")
parser:argument("host_program_args", "Arguments for the host program"):args("*")

local args = parser:parse({ ... })

config.pipelined_core_config = config.pipelined_core_config or {}
config.pipelined_core_config.heartbeat_frequency = args.heartbeat_frequency
if args.ghost then config.pipelined_core_config.enable_ghost_scheduler = args.ghost == "on" end

local make_sm = require(args.sm)

trace.settings.streaming = true
trace.settings.file_write_period_ms = 100
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.NS)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = args.trace

local host_program_with_args =
  table.concat({ args.host_program, unpack(args.host_program_args) }, " ")

local function wait_child_nonblocking(pid, timeout_sec)
  local waited = 0
  while waited < timeout_sec do
    local wpid, how, status = W.wait(pid, W.WNOHANG)
    if wpid == pid then return true, how, status end
    if wpid == nil and how == "No child processes" then return true, "exited", status end
    U.sleep(1)
    waited = waited + 1
  end
  return false, "timeout", nil
end

-- Pipe used to detect parent death: parent holds w, child/watchdog holds r.
local r, w = U.pipe()

local childpid = U.fork()

if childpid == 0 then
  -- =========================
  -- Child (main simulation)
  -- =========================

  -- Child doesn't need the write end
  U.close(w)

  local mainpid = U.getpid()

  -- Fork watchdog: blocks on reading r; if parent dies, w closes => EOF => kill main
  local wpid = U.fork()
  if wpid == 0 then
    -- =========================
    -- Watchdog process
    -- =========================
    -- Block until EOF. We never expect actual data; only care about parent closing w.
    while true do
      local data = U.read(r, 1) -- blocking
      if not data or #data == 0 then
        -- Parent is dead (or closed w normally). Ask main child to exit gracefully.
        S.kill(mainpid, S.SIGINT)
        os.exit(0)
      end
      -- If someone accidentally writes data, just keep reading.
    end
  end

  local system = System("System", agent_socket_path, config, make_sm)

  sc.start()
  sc.stop()

  if args.stats and system.stats and system.stats.dump_toml then
    local ok, err = pcall(function()
      local stat_filename = args.stats
      local stat_file = assert(io.open(stat_filename, "w"), "Cannot open " .. stat_filename)
      lv.info("Writing stats to " .. stat_filename)
      stat_file:write(system.stats:dump_toml())
      stat_file:close()
    end)

    if not ok then lv.warning("Warning: " .. tostring(err)) end
  end

  -- If serve() returns naturally, clean up watchdog so it doesn't linger
  -- (Parent should have closed w anyway, but this makes it tighter.)
  S.kill(wpid, S.SIGTERM)
  W.wait(wpid)

  os.exit(0)
else
  -- =========================
  -- Parent (run OpenCL test)
  -- =========================

  -- Parent doesn't need the read end
  U.close(r)

  stdlib.setenv("AGENT_SOCKET_PATH", agent_socket_path)
  if args.replay_capture then stdlib.setenv("FORMOSA_HAL_CAPTURE_TRACE", args.replay_capture) end
  config:export()

  print("Running OpenCL program: " .. host_program_with_args)

  local exit_code = os.execute(host_program_with_args)
  print("OpenCL program exited with status: " .. exit_code)
  local child_exited = false

  child_exited = wait_child_nonblocking(childpid, 2)

  if not child_exited then
    S.kill(childpid, S.SIGINT)
    U.close(w)
    child_exited = wait_child_nonblocking(childpid, 2)
  end

  if not child_exited then
    S.kill(childpid, S.SIGTERM)
    child_exited = wait_child_nonblocking(childpid, 2)
  end

  if not child_exited then S.kill(childpid, S.SIGKILL) end

  W.wait(childpid, W.WNOHANG)

  -- Decide exit code
  if exit_code ~= 0 then os.exit(-1) end
end
