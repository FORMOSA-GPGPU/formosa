-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

require("baremetal-freertos.runner")(
  { ... },
  "freertos_demo",
  require("baremetal-freertos.freertos_demo.check"),
  { "has_timer_int" }
)
