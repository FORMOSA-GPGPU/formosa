-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local platform = require("cp.platform")
-- Test-platform ownership policy. Interrupt probes (0x3000..0x300b), prog0
-- completion/data (0x7ffc..0x80b3) and benchmark mailboxes are observed by the
-- testbench and must use synchronized AT, even though RAM can grant full DMI.
-- Keep these addresses out of the native CPU and generic runner.
return {
  { addr = platform.ram_base, size = 0x3000 - platform.ram_base },
  { addr = 0x3100, size = 0x7ffc - 0x3100 },
  { addr = 0x8100, size = platform.ram_base + platform.ram_size - 0x8100 },
}
