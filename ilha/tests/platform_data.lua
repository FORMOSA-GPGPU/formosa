-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local header_path, linker_path = assert(arg[1]), assert(arg[2])

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

print("Ilha generator backends emitted the supplied address-map fixture")
