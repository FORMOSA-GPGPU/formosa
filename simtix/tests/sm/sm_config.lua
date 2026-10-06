-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local cases = {
  default = { params = { core = {} }, count = 2 },
  core = { params = { core = { num_subcores = 1 } }, count = 1 },
  four = { params = { core = { num_subcores = 4 } }, count = 4 },
}
local case = assert(cases[({ ... })[1]], "unknown SM configuration case")
local params = case.params
local original = params.core.num_subcores
local sm = require("simtix.pipelined_sm")("sm", {
  id = 0,
  config = require("ilha.config").system,
  address_map = require("ilha.addr_map"),
  params = params,
})

-- Check the constructed core and its routing, rather than the input table.
assert(#sm._core.subcores == case.count, "incorrect number of subcores")
assert(#sm._dmem_xbars == case.count, "subcore memory routing count differs")
assert(params.core.num_subcores == original, "SM modified caller-owned core parameters")
print("Pipelined SM configuration PASS")
