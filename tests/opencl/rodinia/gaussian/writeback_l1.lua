-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- A write-back L1 keeps each elimination step's results dirty until a later
-- launch or host copy needs them.
return {
  system = { num_sm = 1 },
  sm = { param = { dcache = { write_hit_policy = "WriteBack" } } },
}
