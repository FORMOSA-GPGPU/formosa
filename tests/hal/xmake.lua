-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, name in ipairs({ "memory_copy", "reboot" }) do
  target("hal-" .. name .. "-test", function()
    set_kind("binary")
    add_files("hal_" .. name .. "_test.cc")
    add_deps("formosa-hal.lv", "ilha-platform-data")
  end)
end

target("hal-daemon-tests", function()
  set_kind("phony")
  add_deps("lv", "fw.runfiles", "hal-memory_copy-test", "hal-reboot-test")
  for _, name in ipairs({ "memory_copy", "large_sm_capabilities", "reboot" }) do
    -- Coverage plus shared-runner contention can exceed two minutes for the
    -- complete daemon scenario; HAL operations retain their own deadlines.
    add_tests(name, { run_timeout = 300000 })
  end
  on_test(function(target, opt)
    local case = path.filename(opt.name)
    local binary = case == "reboot" and "hal-reboot-test" or "hal-memory_copy-test"
    local argv = {
      path.join(target:scriptdir(), "run_daemon_test.sh"),
      path.absolute(target:dep("lv"):targetfile()),
      path.join(os.projectdir(), "ilha/daemon.lua"),
      path.absolute(target:dep(binary):targetfile()),
    }
    opt.runenvs = opt.runenvs or {}
    if case == "large_sm_capabilities" then
      opt.runenvs.FORMOSA_DAEMON_CONFIG = "large_sm"
      opt.runenvs.FORMOSA_EXPECTED_MAX_THREADS = "1024"
    end
    -- Preserve the daemon resource lock used by the existing regression:
    -- concurrent simulator boots can exhaust HAL's firmware-ready deadline.
    import("core.base.scheduler")
    scheduler.co_lock("formosa_daemon")
    return try({
      function() return import("testing").run_with("bash", argv, opt) end,
      finally({
        function(ok, result)
          scheduler.co_unlock("formosa_daemon")
          if not ok then raise(result) end
        end,
      }),
    })
  end)
end)
