-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local initiator = simple.Initiator("initiator")
initiator.clock = clock

local cache_mmio = formosa.StubCacheMmio("cache_mmio", {
  verbose = true,
})
cache_mmio.clock = clock

initiator.target = cache_mmio.mmio_port

local function to_byte_arr(data)
  local byte_arr = {}
  for _ = 1, 8 do
    table.insert(byte_arr, bit.band(data, 0xff))
    data = bit.arshift(data, 8)
  end
  return byte_arr
end

local function put_cache_cmd(addr, size, mode)
  initiator:add_payload({ addr = 0x08, data = to_byte_arr(addr) })
  initiator:add_payload({ addr = 0x10, data = to_byte_arr(size) })
  initiator:add_payload({ addr = 0x18, data = to_byte_arr(mode) })
  initiator:add_payload({ addr = 0x00, data = { 0x1 } })

  -- poll until start is deasserted by the cache
  initiator:add_payload({ addr = 0x00, value = 0, poll = true })
  sc.start()
  print("===------- cycle: ", sc.time_stamp() / period, "-------===")
end

-- 1. NOP
put_cache_cmd(0xdeadbeef, 0xcafecafe, 0)

-- 2. Invalidate
put_cache_cmd(0xbaadf00d, 0xabababab, 1)

-- 3. Flush
put_cache_cmd(0xbaadf00d, 0xabababab, 2)

-- 4. Invalid operation
put_cache_cmd(0x1badb002, 0xcafefeed, 87)

print("Pass!")
