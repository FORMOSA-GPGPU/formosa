-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, name in ipairs({ "cp_lmem_allocator", "cp_stack_remap" }) do
  target("fw-" .. name .. "-test", function()
    set_kind("binary")
    add_files(name .. "_test.c", "$(projectdir)/fw/" .. name .. ".c")
    add_includedirs("$(projectdir)/fw")
    add_deps("hal.protocol", "ilha-platform-data")
    add_tests("test", { run_timeout = 30000 })
  end)
end
