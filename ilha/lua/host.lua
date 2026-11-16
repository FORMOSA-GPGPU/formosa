-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

--- How a host reaches the device. Each function returns a constructor that
--- System calls while it builds the device; the module it creates provides
--- `target` (host accesses into the device), `port` (device DMA into host
--- memory) and `start()` (serve the host once the device leaves reset).
---@class ilha.host
local Host = {}

local addr = require("ilha.addr_map")

---@alias ilha.host.interface ipc.Agent|vfio_user.PcieEndpoint

--- A HAL process connected over the IPC socket (the lv REAL backend).
---@param socket_path string
---@param opts? { keep_alive?: boolean } keep_alive ignores the client's Terminate
---@return fun(): ilha.host.interface
function Host.ipc(socket_path, opts)
  return function()
    return ipc.Agent("agent", {
      socket_path = socket_path,
      timeout_ms = 100,
      debug = false,
      ignore_terminate = opts ~= nil and opts.keep_alive or false,
      synchronous = true,
    })
  end
end

--- A PCIe endpoint served to QEMU's vfio-user-pci client. BAR0 maps the
--- address map's host BAR window.
---@param socket_path string
---@return fun(): ilha.host.interface
function Host.pcie(socket_path)
  return function()
    return vfio_user.PcieEndpoint("agent", {
      socket_path = socket_path,
      vendor_id = 0x1234,
      device_id = 0xf05a,
      class_code = 0x120000, -- processing accelerator
      bars = { { index = 0, base = addr.host_bar_base, size = addr.host_bar_size } },
    })
  end
end

return Host
