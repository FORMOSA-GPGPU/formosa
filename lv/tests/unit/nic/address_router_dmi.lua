-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Compose the same generic endpoints used by the memory and CPU tests.
-- Exercise nested translation, clipped pointers and reverse invalidation
-- through the public Router/XBar bindings, with two masters sharing a grant.
local function ns(value) return sc.time(value, sc.time_unit.NS) end
local target =
  dbg.TestMemory("target", { size = 0x400, read_latency = ns(3), write_latency = ns(5) })
local readonly = dbg.TestMemory("readonly", {
  size = 0x400,
  read_latency = ns(3),
  write_latency = ns(5),
  dmi = "read_only",
})
local outer = nic.XBar("outer", 2, { { addr = 0x1000, size = 0x80 } })
local inner = nic.Router("inner", { { addr = 0x20, size = 0x40, subtract_start_addr = false } })
local identity =
  nic.Router("identity", { { addr = 0x200, size = 0x40, subtract_start_addr = false } })
outer.mem_side = inner.from
inner.to = target.port
identity.to = readonly.port
local a, b, c = dbg.TransportProbe("a"), dbg.TransportProbe("b"), dbg.TransportProbe("c")
a.target, b.target, c.target = outer.core_side, outer.core_side, identity.from

local bytes = {}
for i = 1, 0x40 do
  bytes[i] = (i * 7 + 0x41) % 256
end
target:write_bytes(0x20, bytes)
readonly:write_bytes(0x200, bytes)

local function equal(actual, expected)
  assert(#actual == #expected, "wrong DMI data length")
  for i, value in ipairs(expected) do
    assert(actual[i] == value, "DMI pointer offset or backing storage is wrong")
  end
end
local function grant(probe, addr, first, last, writable)
  -- DMI queries describe an address; a zero-length payload must be accepted.
  local result = probe:request_dmi({ addr = addr })
  assert(result.granted and probe.dmi_valid, "DMI grant lost")
  assert(result.address == addr, "DMI query address changed")
  assert(result.start == first and result.end_address == last, "DMI range not clipped/translated")
  assert(result.read and result.write == writable, "DMI permissions changed")
  assert(result.read_latency_ns == 3 and result.write_latency_ns == 5, "DMI latency changed")
end

sc.start(sc.ZERO_TIME)
for _, probe in ipairs({ a, b }) do
  grant(probe, 0x1028, 0x1020, 0x105f, true)
  -- Non-uniform bytes check the adjusted pointer at both ends of the grant.
  equal(probe:dmi_read(0x1020, 0x40), bytes)
end
grant(c, 0x210, 0x200, 0x23f, false)
equal(c:dmi_read(0x200, 0x40), bytes)

local response = a:blocking({ addr = 0x1028, data = { 0 } })
assert(response.response == "TLM_OK_RESPONSE" and response.dmi_allowed, "blocking DMI hint lost")
assert(response.data[1] == bytes[9] and response.delay_ns == 3, "blocking data/delay changed")
assert(response.elapsed_ns == 0, "blocking transport waited instead of annotating")

-- The old allocation stays alive with different bytes. A stale pointer must
-- produce wrong data, not merely rely on freed memory happening to crash.
target:replace()
target:write_bytes(0x28, { 0x77 })
for _, probe in ipairs({ a, b }) do
  local invalidations = probe.invalidations
  assert(#invalidations == 1, "reverse invalidation lost or duplicated")
  equal(invalidations[1], { 0x1020, 0x105f })
  assert(not probe.dmi_valid, "overlapping grant remained valid")
  grant(probe, 0x1028, 0x1020, 0x105f, true)
  equal(probe:dmi_read(0x1028, 1), { 0x77 })
end
assert(#c.invalidations == 0 and c.dmi_valid, "unrelated path invalidated")
equal(c:dmi_read(0x200, 0x40), bytes)

-- Reuse the payload with a successful DMI hint, then query a hole in the
-- inner map. Both the hint and the translated request address must recover.
local rejected = a:request_dmi({ addr = 0x1010, data = { 0 } })
assert(not rejected.granted and not rejected.dmi_allowed, "unmapped DMI query accepted/stale hint")
assert(rejected.address == 0x1010, "rejected DMI query address not restored")
print("Pass!")
