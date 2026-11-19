-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Feeds ilha.command_trace the Host ABI writes a scratch-link watcher reports,
-- with the annotation a labelled packet write carries, and checks the slices
-- it records. Packets are built from the Host ABI types, which requiring
-- ilha.command_trace declares through the LuaJIT FFI.

local ffi = require("ffi")
local addr = require("ilha.addr_map")
local CommandTrace = require("ilha.command_trace")

local ABI = ilha.command_packet_abi
local C = ffi.C
local COMPLETION = ABI.completion
local PACKET_BYTES = ffi.sizeof("union Packet")

-- ABI metadata is shared by decoders; Lua must not be able to corrupt it.
for _, write_field in ipairs({
  function() ABI.cdef = "" end,
  function() COMPLETION.slot_index.size = 1 end,
  function() COMPLETION.pending = -1 end,
  function() ABI.dispatch_flags.dim_mask = 0 end,
}) do
  assert(not pcall(write_field), "ABI metadata must be read-only")
end

local base = addr.cp_csr_base
local events = {}
local sink = {
  track = function(_, name) return name end,
  begin = function(_, track, name, args) table.insert(events, { "begin", track, name, args }) end,
  finish = function(_, track) table.insert(events, { "end", track }) end,
  instant = function(_, track, name, args) table.insert(events, { "instant", track, name, args }) end,
}
local ct = CommandTrace({ addr = addr, base = base, sink = sink })

local function le(value, n)
  local bytes = {}
  for i = 1, n do
    bytes[i] = value % 256
    value = math.floor(value / 256)
  end
  return bytes
end

local function zeros(n)
  local bytes = {}
  for i = 1, n do
    bytes[i] = 0
  end
  return bytes
end

local function put(bytes, offset, field)
  for i, b in ipairs(field) do
    bytes[offset + i] = b
  end
end

-- Token = boot generation (16) | allocation generation (32) | slot index (16);
-- the allocation tag is both generations.
local function token(slot, generation)
  local bytes = zeros(8)
  put(bytes, COMPLETION.slot_index.offset, le(slot, COMPLETION.slot_index.size))
  put(bytes, COMPLETION.alloc_tag.offset, le(2 ^ 32 + generation, COMPLETION.alloc_tag.size))
  local hex = string.format("0x%04x%08x%04x", 1, generation, slot)
  return bytes, hex
end

-- The bytes of a packet: `fields` sets members of the union Packet member
-- `kind` by name.
local function packet(header, kind, fields, token_bytes)
  local p = ffi.new("union Packet")
  p.header_only_packet.header = header
  for name, value in pairs(fields) do
    p[kind][name] = value
  end
  local bytes = {}
  for i = 0, PACKET_BYTES - 1 do
    bytes[i + 1] = p.raw[i]
  end
  put(bytes, ffi.offsetof("HeaderOnlyPacket", "completion_token"), token_bytes)
  return bytes
end
local function barrier(token_bytes)
  return packet(C.kBarrierPacketHeader, "header_only_packet", {}, token_bytes)
end

local function write(abs, data, annotation) ct:on_write(abs - base, data, nil, annotation) end

local wr, rd = 0, 0
local function submit(index, bytes, label)
  write(addr.cmd_ring_base + index * PACKET_BYTES, bytes, label)
  wr = wr + 1
  write(addr.fsa_mmio_base + addr.cp_off_wr_ptr, le(wr, 8))
end
local function consume()
  rd = rd + 1
  write(addr.fsa_mmio_base + addr.cp_off_rd_ptr, le(rd, 8))
end

-- A completion slot holds its token's allocation tag above the result.
local function complete(slot, token_bytes, result)
  local value = zeros(addr.completion_slot_bytes)
  local tag = COMPLETION.alloc_tag
  for i = tag.offset + 1, tag.offset + tag.size do
    value[i] = token_bytes[i]
  end
  put(value, COMPLETION.result.offset, le(result, COMPLETION.result.size))
  write(addr.completion_pool_base + slot * addr.completion_slot_bytes, value)
