-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("lv.device", function()
  set_kind("headeronly")
  add_includedirs("include", { public = true })
end)
