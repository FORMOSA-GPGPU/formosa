-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@class ilha.config_cli.args
---@field config? string
---@field system_config? string
---@field sm? string
---@field sm_param? string
---@field heartbeat_frequency? number

local ConfigCli = {}

local function dirname(path)
  local directory = path:match("^(.*)/[^/]*$")
  return directory == "" and "." or (directory or ".")
end

local function resolve_config_path(path)
  if path:find("/") or path:match("%.lua$") then return path end
  return require("lv.util").runfile("configs/" .. path .. ".lua")
end

local function load_table(path)
  local value = assert(loadfile(path))()
  assert(type(value) == "table", "Ilha configuration override must return a table")
  return value
end

local function load_config_override(path)
  path = resolve_config_path(path)
  local overrides = load_table(path)
  if overrides.system and overrides.system.dram_config then
    local dram_config = overrides.system.dram_config
    if dram_config:sub(1, 1) ~= "/" then
      overrides.system.dram_config =
        assert(require("posix.stdlib").realpath(dirname(path) .. "/" .. dram_config))
    end
  end
  return overrides
end

---@param parser any
---@param heartbeat_compatibility? boolean
function ConfigCli.add_options(parser, heartbeat_compatibility)
  parser:option("--config", "Ilha Lua hardware configuration"):args(1)
  parser:option("--system-config", "Lua file returning Ilha system overrides"):args(1)
  parser
    :option("--sm", "Stream multiprocessor to simulate")
    :args(1)
    :choices(require("ilha.sm_kinds").available())
  parser:option("--sm-param", "Lua file returning the selected SM's parameter table"):args(1)
  if heartbeat_compatibility then
    parser
      :option(
        "--heartbeat-frequency",
        "Heartbeat interval in retired warp instructions. Set to 0 to disable."
      )
      :args(1)
      :convert(tonumber)
  end
end

---@param args ilha.config_cli.args
---@return ilha.config
function ConfigCli.load(args)
  local config = require("ilha.config")
  if args.config then config:override(load_config_override(args.config)) end
  if args.system_config then
    local system = load_table(args.system_config)
    if system.dram_config and system.dram_config:sub(1, 1) ~= "/" then
      system.dram_config = assert(
        require("posix.stdlib").realpath(dirname(args.system_config) .. "/" .. system.dram_config)
      )
    end
    config:override({ system = system })
  end
  if args.sm then config:override({ sm = { module = args.sm } }) end
  if args.sm_param then config:override({ sm = { param = load_table(args.sm_param) } }) end
  if args.heartbeat_frequency ~= nil then
    assert(config.sm.module == "simtix.pipelined_sm", "--heartbeat-frequency requires pipelined SM")
    config:override({
      sm = { param = { core = { heartbeat_frequency = args.heartbeat_frequency } } },
    })
  end
  config:validate()
  return config
end

return ConfigCli
