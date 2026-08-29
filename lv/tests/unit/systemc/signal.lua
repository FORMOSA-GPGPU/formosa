-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local sig_bool = sc.signal("signal_test", false)
local wait_time = sc.time(10, sc.time_unit.NS)
assert(sig_bool:read() == false, "Initial value must be false")
--- start simulation
sc.start(wait_time)
sig_bool:write(true)
sc.start(wait_time)
assert(sig_bool:read() == true, "Value must be true")
sig_bool:write(false)
sc.start(wait_time)
assert(sig_bool:read() == false, "Value must be false")
