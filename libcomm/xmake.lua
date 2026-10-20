-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("libcomm", function()
  set_kind("headeronly")
  add_includedirs("include", { public = true })
  add_headerfiles("include/(libcomm/**.h)")
end)

for _, name in ipairs({ "client", "server" }) do
  target("libcomm-" .. name, function()
    set_kind("binary")
    set_default(false)
    add_files("example/" .. name .. ".cc")
    add_deps("libcomm")
  end)
end

target("libcomm-unit-test", function()
  set_kind("binary")
  add_files("tests/unit/consumer.cc")
  add_deps("libcomm")
  add_tests("consumer")
end)

target("libcomm-install-test", function()
  set_kind("phony")
  add_deps("libcomm")
  add_tests("consumer")
  on_test(function(target, opt)
    import("target.action.install", { alias = "install_target" })
    local prefix = os.tmpfile()
    os.tryrm(prefix)
    install_target(target:dep("libcomm"), { installdir = prefix, includedir = "include" })
    local passed = import("testing").run_with(
      "bash",
      { path.join(target:scriptdir(), "tests/unit/install_consumer.sh"), prefix },
      opt
    )
    os.tryrm(prefix)
    return passed
  end)
end)
