-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- simple.LtMemory through its standard socket: zero initialization, annotated
-- latency without waiting, byte enables, one storage shared by blocking, DMI
-- and debug transport and the backdoor, error responses, and ELF
-- physical-address/BSS loading. One dbg.TransportProbe payload carries every
-- call, so a stale DMI hint from an earlier success would survive a later
-- error.

local function ns(value) return sc.time(value, sc.time_unit.NS) end
local ENABLED, DISABLED = 0xff, 0x00

local function filled(value, count)
  local bytes = {}
  for i = 1, count do
    bytes[i] = value
  end
  return bytes
end

local function same_time(a, b) return not (a < b) and not (b < a) end

local function expect(result, response, message)
  assert(result.response == response, message .. ": got " .. result.response)
  return result
end

local memory = simple.LtMemory("memory", { size = 64, latency = ns(3) })
local probe = dbg.TransportProbe("probe")
probe.target = memory.port

sc.start(sc.ZERO_TIME)
local before = sc.time_stamp()

-- A fresh read sees zeros and adds the latency to the annotated delay, without
-- waiting in the target.
local read = expect(
  probe:blocking({
    command = "read",
    addr = 8,
    data = filled(0xee, 8),
    byte_enables = filled(ENABLED, 8),
    delay_ns = 9,
  }),
  "TLM_OK_RESPONSE",
  "LT initialization"
)
for i = 1, 8 do
  assert(read.data[i] == 0, "LT memory is not zero-initialized")
end
assert(read.delay_ns == 12 and read.elapsed_ns == 0, "annotated latency changed or target waited")

-- The CP's scalar byte-enable array: the disabled byte stays unwritten.
local enables = filled(ENABLED, 8)
enables[2] = DISABLED
expect(
  probe:blocking({ command = "write", addr = 8, data = filled(0xaa, 8), byte_enables = enables }),
  "TLM_OK_RESPONSE",
  "byte-enable write"
)

-- The WRITE grant checks pointer coherence on its own before a READ grant
-- replaces it: the blocking write and the pointer share storage.
local grant = probe:request_dmi({ command = "write", addr = 8 })
assert(
  grant.granted and grant.dmi_allowed and grant.read and grant.write,
  "DMI write grant refused"
)
assert(grant.start == 0 and grant.end_address == 63, "DMI range incorrect")
assert(grant.read_latency_ns == 3 and grant.write_latency_ns == 3, "DMI latency incorrect")
local seen = probe:dmi_read(8, 2)
assert(seen[1] == 0xaa and seen[2] == 0, "blocking/DMI storage differs or write mask ignored")
probe:dmi_write(8, { 0x55 })
local masked = expect(
  probe:blocking({ command = "read", addr = 8, data = filled(0xee, 8), byte_enables = enables }),
  "TLM_OK_RESPONSE",
  "byte-enable read"
)
assert(
  masked.data[1] == 0x55 and masked.data[2] == 0xee and masked.data[3] == 0xaa,
  "DMI write not visible through blocking read or read mask ignored"
)

local read_grant = probe:request_dmi({ command = "read", addr = 8 })
assert(
  read_grant.granted and read_grant.read and read_grant.start == 0 and read_grant.end_address == 63,
  "DMI read grant incorrect"
)
assert(probe:dmi_read(8, 1)[1] == 0x55, "READ grant points to another storage")

-- The backdoor shares the storage too.
memory:write_bytes(16, { 0x56 })
assert(probe:dmi_read(16, 1)[1] == 0x56, "backdoor write uses another storage")
assert(memory:read_bytes(8, 1)[1] == 0x55, "backdoor read uses another storage")

