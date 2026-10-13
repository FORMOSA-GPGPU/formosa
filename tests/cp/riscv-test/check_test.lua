-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local riscv_test = require("cp.riscv-test.check")

local function checker(manifest, expected_error)
  local path = os.tmpname()
  local file = assert(io.open(path, "w"))
  file:write(manifest)
  file:close()
  local ok, result = pcall(riscv_test.checker, path)
  os.remove(path)
  if expected_error then
    assert(not ok, "invalid manifest unexpectedly accepted")
    assert(tostring(result):find(expected_error, 1, true), tostring(result))
  else
    assert(ok, result)
    return result
  end
end

local function check(text, expected_error, expected_result)
  local validate = checker("add\nsub\n")
  local ok, result = pcall(validate, { output = function() return text end })
  if expected_error then
    assert(not ok, "invalid RISC-V test output unexpectedly passed")
    assert(tostring(result):find(expected_error, 1, true), tostring(result))
  else
    assert(ok, result)
    assert(result == expected_result, "wrong RISC-V test result")
  end
end

local results = "add..OK\nsub..OK\n"
local done = "RISC-V test done\n"
check("add..")
check("add..\27[32mOK\27[0m\r\nsub..OK\r\n" .. done, nil, 2)
check("add..ERR\n" .. done, "reported ERR")
check(results .. "Interrupted\n" .. done, "unexpected trap")
check("add..OK\n" .. done, "missing test result: sub")
check(results .. "mul..OK\n" .. done, "unexpected test result: mul")
check(results .. "bad-name..OK\n" .. done, "unexpected test result: bad-name")
check(results .. "add..OK\n" .. done, "duplicate test result: add")
check(results .. done .. done, "duplicate RISC-V test completion marker")
checker("", "empty RISC-V test manifest")
checker("add\nadd\n", "duplicate RISC-V test manifest entry: add")

-- Complete OK lines remain pending until a complete completion marker arrives.
check(results)
check(results .. "RISC-V test done")
check("")
print("cp.riscv-test.check: manifest and output validation checks passed")
