-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Shared host/firmware protocol headers; no host library or firmware dependency.
target("hal.protocol", function()
  set_kind("headeronly")
  add_includedirs("include", "src/hal", { public = true })
  add_deps("lv.device", { public = true })
end)

target("falloc", function()
  set_kind("static")
  add_files("src/common/falloc/falloc.c")
  add_includedirs("src/common/falloc", { public = true })
  add_includedirs("include")
end)

target("formosa-real.lv", function()
  set_kind("static")
  add_files("src/real/lv/lv-driver.cc", "src/real/lv/lv-config.cc")
  add_includedirs("src")
  add_deps("libcomm", "falloc", "ilha-platform-data")
  add_cxxflags("-Wall", "-Wextra", "-Wno-unused-parameter", "-g")
end)

target("gpufw-embedded", function()
  set_kind("object")
  add_deps("gpufw.elf", { inherit = false })
  set_policy("build.fence", true)
  on_load(function(target)
    local directory = target:autogendir()
    local source = path.join(directory, "src/gpufw.elf.c")
    if not os.isfile(source) then
      os.mkdir(path.directory(source))
      io.writefile(source, "/* generated firmware bytes */\n")
    end
    target:add("files", source)
    target:add("includedirs", directory, { public = true })
  end)
  before_build(function(target)
    import("core.project.depend")
    local elf = target:dep("gpufw.elf"):targetfile()
    local directory = target:autogendir()
    local binary = path.join(directory, "gpufw.bin")
    local source = path.join(directory, "src/gpufw.elf.c")
    local header = path.join(directory, "gpufw.elf.h")
    local dependfile = path.join(directory, "gpufw.elf.c.d")
    os.mkdir(directory)
    depend.on_changed(function()
      os.mkdir(directory)
      os.vrunv("riscv64-none-elf-objcopy", { "-O", "binary", elf, binary })
      os.vrunv("xxd", { "-i", "-n", "gpufw_elf", binary }, { stdout = source })
      io.writefile(
        header,
        "#pragma once\nextern unsigned char gpufw_elf[];\nextern unsigned int gpufw_elf_len;\n"
      )
    end, {
      files = { elf },
      dependfile = dependfile,
      lastmtime = os.mtime(source),
    })
  end)
end)

target("formosa-hal.lv", function()
  set_kind("static")
  add_files("src/hal/hal.cc")
  add_includedirs("include", { public = true })
  add_includedirs("src")
  add_headerfiles("include/(formosa-hal/**.h)")
  add_deps("formosa-real.lv", "falloc", "ilha-platform-data")
  add_deps("gpufw-embedded", { inherit = false })
  after_load(
    function(target) target:add("includedirs", target:dep("gpufw-embedded"):autogendir()) end
  )
  add_cxxflags("-Wall", "-Wextra", "-Wno-unused-parameter", "-g")
end)

includes("tests/xmake.lua")

target("formosa-sdk", function()
  set_kind("phony")
  add_rules("workflow.install")
  add_deps("formosa-hal.lv", "formosa-real.lv", "falloc", "libcomm")
  on_install(function(target)
    import("target.action.install", { alias = "install_target" })
    for _, name in ipairs({ "formosa-hal.lv", "formosa-real.lv", "falloc", "libcomm" }) do
      install_target(target:dep(name), {
        installdir = target:installdir(),
        bindir = "bin",
        libdir = "lib",
        includedir = "include",
      })
    end
    local directory = path.join(target:installdir(), "lib/cmake/Formosa")
    os.mkdir(directory)
    for _, name in ipairs({ "FormosaConfig.cmake", "FormosaConfigVersion.cmake" }) do
      os.cp(path.join(target:scriptdir(), "cmake", name), directory)
    end
    local metadata =
      io.readfile(path.join(target:scriptdir(), "cmake/FormosaTargets.install.cmake.in"))
    local coverage = is_mode("coverage") and '  INTERFACE_LINK_OPTIONS "--coverage"\n' or ""
    for _, name in ipairs({ "FALLOC", "REAL", "HAL" }) do
      metadata =
        metadata:replace("@FORMOSA_" .. name .. "_COVERAGE@\n", name == "HAL" and coverage or "", {
          plain = true,
        })
    end
    -- C consumers and consumers using a different C++ standard library need
    -- the producer's runtime, including non-system library search paths.
    local runtime = ""
    local hal = target:dep("formosa-hal.lv")
    local compiler = hal:compiler("cxx")
    if hal:has_tool("cxx", "gcc", "gxx") then
      local library = hal:is_plat("macosx") and "libstdc++.dylib" or "libstdc++.so"
      runtime = os.iorunv(compiler:program(), { "-print-file-name=" .. library }, {
        envs = compiler:runenvs(),
      }):trim()
      if not os.isfile(runtime) then runtime = "stdc++" end
    elseif hal:has_tool("cxx", "clang", "clangxx") then
      -- Clang's -print-file-name does not search its linker -L directories.
      -- A dry run exposes the actual stdlib selection without building a probe.
      local _, commands = os.iorunv(
        compiler:program(),
        table.join(compiler:compflags(), { "-###", "-x", "c++", os.nuldev(), "-o", os.nuldev() }),
        { envs = compiler:runenvs() }
      )
      local arguments = os.argv(commands)
      for _, argument in ipairs(arguments) do
        if argument == "-lc++" or argument == "-lstdc++" then runtime = argument:sub(3) end
      end
      local name = runtime
      for index, argument in ipairs(arguments) do
        local directory = argument == "-L" and arguments[index + 1] or argument:match("^%-L(.+)$")
        if directory and name ~= "" then
          local suffixes = hal:is_plat("macosx") and { ".tbd", ".dylib", ".a" } or { ".so", ".a" }
          for _, suffix in ipairs(suffixes) do
            local library = path.join(directory, "lib" .. name .. suffix)
            if os.isfile(library) then
              runtime = library
              break
            end
          end
          if path.is_absolute(runtime) then break end
        end
      end
    end
    metadata = metadata:replace("@FORMOSA_CXX_RUNTIME@", runtime, { plain = true })
    io.writefile(path.join(directory, "FormosaTargets.cmake"), metadata)
    print("Installed %s -> %s", target:name(), path.absolute(target:installdir()))
  end)
end)
