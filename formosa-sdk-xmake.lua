-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

set_project("formosa-sdk")
set_xmakever("3.0.4")
set_languages("c++17")
add_rules("mode.debug", "mode.release", "mode.coverage")
set_defaultmode("release")
set_default(false)
set_targetdir("$(builddir)", { bindir = "bin", libdir = "lib" })
-- Shared outputs must also share dependency records when switching modes.
set_policy("build.intermediate_directory", false)
-- Development builds retain symbols/assertions at the established -O2 level.
if is_mode("debug", "coverage") then set_optimize("faster") end

if (get_config("plat") or os.host()) == "macosx" and os.getenv("IN_NIX_SHELL") then
  set_config("toolchain", "gcc")
end

add_moduledirs(path.join(os.projectdir(), "xmake/modules"))
option("workflow", function() set_default("") end)
option("test-suites", function() set_default("") end)
includes("xmake/rules/*/xmake.lua")
add_rules("workflow.test_suites")
includes(
  "libcomm/xmake.lua",
  "lv/device/xmake.lua",
  "ilha/platform-data/xmake.lua",
  "fw/xmake.lua",
  "hal/xmake.lua"
)
