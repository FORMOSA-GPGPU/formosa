-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@param output_file string
---@param source_dir string
---@param module_name? string
function main(output_file, source_dir, module_name)
  assert(output_file and source_dir, "usage: generate_linker.lua <output-file> <source-dir>")
  ---@diagnostic disable-next-line: undefined-global -- provided by xmake lua.
  local address_map_module = import("ilha.lua.addr_map", { rootdir = source_dir })
  local address_map = address_map_module.xmake_address_map()
  if module_name then
    ---@diagnostic disable-next-line: undefined-global -- provided by xmake lua.
    local overrides = import(module_name, { rootdir = source_dir }).xmake_address_map()
    for key, value in pairs(overrides) do
      address_map[key] = value
    end
  end
  local function get(key)
    local value = address_map[key]
    assert(type(value) == "number", "missing numeric Ilha address-map field: " .. key)
    return value
  end

  ---@diagnostic disable-next-line: undefined-field -- xmake extends the standard os library.
  os.mkdir(output_file:match("^(.*)/[^/]+$") or ".")
  local file = assert(io.open(output_file, "w"))
  file:write(string.format(
    [[
/* Generated from ilha/lua/addr_map.lua. Do not edit. */
MEMORY
{
  ROM (RX): ORIGIN = 0x%X, LENGTH = 0x%X
  TCM (RWX): ORIGIN = 0x%X, LENGTH = 0x%X
}
]],
    get("cp_rom_base"),
    get("cp_rom_size"),
    get("cp_tcm_base"),
    get("cp_tcm_size")
  ))
  file:close()
end
