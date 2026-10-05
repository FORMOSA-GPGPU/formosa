-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local cpu = require("cp.platform")
return {
  ram_base = cpu.ram_base,
  ram_size = cpu.ram_size,
  text_base = cpu.text_base,
  stack_base = cpu.stack_base,

  plic_base = 0x0c000000,
  plic_size = 0x4000000,

  -- Fixture mailbox base, followed by byte offsets for its fields.
  mailbox = 0x3000,
  ready = 0,
  claimed_id = 4,
  claim_seq = 8,
  ack_seq = 12,

  handled = 16,
  returned = 20,
  done = 24,
  error = 28,
  irq_entries = 32,
  -- Three consecutive uint32_t entries record the claimed source IDs.
  log = 36,
}
