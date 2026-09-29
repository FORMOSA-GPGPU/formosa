-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local function read_manifest(path)
  local file = assert(io.open(path, "r"))
  local contents = file:read("*a")
  file:close()
  local expected = {}
  for name in contents:gmatch("[^\r\n]+") do
    assert(name:match("^[%w_]+$"), "invalid RISC-V test name: " .. name)
    assert(not expected[name], "duplicate RISC-V test manifest entry: " .. name)
    expected[name] = true
  end
  assert(next(expected), "empty RISC-V test manifest")
  return expected
end

local function validate(text, expected)
  assert(not text:find("ERR", 1, true), "RISC-V test reported ERR")
  assert(not text:find("Interrupted", 1, true), "unexpected trap during RISC-V tests")
  local seen, count, done = {}, 0, false
  for line in text:gmatch("([^\n]*)\n") do
    local name = line:match("^(.-)%.%.OK$")
    if name then
      assert(expected[name], "unexpected test result: " .. name)
      assert(not seen[name], "duplicate test result: " .. name)
      seen[name] = true
      count = count + 1
    elseif line == "RISC-V test done" then
      assert(not done, "duplicate RISC-V test completion marker")
      done = true
    end
  end
  if done then
    for name in pairs(expected) do
      assert(seen[name], "missing test result: " .. name)
    end
    return count
  end
end

-- The returned checker owns this program's output protocol. nil means pending;
-- a passing result completes the run; an assertion fails it.
local function checker(manifest_path)
  local expected = read_manifest(manifest_path)
  return function(system)
    local text = system:output():gsub("\27%[[0-9;]*m", ""):gsub("\r\n", "\n")
    return validate(text, expected)
  end
end

return { checker = checker }
