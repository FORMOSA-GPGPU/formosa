-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Match the same simulator reports as the CMake cases. Deadlock deliberately
-- exits with failure; neither a timeout nor an unrelated failure is success.
local cases = {
  ["deadlock"] = {
    "barrier deadlock in WG slot 0 .*: 1 of %d+ warps exited .*warp@pc: 1@0x11008",
  },
  ["exited-lanes"] = { "Kernel completed" },
  ["divergent-lanes"] = {
    "has lanes waiting at different barriers %(pc 0x11010 and 0x1100c%).*Kernel completed",
    "has lanes waiting at different barriers %(pc 0x1100c and 0x11010%).*Kernel completed",
  },
  ["divergent-warps"] = {
    "resumes warps waiting at different barriers %(warp@pc: 0@0x11008, 1@0x11010.*Kernel completed",
  },
}

for name, patterns in pairs(cases) do
  target("kernel-sim-barrier-" .. name, function()
    add_rules("kernel-sim.elf", "linker.link_scripts")
    add_deps("lv", { inherit = false })
    add_files(name .. ".S", "$(projectdir)/tests/kernel-sim/common/link.ld")
    set_filename("barrier." .. name .. ".kernel-sim.elf")
    for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
      add_tests(sm, { sm = sm, run_timeout = 60000 })
    end
    on_test(function(target, opt)
      local stdout, stderr = os.tmpfile(), os.tmpfile()
      local code, errors = os.execv(path.absolute(target:dep("lv"):targetfile()), {
        path.join(target:scriptdir(), "barrier.lua"),
        path.absolute(target:targetfile()),
        "--sm",
        opt.sm,
      }, { try = true, timeout = opt.run_timeout, stdout = stdout, stderr = stderr })
      opt.stdout = os.isfile(stdout) and io.readfile(stdout) or ""
      opt.stderr = os.isfile(stderr) and io.readfile(stderr) or ""
      os.tryrm(stdout)
      os.tryrm(stderr)
      local log = opt.stdout .. "\n" .. opt.stderr
      local matched = false
      for _, pattern in ipairs(patterns) do
        if log:match(pattern) then matched = true end
      end
      if
        name == "exited-lanes"
        and (log:find("different barriers", 1, true) or log:find("barrier deadlock", 1, true))
      then
        matched = false
      end
      local passed = code == (name == "deadlock" and 1 or 0) and matched
      if not passed then
        opt.errors = errors or "Unexpected barrier exit status or simulator report"
      end
      return passed
    end)
  end)
end
