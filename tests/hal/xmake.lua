-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, name in ipairs({ "memory-copy", "reboot" }) do
  target("hal-" .. name .. "-test.lv", function()
    set_kind("binary")
    add_files("hal_" .. name:gsub("-", "_") .. "_test.cc")
    add_deps("formosa-hal.lv", "ilha-platform-data")
  end)
end

-- Decodes a Perfetto trace for a slice of a given name on a track of a
-- given name; trace-invariance runs it in a Perfetto build.
target("hal-trace-slice-check", function()
  set_kind("binary")
  add_files("trace_slice_check.cc")
  add_packages("perfetto")
end)

target("hal-integration-test.lv", function()
  set_kind("phony")
  add_deps(
    "lv",
    "fw.runfiles",
    "hal-memory-copy-test.lv",
    "hal-reboot-test.lv",
    "hal-trace-slice-check"
  )
  for _, name in ipairs({ "memory-copy", "large-sm-capabilities", "reboot", "trace-invariance" }) do
    -- Coverage plus shared-runner contention can exceed two minutes for the
    -- complete daemon scenario; HAL operations retain their own deadlines.
    add_tests(name, { run_timeout = 300000 })
  end
  on_test(function(target, opt)
    local case = path.filename(opt.name)
    local binary = case == "reboot" and "hal-reboot-test.lv" or "hal-memory-copy-test.lv"
    local runner = case == "trace-invariance" and "run_trace_invariance_test.sh"
      or "run_daemon_test.sh"
    local argv = {
      path.join(target:scriptdir(), runner),
      path.absolute(target:dep("lv"):targetfile()),
      path.join(os.projectdir(), "ilha/daemon.lua"),
      path.absolute(target:dep(binary):targetfile()),
    }
    if case == "trace-invariance" then
      -- Only a Perfetto build records the slices the checker decodes.
      local perfetto = import("workflow.backends.xmake").enabled("lv-enable-perfetto", false)
      table.insert(
        argv,
        perfetto and path.absolute(target:dep("hal-trace-slice-check"):targetfile()) or ""
      )
    end
    opt.runenvs = opt.runenvs or {}
    if case == "large-sm-capabilities" then
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

if is_plat("linux") then
  -- The PCI HAL (Linux VFIO) tests, run in the guest booted by ilha/qemu.lua,
  -- e.g. xmake run ilha-qemu -- build/bin/hal-reboot-test.pci
  for _, name in ipairs({ "memory-copy", "reboot" }) do
    target("hal-" .. name .. "-test.pci", function()
      set_kind("binary")
      add_files("hal_" .. name:gsub("-", "_") .. "_test.cc")
      add_deps("formosa-hal.pci", "ilha-platform-data")
    end)
  end

  -- The PCI HAL tests in a QEMU guest (ilha/qemu.lua, x86_64 only) against the
  -- device served over vfio-user; uses KVM when available.
  target("hal-integration-test.pci", function()
    set_enabled(is_arch("x86_64"))
    set_kind("phony")
    add_deps("lv", "fw.runfiles")
    for _, name in ipairs({ "memory-copy", "reboot" }) do
      add_deps("hal-" .. name .. "-test.pci")
      add_tests(name, { run_timeout = 600000 })
    end
    on_test(function(target, opt)
      local binary = target:dep("hal-" .. path.filename(opt.name) .. "-test.pci")
      return import("testing").run_with(path.absolute(target:dep("lv"):targetfile()), {
        path.join(os.projectdir(), "ilha/qemu.lua"),
        "--",
        path.absolute(binary:targetfile()),
      }, opt)
    end)
  end)
end
