-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

set_project("formosa")
set_xmakever("3.0.4")
set_languages("c++17")
add_rules("mode.debug", "mode.release", "mode.coverage")
set_defaultmode("debug")
set_default(false)
set_targetdir("$(builddir)", { bindir = "bin", libdir = "lib" })
-- Shared outputs must also share dependency records when switching modes.
set_policy("build.intermediate_directory", false)
-- Development builds retain symbols/assertions at the established -O2 level.
if is_mode("debug", "coverage") then set_optimize("faster") end

if (get_config("plat") or os.host()) == "macosx" and os.getenv("IN_NIX_SHELL") then
  -- The flake uses GCC with its own SDK. Xcode's host SDK is incompatible.
  -- A native config default still allows an explicit --toolchain override.
  set_config("toolchain", "gcc")
end

add_moduledirs(path.join(os.projectdir(), "xmake/modules"))
option("workflow", function()
  set_default("")
  set_showmenu(true)
  set_description("Development workflow from workflows.lua")
end)
option("test-suites", function()
  set_default("")
  set_showmenu(true)
  set_description("Comma-separated workflow test suites; alternative to --workflow")
end)

includes("xmake/tasks/*/xmake.lua")
includes("xmake/rules/*/xmake.lua")
add_rules("workflow.test_suites")

includes("third-party/xmake.lua", "libcomm/xmake.lua", "lv/device/xmake.lua", "lv/xmake.lua")
includes("ilha/xmake.lua", "fw/xmake.lua", "hal/xmake.lua")
includes("simtix/xmake.lua", "tests/xmake.lua")
