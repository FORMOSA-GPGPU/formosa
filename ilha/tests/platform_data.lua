-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

function main(header_path, linker_path)
  local function read(path)
    local file = assert(io.open(path, "r"))
    local contents = file:read("*a")
    file:close()
    return contents
  end

  local header = read(header_path)
  assert(header:match("#define%s+FSA_COMPLETION_SLOT_COUNT%s+13u"))
  assert(header:match("#define%s+FSA_CP_ROM_BASE%s+0x12345000ull"))
  assert(header:match("#define%s+FSA_CP_ROM_SIZE%s+0x2345ull"))
  assert(header:match("#define%s+FSA_SYSTEM_INFO_ABI_VERSION%s+77ull"))

  local linker = read(linker_path)
  assert(linker:match("ROM %(RX%): ORIGIN = 0x12345000, LENGTH = 0x2345"))
  assert(linker:match("TCM %(RWX%): ORIGIN = 0xABC000, LENGTH = 0x3456"))

  -- Native builds run both generators for multiple targets in one Lua process.
  -- Applying test overrides must not change the cached production address map.
  for _, kind in ipairs({ "header", "linker" }) do
    local generate = import("ilha.scripts.generate_" .. kind, { rootdir = os.projectdir() })
    local output = os.tmpfile()
    generate.main(output, os.projectdir())
    local expected = read(output)
    generate.main(output, os.projectdir(), "ilha.tests.data.addr_map_fixture")
    generate.main(output, os.projectdir())
    assert(read(output) == expected, kind .. " fixture modified the production address map")
    os.rm(output)
  end

  print("Ilha generator backends emitted the supplied address-map fixture")
end
