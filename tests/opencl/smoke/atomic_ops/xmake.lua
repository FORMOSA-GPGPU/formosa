-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("opencl.atomic-spirv", function()
  set_kind("phony")
  on_build(function(target)
    import("core.project.depend")
    import("lib.detect.find_tool")
    local clang = assert(find_tool("clang"), "clang is required")
    local spirv = assert(find_tool("llvm-spirv"), "llvm-spirv is required")
    local source = path.join(target:scriptdir(), "c11.cl")
    local bitcode = path.join(target:autogendir(), "c11.bc")
    local output = path.join(target:autogendir(), "c11.spv")
    depend.on_changed(function()
      os.mkdir(target:autogendir())
      os.vrunv(clang.program, {
        "--target=spir64",
        "-x",
        "cl",
        "-cl-std=CL2.0",
        "-O1",
        "-emit-llvm",
        "-c",
        source,
        "-o",
        bitcode,
      })
      os.vrunv(spirv.program, { bitcode, "-o", output })
    end, {
      files = { source, clang.program, spirv.program, path.join(target:scriptdir(), "xmake.lua") },
      dependfile = target:dependfile(output),
      lastmtime = os.mtime(output),
      changed = target:is_rebuilt(),
    })
  end)
end)

target("opencl-smoke-atomic_ops", function()
  add_deps("opencl.atomic-spirv")
  add_defines('C11_KERNEL_PATH="' .. path.join(os.scriptdir(), "c11.cl") .. '"')
end)
