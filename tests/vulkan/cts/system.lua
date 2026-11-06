-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Exercise Vulkan's 128-invocation minimum and cross-SM memory visibility.
-- Keep the actual hardware subgroup width at four lanes.
return { warps_per_core = 32, num_sm = 2 }
