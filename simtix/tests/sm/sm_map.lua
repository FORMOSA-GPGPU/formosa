-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Relocate every control bank so an SM that ignores the caller's map cannot
-- pass by relying on the matching offsets of the default platform.
local model = ({ ... })[1]
local config = require("ilha.config").system
local map = {}
for key, value in pairs(require("ilha.addr_map")) do
  map[key] = value
end
for _, bank in ipairs({ "wgi", "icache", "dcache", "core", "stack_remap" }) do
  map[bank .. "_csr_base"] = map[bank .. "_csr_base"] + 0x400
end
local params = { icache = {}, dcache = {}, core = {} }
local sm = require(model)("sm", { id = 0, config = config, params = params, address_map = map })
assert(next(params.icache) == nil and next(params.dcache) == nil and next(params.core) == nil)
local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local memory = simple.Memory("memory", { size = 0x40000 })
local host = simple.Initiator("host")
sm.clock, memory.clock, host.clock = clock, clock, clock
sm.target, host.target = memory.port, sm.port
for _, offset in ipairs({ 0, 8, 16 }) do
  host:add_payload({ addr = map.core_csr_base + offset, size = 8 })
end
sc.start(100 * period)
local function u64(bytes)
  local value = 0
  for i = 8, 1, -1 do
    value = value * 256 + bytes[i]
  end
  return value
end
for _, expected in ipairs({
  config:threads_per_core(),
  config.stack_remap_entries,
  config:effective_stack_remap_group_size(),
}) do
  local data = assert(host:get_read_data(), "SM ignored the supplied control map: no response")
  assert(u64(data) == expected, "SM ignored the supplied control map")
end
print("SM address map / parameter ownership PASS: " .. model)
