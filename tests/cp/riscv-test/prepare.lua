-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Build-tool-independent startup, linker, source and manifest generation.
local function prepare(isa, profile, riscv_tests, here)
  local id = isa
  local xlen =
    assert(isa:match("^rv(32)[a-z0-9_]+$") or isa:match("^rv(64)[a-z0-9_]+$"), "invalid ISA")
  local function read(path)
    local file = assert(io.open(path, "r"))
    local text = file:read("*a")
    file:close()
    return text
  end

  local files = {}
  local function write(suffix, text) files[id .. suffix] = text end

  local names, sources, calls, seen = {}, {}, {}, {}
  -- ISA-test selection belongs to this program, not the core/platform contract.
  local extensions = assert(isa:match("^rv%d+([a-z]+)"), "invalid ISA")
  assert(extensions:find("i", 1, true), "RISC-V ISA tests require the I extension")
  local groups = { "rv" .. xlen .. "ui" }
  if extensions:find("m", 1, true) then groups[#groups + 1] = "rv" .. xlen .. "um" end
  for _, group in ipairs(groups) do
    local root = riscv_tests .. "/isa/" .. group
    local makefrag = read(root .. "/Makefrag"):gsub("#[^\n]*", ""):gsub("\\\r?\n", " ")
    local list = assert(
      ("\n" .. makefrag):match("\n%s*" .. group .. "_sc_tests%s*=%s*([^\n]+)"),
      "missing scalar test list: " .. group
    )
    for name in list:gmatch("%S+") do
      assert(name:match("^[%w_]+$"), "unsupported Makefrag test name: " .. name)
      if name ~= "fence_i" and name ~= "ma_data" then
        assert(not seen[name], "duplicate test: " .. name)
        local source = root .. "/" .. name .. ".S"
        read(source)
        seen[name] = true
        names[#names + 1] = name
        sources[#sources + 1] = source
        calls[#calls + 1] = "    TEST(" .. name .. ")"
      end
    end
  end
  assert(#names > 0, "empty test workload")
  write(".sources", table.concat(sources, "\n") .. "\n")
  write(".tests", table.concat(names, "\n") .. "\n")
  write(
    ".start.S",
    (
      read(here .. "/start.S.in"):gsub(
        "@TEST_CALLS@",
        function() return table.concat(calls, "\n") end
      )
    )
  )
  local linker = read(here .. "/linker.ld.in")
  for _, field in ipairs({ "ram_base", "ram_size", "text_base", "stack_base", "serial_base" }) do
    local value = assert(profile[field], "missing platform field: " .. field)
    linker = linker:gsub("@IMAGE_" .. field:upper() .. "@", tostring(value))
  end
  write(".ld", linker)
  -- A small neutral description for build adapters; paths are relative to --output.
  return { workload = id, isa = isa, sources = sources, files = files }
end

if xmake then
  function main(...) return prepare(...) end
end

return prepare
