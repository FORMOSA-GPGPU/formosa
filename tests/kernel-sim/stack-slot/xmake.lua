-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Check the simulator's private stack diagnostic. A store or AMO into another
-- thread's slot deliberately stops the run with a fatal report naming the
-- target address; neither a clean exit nor an unrelated failure is success.
for _, name in ipairs({ "store", "amo" }) do
  target("kernel-sim-stack-slot-" .. name, function()
    add_rules("kernel-sim.elf", "linker.link_scripts")
    add_deps("lv", { inherit = false })
    add_files("kernel.c", "../common/start.S", "../common/link.ld")
    add_includedirs("../common")
    set_filename("stack-slot." .. name .. ".kernel-sim.elf")
    for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
      add_tests(sm, { sm = sm, run_timeout = 60000 })
    end
    on_test(function(target, opt)
      local stdout, stderr = os.tmpfile(), os.tmpfile()
      local code, errors = os.execv(path.absolute(target:dep("lv"):targetfile()), {
        path.join(target:scriptdir(), "stack_slot.lua"),
        path.absolute(target:targetfile()),
        name,
        "--sm",
        opt.sm,
      }, { try = true, timeout = opt.run_timeout, stdout = stdout, stderr = stderr })
      opt.stdout = os.isfile(stdout) and io.readfile(stdout) or ""
      opt.stderr = os.isfile(stderr) and io.readfile(stderr) or ""
      os.tryrm(stdout)
      os.tryrm(stderr)
      local log = opt.stdout .. "\n" .. opt.stderr
      local stack_target = log:match("stack target (0x%x+)")
      local passed = code == 1
        and stack_target ~= nil
        and log:find(
            "store/AMO outside the thread's private stack slot: .* address=" .. stack_target .. " "
          )
          ~= nil
      if not passed then opt.errors = errors or "Unexpected exit status or private stack report" end
      return passed
    end)
  end)
end
