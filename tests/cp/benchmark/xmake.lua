-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target(
  "lv",
  function()
    add_tests(
      "cp/benchmark/check_test",
      { runargs = { path.join(os.scriptdir(), "check_test.lua") }, run_timeout = 30000 }
    )
  end
)
