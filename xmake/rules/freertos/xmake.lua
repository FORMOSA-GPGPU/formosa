-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Each target compiles its own kernel with its own configuration and port.
rule("freertos.kernel", function()
  on_load(function(target)
    local configdir = assert(target:extraconf("rules", "freertos.kernel", "configdir"))
    local portdir = assert(target:extraconf("rules", "freertos.kernel", "portdir"))
    local heap = target:extraconf("rules", "freertos.kernel", "heap") or "heap_4"
    local kernel = path.join(os.projectdir(), "third-party/FreeRTOS-Kernel")
    configdir = path.absolute(configdir, target:scriptdir())
    portdir = path.absolute(portdir, target:scriptdir())
    for _, source in ipairs({
      "croutine.c",
      "event_groups.c",
      "list.c",
      "queue.c",
      "stream_buffer.c",
      "tasks.c",
      "timers.c",
    }) do
      target:add("files", path.join(kernel, source))
    end
    target:add("files", path.join(kernel, "portable/MemMang", heap .. ".c"))
    target:add("files", path.join(portdir, "port.c"), path.join(portdir, "portASM.S"))
    target:add("includedirs", configdir, path.join(kernel, "include"), portdir, { public = true })
  end)
end)
