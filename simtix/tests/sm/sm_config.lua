-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local cases = {
  default = { params = { core = {} }, count = 2 },
  core = { params = { core = { num_subcores = 1 } }, count = 1 },
  four = { params = { core = { num_subcores = 4 } }, count = 4 },
  -- Write-back L1s of several SMs would lose each other's updates.
  writeback = {
    params = { core = {}, dcache = { write_hit_policy = "WriteBack" } },
    num_sm = 2,
    rejected = "multiple SMs require WriteThrough L1 D%-caches",
  },
}
local case = assert(cases[({ ... })[1]], "unknown SM configuration case")
local params = case.params
local original = params.core.num_subcores
local config = require("ilha.config")
if case.num_sm then config:override({ system = { num_sm = case.num_sm } }) end
local function build()
  return require("simtix.pipelined_sm")("sm", {
    id = 0,
    config = config.system,
    address_map = require("ilha.addr_map"),
    params = params,
  })
end

if case.rejected then
  local ok, message = pcall(build)
  assert(not ok, "SM accepted an unsupported configuration")
  assert(tostring(message):match(case.rejected), "unexpected error: " .. tostring(message))
  print("Pipelined SM configuration PASS")
  return
end

local sm = build()
-- Check the constructed core and its routing, rather than the input table.
assert(#sm._core.subcores == case.count, "incorrect number of subcores")
assert(#sm._dmem_xbars == case.count, "subcore memory routing count differs")
assert(params.core.num_subcores == original, "SM modified caller-owned core parameters")
print("Pipelined SM configuration PASS")
