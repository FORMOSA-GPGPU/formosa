-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

includes("platform-data/xmake.lua")

target("libilha", function()
  set_kind("headeronly")
  add_includedirs("include", { public = true })
  add_deps("systemc", { public = true })
end)

-- Preprocesses the Host ABI header for the LuaJIT FFI into a C++ string
-- literal, which ilha.command_packet_abi carries as its cdef. Stub standard
-- headers keep the system headers out, since ffi.cdef cannot parse them and
-- LuaJIT predefines the fixed-width types; the static assertions go too.
target("ilha.command-packet-cdef", function()
  set_kind("phony")
  set_policy("build.fence", true)
  on_load(
    function(target)
      target:add("includedirs", path.join(target:autogendir(), "cdef"), { public = true })
    end
  )
  on_build(function(target)
    import("core.project.depend")
    local dir = path.join(target:autogendir(), "cdef")
    local output = path.join(dir, "command_packet_cdef.inc")
    local hal = path.join(os.projectdir(), "hal")
    depend.on_changed(function()
      local stub = path.join(dir, "stub")
      os.mkdir(stub)
      io.writefile(path.join(stub, "stddef.h"), "")
      io.writefile(
        path.join(stub, "stdint.h"),
        table.concat({
          "#define UINT16_C(c) c",
          "#define UINT32_C(c) c",
          "#define UINT64_C(c) c",
          "#define UINT32_MAX 0xffffffffu",
          "",
        }, "\n")
      )
      local source = path.join(dir, "command_packet.c")
      io.writefile(source, '#include "command_packet.h"\n')
      local compiler = target:compiler("cxx")
      local cdef = os.iorunv(compiler:program(), {
        "-E",
        "-P",
        "-x",
        "c",
        "-nostdinc",
        "-D_Static_assert(...)=",
        "-I" .. stub,
        "-I" .. path.join(hal, "src/hal"),
        "-I" .. path.join(hal, "include"),
        source,
      }, { envs = compiler:runenvs() })
      io.writefile(output, 'R"cdef(' .. cdef .. ')cdef"\n')
    end, {
      files = {
        path.join(hal, "src/hal/command_packet.h"),
        path.join(hal, "include/formosa-hal/api.h"),
        path.join(os.scriptdir(), "xmake.lua"),
      },
      dependfile = output .. ".d",
      lastmtime = os.mtime(output),
      changed = target:is_rebuilt(),
    })
  end)
end)

target("ilha.bindings", function()
  set_kind("object")
  add_files("src/*.cc")
  add_deps("libilha", "liblv", "hal.protocol")
  add_deps("ilha.command-packet-cdef")
end)

target("ilha.lua", function()
  set_kind("phony")
  add_rules("lv.lua", { namespaces = { ilha = "lua" } })
end)

target("ilha.runfiles", function()
  set_kind("phony")
  local files = {}
  for _, source in ipairs(os.files("configs/**")) do
    files[source] = source
  end
  for _, source in ipairs({
    "addressmapping/am_lpddr4_1Gbx16_robaco.json",
    "mcconfig/fr_fcfs.json",
    "memspec/JEDEC_1Gbx16_LPDDR4-1600.json",
    "memspec/JEDEC_1Gbx16_LPDDR4-3200.json",
  }) do
    files["configs/dram/" .. path.filename(source)] = "../third-party/DRAMSys/configs/" .. source
  end
  add_rules("lv.runfiles", { files = files, copy = true })
end)

target("lv", function() add_deps("ilha.bindings", "ilha.lua", "ilha.runfiles") end)
target("ilha-daemon", function()
  add_rules("lv.script", { script = "daemon.lua" })
  add_deps("fw.runfiles", { inherit = false })
end)

target("ilha-qemu", function()
  add_rules("lv.script", { script = "qemu.lua" })
  add_deps("fw.runfiles", { inherit = false })
end)

includes("tests/xmake.lua")