end

local function expect(i, kind, track, name)
  local e = events[i]
  assert(e ~= nil, string.format("event %d missing", i))
  assert(
    e[1] == kind and e[2] == track and (name == nil or e[3] == name),
    string.format(
      "event %d: got %s %s %s, want %s %s %s",
      i,
      tostring(e[1]),
      tostring(e[2]),
      tostring(e[3]),
      kind,
      track,
      tostring(name)
    )
  )
  return e[4]
end

-- The value of a trace argument; arguments are {key, value} pairs.
local function get(args, key)
  for _, pair in ipairs(args) do
    if pair[1] == key then return pair[2] end
  end
end

-- 1. A kernel: the host arms its slot and rings the doorbell. Its flags carry
-- printf metadata and a bit this trace does not know.
local DF = ABI.dispatch_flags
local t1, t1hex = token(3, 7)
complete(3, t1, COMPLETION.pending) -- arming records nothing
submit(
  0,
  packet(C.kKernelDispatchPacketHeader, "kernel_dispatch_packet", {
    dim_reserved0 = 1 + DF.printf_meta + 0x10,
    local_size_x = 64,
    local_size_y = 1,
    local_size_z = 1,
    num_groups_x = 4,
    num_groups_y = 1,
    num_groups_z = 1,
    global_offset_x = 8,
    kernel_object = 0x80001000,
    kernel_trampoline = 0x80001100,
  }, t1)
)
local args = expect(1, "instant", "Submissions", "kernel 0x80001100")
assert(get(args, "token") == t1hex and get(args, "ring_index") == 0)
consume()
args = expect(2, "begin", "Kernels", "kernel 0x80001100")
assert(get(args, "groups") == "4x1x1" and get(args, "local") == "64x1x1")
assert(get(args, "global") == "256x1x1" and get(args, "global_offset") == "8,0,0")
assert(get(args, "dims") == 1 and get(args, "printf_meta") == true)
assert(get(args, "stack_remap") == false and get(args, "other_flags") == "0x10")
assert(get(args, "entry") == "0x0000000080001000")
assert(get(args, "trampoline") == "0x0000000080001100" and get(args, "token") == t1hex)

-- 2. A memory copy the CP takes while the kernel still runs gets its own track.
local t2, t2hex = token(4, 8)
submit(
  1,
  packet(C.kMemoryCopyPacketHeader, "memory_copy_packet", {
    src_domain = C.kFsaMemoryDomainHost,
    dst_domain = C.kFsaMemoryDomainDevice,
    src_addr = 0x1000,
    dst_addr = 0x2000,
    size = 4096,
  }, t2)
)
consume()
args = expect(4, "begin", "Memory copies", "memcpy H2D")
assert(get(args, "direction") == "H2D" and get(args, "bytes") == 4096)
assert(get(args, "token") == t2hex)

-- 3. The kernel completes.
complete(3, t1, COMPLETION.success)
expect(5, "end", "Kernels")

-- 4. Two overlapping cache operations use two lanes.
local t3 = token(5, 9)
local t4 = token(6, 10)
local CACHE = "cache_operation_packet"
submit(2, packet(C.kCacheFlushPacketHeader, CACHE, { addr = 0x80000000, size = 0x100000 }, t3))
submit(3, packet(C.kCacheInvalidatePacketHeader, CACHE, { addr = 0x80000000, size = 64 }, t4))
consume()
consume()
args = expect(8, "begin", "Cache commands", "cache flush")
assert(get(args, "bytes") == 0x100000)
expect(9, "begin", "Cache commands #2", "cache invalidate")

-- 5. A failed command records its result before the slice ends.
complete(5, t3, 2)
args = expect(10, "instant", "Cache commands", "Failed")
assert(get(args, "result") == 2)
expect(11, "end", "Cache commands")

