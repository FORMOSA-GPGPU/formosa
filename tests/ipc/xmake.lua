-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("ipc-test", function()
  set_kind("binary")
  add_rules("lv.test")
  add_files("ipc_test.cc")
  add_deps("libcomm")
  on_load(
    function(target)
      target:add("tests", "duplex", {
        runargs = {
          path.join(target:scriptdir(), "ipc_test.lua"),
          path.absolute(target:targetfile()),
        },
        run_timeout = 30000,
      })
    end
  )
end)

target("ipc-synchronous-test", function()
  set_kind("binary")
  add_rules("lv.test")
  add_files("synchronous_host.cc")
  add_deps("libcomm")
  on_load(function(target)
    local command = {
      path.join(target:scriptdir(), "synchronous.lua"),
      path.absolute(target:targetfile()),
    }
    for _, delay in ipairs({ "0", "20" }) do
      target:add("tests", delay, {
        runargs = table.join(command, { delay }),
        run_timeout = 20000,
      })
    end
    target:add("tests", "disconnect", {
      runargs = table.join(command, { "0", "synchronous", "disconnect" }),
      run_timeout = 10000,
    })
  end)
end)