-- Debug ignores stale timed attributes: an invalid byte-enable pointer and a
-- zero streaming width stay on the payload exactly as given.
local debug = probe:debug({
  command = "write",
  addr = 8,
  data = filled(0x33, 8),
  stale_byte_enables = true,
  streaming_width = 0,
})
assert(debug.count == 8 and debug.elapsed_ns == 0, "debug transport used timed attributes or time")
assert(debug.dmi_allowed, "debug success left no DMI hint for the next check")
assert(probe:dmi_read(9, 1)[1] == 0x33, "debug transport used a different storage")
local debug_read = probe:debug({ command = "read", addr = 8, data = filled(0, 2) })
assert(debug_read.count == 2 and debug_read.data[1] == 0x33, "debug read failed")

-- The same payload still carries that hint: an error response must clear it.
local bounds = expect(
  probe:blocking({ command = "write", addr = 63, data = filled(0, 8) }),
  "TLM_ADDRESS_ERROR_RESPONSE",
  "out-of-bounds transfer"
)
assert(not bounds.dmi_allowed, "error response kept the stale DMI hint")

-- The address converts to 2^64 - 8, so addr + length wraps to 0, inside the
-- memory, unless bounds avoid overflow.
local wrapped = probe:debug({ command = "write", addr = -8, data = filled(0, 8) })
assert(wrapped.count == 0, "overflowing debug transfer accepted")
expect(
  probe:blocking({ command = "write", addr = 8, data = filled(0, 8), streaming_width = 4 }),
  "TLM_BURST_ERROR_RESPONSE",
  "unsupported streaming payload"
)
expect(
  probe:blocking({
    command = "write",
    addr = 8,
    data = filled(0, 8),
    byte_enables = filled(ENABLED, 8),
    byte_enable_length = 0,
  }),
  "TLM_BYTE_ENABLE_ERROR_RESPONSE",
  "zero-length byte-enable array"
)
expect(
  probe:blocking({ command = "ignore", addr = 8, data = filled(0, 8) }),
  "TLM_COMMAND_ERROR_RESPONSE",
  "unsupported command"
)

-- An ELF64 image with one PT_LOAD segment: physical address 8, virtual 32,
-- two file bytes and six BSS bytes. The BSS bytes still hold 0x33 from the
-- debug write, so a missing BSS clear leaves them set.
local function le(value, size)
  local out = {}
  for i = 1, size do
    out[i] = string.char(value % 256)
    value = math.floor(value / 256)
  end
  return table.concat(out)
end
local header = "\127ELF"
  .. string.char(2, 1, 1)
  .. string.rep("\0", 9)
  .. le(2, 2) -- ET_EXEC
  .. le(243, 2) -- EM_RISCV
  .. le(1, 4)
  .. le(0, 8) -- e_entry
  .. le(64, 8) -- e_phoff
  .. le(0, 8) -- e_shoff
  .. le(0, 4)
  .. le(64, 2)
  .. le(56, 2)
  .. le(1, 2)
  .. le(64, 2)
  .. le(0, 2)
  .. le(0, 2)
local program = le(1, 4) -- PT_LOAD
  .. le(6, 4) -- PF_R | PF_W
  .. le(120, 8) -- p_offset
  .. le(32, 8) -- p_vaddr
  .. le(8, 8) -- p_paddr
  .. le(2, 8) -- p_filesz
  .. le(8, 8) -- p_memsz
  .. le(1, 8)
local path = os.tmpname()
local file = assert(io.open(path, "wb"))
file:write(header .. program .. string.char(0x12, 0x34))
file:close()
local elf = workload.ELF(path)
os.remove(path)
memory:load_elf(elf)
local loaded = probe:dmi_read(8, 8)
assert(loaded[1] == 0x12 and loaded[2] == 0x34, "ELF physical address not used")
for i = 3, 8 do
  assert(loaded[i] == 0, "ELF BSS not cleared")
end
assert(probe:dmi_read(32, 1)[1] == 0, "ELF loaded at its virtual address")

assert(same_time(sc.time_stamp(), before), "memory API unexpectedly advanced time")
print("LtMemory transport, DMI, timing, errors and ELF: Pass!")
