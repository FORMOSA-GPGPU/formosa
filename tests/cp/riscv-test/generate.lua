-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- CLI entry retained for CMake and other build tools.
local parser =
  require("argparse")("generate.lua", "Prepare a CPU RISC-V test workload for any build tool.")
parser:option("--core", "CPU test adapter name"):count(1)
parser:option("--riscv-tests", "Upstream riscv-tests source directory"):count(1)
parser:option("--output", "Existing output directory"):count(1)
local args = parser:parse()
require("cp.cores")
local plan = require("cp.riscv-test.prepare")(
  require(args.core).isa,
  require("cp.platform"),
  args.riscv_tests,
  arg[0]:match("^(.*)/") or "."
)
for name, contents in pairs(plan.files) do
  local path = args.output .. "/" .. name
  local existing = io.open(path, "r")
  local previous = existing and existing:read("*a")
  if existing then existing:close() end
  if previous ~= contents then
    local file = assert(io.open(path, "w"))
    assert(file:write(contents))
    assert(file:close())
  end
end
print(string.format('{"workload":"%s","isa":"%s"}', plan.workload, plan.isa))
