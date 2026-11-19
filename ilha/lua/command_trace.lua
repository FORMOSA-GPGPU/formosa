-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

--- Host-command trace for the Host ABI v3 command ring.
---
--- A dbg.AccessWatcher on the scratch link reports the writes this module
--- decodes; nothing here touches simulated state or time. Each Host command
--- becomes one slice from the CP consuming it (RD_PTR advance) to the CP
--- publishing its completion slot. Kernels, memory copies, cache operations
--- and barriers get separate tracks because the firmware overlaps them, and a
--- type opens another lane when its own commands overlap. A command is named
--- by the label the host passed with it, which arrives as the annotation of
--- its packet write, or else by its type; sizes and geometry are arguments.
---
--- The packet types come from hal/src/hal/command_packet.h through the
--- LuaJIT FFI, which takes the declarations ilha.command_packet_abi carries; the
--- decoder reads fields by name and holds no offsets. Trace arguments are
--- doubles, so 64-bit fields are passed as hex strings.

--- Trace arguments as {key, value} pairs in display order; a nil value is
--- skipped.
---@alias ilha.command_trace.args [string, any][]

---@class ilha.command_trace.sink
---@field track fun(self, name: string): any
---@field begin fun(self, track: any, name: string, args: ilha.command_trace.args)
---@field finish fun(self, track: any)
---@field instant fun(self, track: any, name: string, args: ilha.command_trace.args)

---@class ilha.command_trace.opts
---@field addr ilha.addr_map
---@field base integer Address of the first byte the watched link can see.
---@field sink? ilha.command_trace.sink Defaults to the lv Perfetto tracks.

--- Byte range of a field, within a command packet, completion token or slot.
---@class ilha.command_trace.bytes
---@field offset integer
---@field size integer

---@class ilha.command_trace
---@overload fun(opts: ilha.command_trace.opts): ilha.command_trace
local CommandTrace = {}
CommandTrace.__index = CommandTrace

local PARENT_TRACK = "Host commands"

local bit = require("bit")
local ffi = require("ffi")

local ABI = ilha.command_packet_abi
-- Declares the Host ABI types in this Lua state, once, as require loads this
-- module once.
ffi.cdef(ABI.cdef)
local C = ffi.C
local PACKET_BYTES = ffi.sizeof("union Packet")
local TOKEN_OFFSET = ffi.offsetof("HeaderOnlyPacket", "completion_token")
local HEADER = {
  kernel_dispatch = C.kKernelDispatchPacketHeader,
  cache_flush = C.kCacheFlushPacketHeader,
  cache_invalidate = C.kCacheInvalidatePacketHeader,
  barrier = C.kBarrierPacketHeader,
  memory_copy = C.kMemoryCopyPacketHeader,
}
local COMPLETION = ABI.completion
local DISPATCH_FLAGS = ABI.dispatch_flags
local KNOWN_DISPATCH_FLAGS =
  bit.bor(DISPATCH_FLAGS.dim_mask, DISPATCH_FLAGS.printf_meta, DISPATCH_FLAGS.stack_remap)

local DOMAIN_NAMES = { [C.kFsaMemoryDomainHost] = "H", [C.kFsaMemoryDomainDevice] = "D" }

local KINDS = {
  [HEADER.kernel_dispatch] = "Kernels",
  [HEADER.cache_flush] = "Cache commands",
  [HEADER.cache_invalidate] = "Cache commands",
  [HEADER.barrier] = "Barriers",
  [HEADER.memory_copy] = "Memory copies",
}

---@return ilha.command_trace.sink
local function perfetto_sink()
  return {
    track = function(_, name) return trace.Track(name, false, PARENT_TRACK) end,
    begin = function(_, track, name, args) trace.event_begin(track, "Command", name, args) end,
    finish = function(_, track) trace.event_end(track) end,
    instant = function(_, track, name, args) trace.event_instant(track, "Command", name, args) end,
  }