-- 6. A completion carrying another allocation's tag is not this command's.
local stale = token(6, 99)
complete(6, stale, COMPLETION.success)
assert(#events == 11, "a completion with a stale tag ended a slice")
complete(6, t4, COMPLETION.success)
expect(12, "end", "Cache commands #2")

-- 7. A free lane is reused; size 0 selects the whole cache and ignores addr.
local t5 = token(7, 11)
submit(4, barrier(t5))
submit(5, packet(C.kCacheFlushPacketHeader, CACHE, { addr = 0x80000000 }, token(8, 12)))
consume()
consume()
expect(15, "begin", "Barriers", "barrier")
args = expect(16, "begin", "Cache commands", "cache flush")
assert(get(args, "whole_cache") == true and get(args, "addr") == nil)
assert(get(args, "bytes") == nil)

-- 8. A labelled packet names its command; rewriting the entry without a
-- label or with an empty one, as the next packet there does once the ring
-- wraps, drops it.
local t6 = token(9, 13)
write(addr.cmd_ring_base + 6 * PACKET_BYTES, barrier(t6), "stale")
submit(6, barrier(t6), "")
local t7, t7hex = token(10, 14)
submit(
  7,
  packet(C.kKernelDispatchPacketHeader, "kernel_dispatch_packet", {
    dim_reserved0 = 1,
    local_size_x = 64,
    local_size_y = 1,
    local_size_z = 1,
    num_groups_x = 4,
    num_groups_y = 1,
    num_groups_z = 1,
  }, t7),
  "vecadd"
)
consume()
consume()
expect(17, "instant", "Submissions", "barrier")
args = expect(18, "instant", "Submissions", "vecadd")
assert(get(args, "label") == "vecadd" and get(args, "token") == t7hex)
args = expect(19, "begin", "Barriers #2", "barrier")
assert(get(args, "label") == nil)
args = expect(20, "begin", "Kernels", "vecadd")
assert(get(args, "label") == "vecadd" and get(args, "groups") == "4x1x1")

-- 9. Rewinding the ring closes what can no longer complete.
write(addr.fsa_mmio_base + addr.cp_off_rd_ptr, le(0, 8))
local closed = {}
for i = 21, #events do
  if events[i][1] == "end" then closed[events[i][2]] = true end
end
assert(closed["Memory copies"] and closed["Barriers"] and closed["Cache commands"])

-- 10. The ring wraps from its last entry, and a reboot zeroes RD_PTR, then
-- WR_PTR, as fw/cp_reboot.c does. Both move a pointer from the last entry to
-- 0; only the wrap is a command.
local n = addr.cmd_ring_entries
write(addr.fsa_mmio_base + addr.cp_off_wr_ptr, le(0, 8)) -- end the rewind
local generation = 100
local function run(index)
  local t = token(11, generation)
  generation = generation + 1
  write(addr.cmd_ring_base + index * PACKET_BYTES, barrier(t))
  write(addr.fsa_mmio_base + addr.cp_off_wr_ptr, le((index + 1) % n, 8))
  write(addr.fsa_mmio_base + addr.cp_off_rd_ptr, le((index + 1) % n, 8))
  complete(11, t, COMPLETION.success)
end
for index = 0, n - 2 do
  run(index)
end
local before = #events
run(n - 1)
args = expect(before + 1, "instant", "Submissions", "barrier")
assert(get(args, "ring_index") == n - 1)
args = expect(before + 2, "begin", "Barriers", "barrier")
assert(get(args, "ring_index") == n - 1)
expect(before + 3, "end", "Barriers")
for index = 0, n - 2 do
  run(index)
end
before = #events
write(addr.fsa_mmio_base + addr.cp_off_rd_ptr, le(0, 8))
write(addr.fsa_mmio_base + addr.cp_off_wr_ptr, le(0, 8))
assert(#events == before, "a reboot of an empty ring recorded a command")

print("Pass!")
