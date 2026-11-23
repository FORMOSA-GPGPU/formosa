-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

require("baremetal-freertos.runner")({ ... }, "baremetal_demo", function(system)
  if system:output():find("Hello, bare-metal CPU!\n", 1, true) then return true end
end)