end

---@param args ilha.command_trace.args
local function arg(args, key, value) args[#args + 1] = { key, value } end

-- A 64-bit field as a 0x-prefixed hex string of 16 digits.
local function hex64(value) return "0x" .. bit.tohex(value) end

-- A 0x-prefixed hex string without its leading zeros.
local function short_hex(hex)
  local digits = hex:sub(3):gsub("^0+", "")
  return "0x" .. (digits == "" and "0" or digits)
end

---@param opts ilha.command_trace.opts
---@return ilha.command_trace
function CommandTrace.new(opts)
  local addr = opts.addr
  assert(addr.cmd_packet_size == PACKET_BYTES, "addr_map and the Host ABI disagree on packet size")
  local self = setmetatable({}, CommandTrace)
  self._base = opts.base
  self._sink = opts.sink or perfetto_sink()
  self._entries = addr.cmd_ring_entries
  self._packet_size = addr.cmd_packet_size
  self._ring_base = addr.cmd_ring_base
  self._rd_ptr_addr = addr.fsa_mmio_base + addr.cp_off_rd_ptr
  self._wr_ptr_addr = addr.fsa_mmio_base + addr.cp_off_wr_ptr
  self._pool_base = addr.completion_pool_base
  self._slot_bytes = addr.completion_slot_bytes
  self._pool_bytes = addr.completion_pool_bytes
  self._shadow = {}
  self._packet = ffi.new("union Packet") -- the ring entry being decoded
  self._rd_ptr = 0
  self._wr_ptr = 0
  self._labels = {} -- ring index -> label of the packet written there
  self._open = {} -- completion slot index -> open command
  self._lanes = {} -- kind -> { {track = ..., busy = bool}, ... }
  self._submissions = nil
  return self
end

--- Ranges, as seen on the watched link, whose writes the trace needs.
---@return {addr: integer, size: integer}[]
function CommandTrace:watch_ranges()
  local base = self._base
  return {
    { addr = self._rd_ptr_addr - base, size = 8 },
    { addr = self._wr_ptr_addr - base, size = 8 },
    { addr = self._ring_base - base, size = self._entries * self._packet_size },
    { addr = self._pool_base - base, size = self._pool_bytes },
  }
end

-- The little-endian value of `size` bytes at `addr`.
function CommandTrace:_u(addr, size)
  local value = 0
  for i = size - 1, 0, -1 do
    value = value * 256 + (self._shadow[addr + i] or 0)
  end
  return value
end

---@param bytes ilha.command_trace.bytes
function CommandTrace:_field(addr, bytes) return self:_u(addr + bytes.offset, bytes.size) end

-- The packet at a ring index, copied from the shadow into self._packet.
function CommandTrace:_load_packet(index)
  local p = self._ring_base + index * PACKET_BYTES
  local packet = self._packet
  for i = 0, PACKET_BYTES - 1 do
    packet.raw[i] = self._shadow[p + i] or 0
  end
  return packet
end

function CommandTrace:_lane(kind)
  local lanes = self._lanes[kind]
  if lanes == nil then
    lanes = {}
    self._lanes[kind] = lanes
  end
  for _, lane in ipairs(lanes) do
    if not lane.busy then return lane end
  end
  local name = #lanes == 0 and kind or string.format("%s #%d", kind, #lanes + 1)
  local lane = { track = self._sink:track(name), busy = false }
  lanes[#lanes + 1] = lane
  return lane
end

function CommandTrace:_submission_track()
  if self._submissions == nil then self._submissions = self._sink:track("Submissions") end
  return self._submissions
end

--- Decode the packet at a ring index into a slice name, its arguments and
--- its completion token.
---@return integer header
---@return string name
---@return ilha.command_trace.args args
---@return string token
function CommandTrace:_decode(index)
  local packet = self:_load_packet(index)
  local header = packet.header_only_packet.header
  local token = hex64(packet.header_only_packet.completion_token)
  local label = self._labels[index]
  local args = { { "label", label } }
  local name
  if header == HEADER.kernel_dispatch then
    local k = packet.kernel_dispatch_packet
    local groups = { k.num_groups_x, k.num_groups_y, k.num_groups_z }
    local loc = { k.local_size_x, k.local_size_y, k.local_size_z }
    local flags = k.dim_reserved0
    local other_flags = bit.band(flags, bit.bnot(KNOWN_DISPATCH_FLAGS))
    local trampoline = hex64(k.kernel_trampoline)
    arg(args, "dims", bit.band(flags, DISPATCH_FLAGS.dim_mask))
    arg(
      args,
      "global",
      string.format("%dx%dx%d", groups[1] * loc[1], groups[2] * loc[2], groups[3] * loc[3])
    )
    arg(
      args,
      "global_offset",
      string.format("%d,%d,%d", k.global_offset_x, k.global_offset_y, k.global_offset_z)
    )
    arg(args, "groups", string.format("%dx%dx%d", groups[1], groups[2], groups[3]))
    arg(args, "local", string.format("%dx%dx%d", loc[1], loc[2], loc[3]))
    arg(args, "local_mem", k.local_mem_size)
    arg(args, "printf_meta", bit.band(flags, DISPATCH_FLAGS.printf_meta) ~= 0)
    arg(args, "stack_remap", bit.band(flags, DISPATCH_FLAGS.stack_remap) ~= 0)
    if other_flags ~= 0 then arg(args, "other_flags", string.format("0x%x", other_flags)) end
    -- The HAL passes the program's entry as the kernel object; the trampoline
    -- is the kernel's own wrapper, so it tells kernels apart.
    arg(args, "entry", hex64(k.kernel_object))
    arg(args, "trampoline", trampoline)
    arg(args, "kernarg", hex64(k.kernarg_address))
    name = "kernel " .. short_hex(trampoline)
  elseif header == HEADER.memory_copy then
    local m = packet.memory_copy_packet
    local src = DOMAIN_NAMES[m.src_domain] or "?"
    local dst = DOMAIN_NAMES[m.dst_domain] or "?"
    local direction = src .. "2" .. dst
    arg(args, "direction", direction)
    arg(args, "bytes", tonumber(m.size))
    arg(args, "src", hex64(m.src_addr))
    arg(args, "dst", hex64(m.dst_addr))
    name = "memcpy " .. direction
  elseif header == HEADER.cache_flush or header == HEADER.cache_invalidate then
    local c = packet.cache_operation_packet
    local size = tonumber(c.size)
    if size == 0 then
      -- Size 0 selects the whole cache and ignores addr.
      arg(args, "whole_cache", true)
    else
      arg(args, "addr", hex64(c.addr))
      arg(args, "bytes", size)
    end
    name = header == HEADER.cache_flush and "cache flush" or "cache invalidate"
  elseif header == HEADER.barrier then
    name = "barrier"
  else
    name = string.format("header %d", header)
  end
  arg(args, "token", token)
  arg(args, "ring_index", index)
  return header, label or name, args, token
end

function CommandTrace:_end(slot, command)
  self._sink:finish(command.lane.track)
  command.lane.busy = false
  self._open[slot] = nil
end

function CommandTrace:_close_all(reason)
  for slot, command in pairs(self._open) do
    self._sink:instant(command.lane.track, reason, { { "token", command.token } })
    self:_end(slot, command)
  end
end

function CommandTrace:_on_wr_ptr(value)
  -- The host never fills the ring and RD_PTR never passes WR_PTR, so a
  -- doorbell never leaves WR_PTR on RD_PTR; a reboot zeroing WR_PTR after
  -- RD_PTR does.
  if value == (self._wr_ptr + 1) % self._entries and value ~= self._rd_ptr then
    local _, name, args = self:_decode(self._wr_ptr)
    self._sink:instant(self:_submission_track(), name, args)
  end
  self._wr_ptr = value
end

function CommandTrace:_on_rd_ptr(value)
  if value ~= (self._rd_ptr + 1) % self._entries or self._rd_ptr == self._wr_ptr then
    -- The CP takes a command only from a non-empty ring and advances RD_PTR
    -- by one, so anything else is a reset or reboot rewinding the ring, even
    -- one that zeroes RD_PTR from the last entry; nothing in flight will
    -- complete. A reboot that discards a command queued at the last entry
    -- looks like the CP taking it.
    if value ~= self._rd_ptr then self:_close_all("Ring reset") end
    self._rd_ptr = value
    return
  end
  local index = self._rd_ptr
  self._rd_ptr = value
  local header, name, args, token = self:_decode(index)
  local token_addr = self._ring_base + index * self._packet_size + TOKEN_OFFSET
  local slot = self:_field(token_addr, COMPLETION.slot_index)
  local stale = self._open[slot]
  if stale ~= nil then
    -- The firmware dropped the previous command of this slot unanswered.
    self._sink:instant(stale.lane.track, "No completion", { { "token", stale.token } })
    self:_end(slot, stale)
  end
  local lane = self:_lane(KINDS[header] or "Other commands")
  lane.busy = true
  self._sink:begin(lane.track, name, args)
  -- The 48-bit allocation tag is exact in a double; a later packet may reuse
  -- this ring entry before the command completes.
  self._open[slot] =
    { lane = lane, token = token, alloc_tag = self:_field(token_addr, COMPLETION.alloc_tag) }
end

function CommandTrace:_on_completion(slot_addr)
  local result = self:_field(slot_addr, COMPLETION.result)
  if result == COMPLETION.pending then return end -- the host arming the slot
  local slot = (slot_addr - self._pool_base) / self._slot_bytes
  local command = self._open[slot]
  if command == nil then return end
  if self:_field(slot_addr, COMPLETION.alloc_tag) ~= command.alloc_tag then
    return -- another allocation of this slot
  end
  if result ~= COMPLETION.success then
    self._sink:instant(
      command.lane.track,
      "Failed",
      { { "result", result }, { "token", command.token } }
    )
  end
  self:_end(slot, command)
end

--- AccessWatcher on_write callback.
---@param link_addr integer
---@param data integer[]
---@param enables boolean[]|nil
---@param annotation string|nil On a packet write, the host's label for the command.
function CommandTrace:on_write(link_addr, data, enables, annotation)
  local addr = self._base + link_addr
  for i = 1, #data do
    if enables == nil or enables[i] then self._shadow[addr + i - 1] = data[i] end
  end
  -- A packet write starts at its ring entry; a later one replaces its label.
  local ring_offset = addr - self._ring_base
  if
    ring_offset >= 0
    and ring_offset < self._entries * self._packet_size
    and ring_offset % self._packet_size == 0
  then
    -- An empty annotation is no label.
    self._labels[ring_offset / self._packet_size] = annotation ~= "" and annotation or nil
  end
  local finish = addr + #data
  local function covers(a) return addr <= a and a + 8 <= finish end
  if covers(self._wr_ptr_addr) then self:_on_wr_ptr(self:_u(self._wr_ptr_addr, 8)) end
  if covers(self._rd_ptr_addr) then self:_on_rd_ptr(self:_u(self._rd_ptr_addr, 8)) end
  local pool_end = self._pool_base + self._pool_bytes
  if addr < pool_end and finish > self._pool_base then
    local first = math.max(addr, self._pool_base)
    first = first - (first - self._pool_base) % self._slot_bytes
    for slot_addr = first, math.min(finish, pool_end) - 1, self._slot_bytes do
      if covers(slot_addr) then self:_on_completion(slot_addr) end
    end
  end
end

setmetatable(CommandTrace --[[@as table]], {
  __call = function(cls, ...) return cls.new(...) end,
})

return CommandTrace
