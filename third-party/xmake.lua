-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("softfloat", function()
  add_rules("build.no-coverage")
  set_kind("static")
  on_load(function(target)
    -- The pinned upstream source lists select the FAST_INT64 implementation.
    local recipe =
      io.readfile(path.join(target:scriptdir(), "softfloat/build/Linux-x86_64-GCC/Makefile"))
    for _, group in ipairs({ "PRIMITIVES", "SPECIALIZE", "OTHERS" }) do
      local sources = assert(recipe:match("OBJS_" .. group .. " = (.-)\n\n"))
      local directory = group == "SPECIALIZE" and "softfloat/source/RISCV/" or "softfloat/source/"
      for name in sources:gmatch("([%w_]+)%$%(OBJ%)") do
        target:add("files", path.join(target:scriptdir(), directory .. name .. ".c"))
      end
    end
  end)
  add_includedirs("softfloat/source/include", { public = true })
  add_includedirs("softfloat/source/RISCV", "softfloat/build/Linux-x86_64-GCC")
  add_defines(
    "SOFTFLOAT_ROUND_ODD",
    "INLINE_LEVEL=5",
    "SOFTFLOAT_FAST_DIV32TO16",
    "SOFTFLOAT_FAST_DIV64TO32",
    "SOFTFLOAT_FAST_INT64",
    "THREAD_LOCAL=__thread",
    { public = true }
  )
  set_optimize("faster")
end)

target("catch2", function()
  add_rules("build.no-coverage")
  set_kind("static")
  add_files("Catch2/src/catch2/**.cpp")
  remove_files("Catch2/src/catch2/internal/catch_main.cpp")
  add_sysincludedirs("Catch2/src", { public = true })
  on_load(function(target)
    local directory = target:autogendir()
    local header = path.join(directory, "catch2/catch_user_config.hpp")
    -- Use Catch2's own defaults; no project-specific Catch configuration.
    if not os.isfile(header) then
      os.mkdir(path.directory(header))
      io.writefile(header, "// Catch2 default configuration.\n")
    end
    target:add("sysincludedirs", directory, { public = true })
  end)
end)

-- Only discover the environment's LuaJIT; vendored sources below never download.
add_requires("pkgconfig::luajit", { alias = "luajit", system = true })
add_requires("pkgconfig::sqlite3", { alias = "sqlite3", system = true })

target("dramsys", function()
  add_rules("build.no-coverage")
  set_kind("static")
  add_files("DRAMSys/src/DRAMSys/**.cpp")
  add_sysincludedirs("DRAMSys/src", "DRAMUtils/include", "nlohmann_json/include", { public = true })
  add_deps("systemc", "fmt", "dramsys.resources", { public = true })
  add_packages("sqlite3")
  add_defines('DRAMSYS_VERSION="5.6.0"', 'DRAMSYS_YEAR="2026"')
  -- DRAMSys uses fmt's header-only implementation in its upstream recipe.
  add_defines("FMT_HEADER_ONLY=1")
end)

target("dramsys.resources", function()
  add_rules("build.no-coverage")
  set_kind("phony")
  add_rules("lv.runfiles", {
    files = { ["third-party/DRAMSys/configs"] = "DRAMSys/configs" },
  })
end)

target("glm", function()
  add_rules("build.no-coverage")
  set_kind("headeronly")
  add_sysincludedirs("glm", { public = true })
end)

target("systemc", function()
  add_rules("build.no-coverage")
  set_kind("static")
  add_files("systemc/src/**.cpp")
  remove_files("systemc/src/sysc/datatypes/int/sc_int32_mask.cpp")
  add_files("systemc/src/sysc/packages/qt/qt.c")
  add_sysincludedirs("systemc/src", { public = true })
  add_defines("SC_BUILD", "SC_INCLUDE_FX", "SC_ENABLE_ASSERTIONS")
  add_syslinks("pthread", { public = true })
  if is_arch("x86_64") then
    add_files("systemc/src/sysc/packages/qt/md/iX86_64.s")
  elseif is_arch("arm64", "aarch64") then
    add_files("systemc/src/sysc/packages/qt/md/aarch64.s")
  end
  on_load(
    function(target)
      assert(
        target:is_arch("x86_64", "arm64", "aarch64"),
        "SystemC QuickThreads requires x86_64 or aarch64"
      )
    end
  )
  -- Upstream uses preprocessed assembly despite the lowercase .s extension.
  on_config(function(target)
    local cc, toolname = target:tool("cc")
    target:set("toolset", "as", toolname .. "@" .. cc)
  end)
  add_asflags("-xassembler-with-cpp", { force = true })
  if is_plat("macosx") then
    add_ldflags(
      "-Wl,-U,___sanitizer_start_switch_fiber",
      "-Wl,-U,___sanitizer_finish_switch_fiber",
      { public = true }
    )
  end
end)

target("fmt", function()
  add_rules("build.no-coverage")
  set_kind("static")
  add_files("fmt/src/format.cc", "fmt/src/os.cc")
  add_sysincludedirs("fmt/include", { public = true })
end)

target("perfetto", function()
  add_rules("build.no-coverage")
  set_kind("static")
  if is_plat("macosx") then
    -- Nix uses GCC on Darwin too. Keep Apple's signpost intrinsic Clang-only,
    -- as in the existing CMake build, without modifying the pinned SDK source.
    on_load(function(target)
      local source = path.join(target:scriptdir(), "perfetto/sdk/perfetto.cc")
      local generated = path.join(target:autogendir(), "perfetto.cc")
      local contents = io.readfile(source)
      contents = contents
        :replace(
          "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC)\n#include <os/signpost.h>",
          "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC) && defined(__clang__)\n#include <os/signpost.h>",
          { plain = true }
        )
        :replace(
          "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC)\n    // Emit a MacOS point-of-interest signpost",
          "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC) && defined(__clang__)\n    // Emit a MacOS point-of-interest signpost",
          { plain = true }
        )
      if not os.isfile(generated) or io.readfile(generated) ~= contents then
        os.mkdir(path.directory(generated))
        io.writefile(generated, contents)
      end
      target:add("files", generated)
    end)
  else
    add_files("perfetto/sdk/perfetto.cc")
  end
  add_sysincludedirs("perfetto/sdk", { public = true })
  add_syslinks("pthread", { public = true })
  if is_plat("linux") then add_syslinks("dl", { public = true }) end
end)

target("quill", function()
  add_rules("build.no-coverage")
  set_kind("headeronly")
  add_sysincludedirs("quill/include", { public = true })
  add_syslinks("pthread", { public = true })
end)

target("tomlplusplus", function()
  add_rules("build.no-coverage")
  set_kind("headeronly")
  add_sysincludedirs("tomlplusplus/include", { public = true })
end)

target("sol2", function()
  add_rules("build.no-coverage")
  set_kind("headeronly")
  add_sysincludedirs("sol2/include", { public = true })
  add_packages("luajit", { public = true })
end)

target("argparse", function()
  add_rules("build.no-coverage")
  set_kind("headeronly")
  add_sysincludedirs("argparse/include", { public = true })
end)

target("elfio", function()
  add_rules("build.no-coverage")
  set_kind("headeronly")
  add_sysincludedirs("elfio", { public = true })
end)
