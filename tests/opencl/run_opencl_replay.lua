-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@type formosa.system.config
local config = require("formosa.config")

---@type formosa.system
local System = require("formosa.system")

local argparse = require("argparse")
local stdlib = require("posix.stdlib")
local U = require("posix.unistd")

local function make_agent_socket_path()
  local tmpdir = os.getenv("TMPDIR") or "/tmp"
  local fd, path = stdlib.mkstemp(tmpdir:gsub("/+$", "") .. "/formosa-XXXXXX")
  U.close(fd)
  os.remove(path)
  return path
end

local sm_kinds = require("formosa.sm_kinds")
local parser = argparse("run_opencl_replay.lua")
parser:option("-s --stats", "Output path of the stat"):args(1)
parser
  :option("--sm", "Stream multiprocessor to simulate")
  :args(1)
  :choices(sm_kinds.available())
  :default("simtix.pipelined_sm")
parser
  :option("-t --trace", "Output prefix of the Perfetto trace")
  :args(1)
  :default("run_opencl_replay")
parser
  :option("--drain", "Extra deterministic drain cycles after replay completion")
  :convert(tonumber)
  :default(5000)
parser
  :option("--max-wait-cycles", "Base poll budget for one replay wait")
  :convert(tonumber)
  :default(1000000)
parser
  :option("--check", "Device-to-host output validation policy")
  :choices({ "strict", "unstrict" })
  :default("strict")
parser:flag("--no-progress", "Disable replay progress output")
parser
  :option("--progress-interval", "Minimum seconds between progress updates")
  :convert(tonumber)
  :default(0.25)
parser:argument("replay_dir", "Replay capture directory"):args(1)

local args = parser:parse({ ... })

local make_sm = require(args.sm)

trace.settings.streaming = true
trace.settings.file_write_period_ms = 100
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.NS)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = args.trace

local function parse_int(s) return tonumber(s) end

local function split_tab(line)
  local fields = {}
  local start = 1
  while true do
    local pos = string.find(line, "\t", start, true)
    if not pos then
      table.insert(fields, string.sub(line, start))
      break
    end
    table.insert(fields, string.sub(line, start, pos - 1))
    start = pos + 1
  end
  return fields
end

local function load_manifest(dir)
  local manifest = {}
  local f = assert(io.open(dir .. "/manifest.txt", "r"))
  for line in f:lines() do
    if line ~= "" then
      local key, value = line:match("^([^=]+)=(.+)$")
      if key then manifest[key] = parse_int(value) end
    end
  end
  f:close()
  return manifest
end

local function load_events(dir)
  local events = {}
  local f = assert(io.open(dir .. "/events.tsv", "r"))
  assert(
    f:read("*l") == "seq\ttype\taddr\tsize\talloc_tag\tvalue\tstatus\tblob",
    "unsupported replay event schema; recapture with the current HAL"
  )
  for line in f:lines() do
    if line ~= "" then
      local fields = split_tab(line)
      assert(#fields == 8, "bad replay event line: " .. line)
      table.insert(events, {
        seq = parse_int(fields[1]),
        type = fields[2],
        addr = parse_int(fields[3]),
        size = parse_int(fields[4]),
        alloc_tag = parse_int(fields[5]),
        value = parse_int(fields[6]),
        status = parse_int(fields[7]),
        blob = fields[8],
      })
    end
  end
  f:close()
  return events
end

local function read_blob(dir, name)
  local f = assert(io.open(dir .. "/" .. name, "rb"))
  local s = f:read("*a")
  f:close()
  local bytes = {}
  for i = 1, #s do
    bytes[i] = string.byte(s, i)
  end
  return bytes
end

local function zero_bytes(n)
  local bytes = {}
  for i = 1, n do
    bytes[i] = 0
  end
  return bytes
end

local function u32_le(value)
  local bytes = {}
  for i = 1, 4 do
    bytes[i] = value % 256
    value = math.floor(value / 256)
  end
  return bytes
end

local function u64_le(value)
  local bytes = {}
  for i = 1, 8 do
    bytes[i] = value % 256
    value = math.floor(value / 256)
  end
  return bytes
end

local function write_u64_le(bytes, start, value)
  for i = 0, 7 do
    bytes[start + i] = value % 256
    value = math.floor(value / 256)
  end
end

-- Firmware treats a zero host address as an invalid/null pointer.  Keep the
-- replay host window at a non-zero offset while still using a small local
-- Memory target behind the DMA host port.
local replay_host_address = 0x100
local replay_host_next_address = replay_host_address
local pending_memory_copy_h2d = {}
local pending_memory_copy_d2h = {}
local memory_copy_sizes_by_slot_addr = {}

local function align_up(value, alignment)
  return math.floor((value + alignment - 1) / alignment) * alignment
end

local function allocate_replay_host(size)
  local address = replay_host_next_address
  replay_host_next_address = align_up(address + math.max(size, 1), 64)
  return address
end

local function read_u64_le(bytes, start)
  local value = 0
  for i = start + 7, start, -1 do
    value = value * 256 + (bytes[i] or 0)
  end
  return value
end

local function packet_header(bytes)
  if #bytes < 2 then return nil end
  return bytes[1] + bytes[2] * 256
end

-- Multi-MiB firmware-managed copies need more than a fixed poll budget.
local function memory_copy_wait_budget(size)
  local floor = args.max_wait_cycles
  size = math.max(0, size or 0)
  return floor + size
end

local function patch_memory_copy_packet_host_address(bytes)
  -- MemoryCopyPacket layout: src_domain @ byte 3, dst_domain @ byte 4,
  -- src_addr @ byte 9, dst_addr @ byte 17, size @ byte 25, and the common
  -- Completion Token @ byte 57 (all indexes are 1-based).
  if packet_header(bytes) ~= 6 or #bytes < 64 then return bytes end
  local src_domain = bytes[3]
  local dst_domain = bytes[4]
  local size = read_u64_le(bytes, 25)
  local slot_index = bytes[57] + bytes[58] * 256
  assert(slot_index < config.completion_pool_entries, "invalid completion slot")
  local slot_addr = config.completion_pool_base + slot_index * 8
  assert(
    memory_copy_sizes_by_slot_addr[slot_addr] == nil,
    "completion slot reused before completion"
  )
  memory_copy_sizes_by_slot_addr[slot_addr] = size
  if src_domain == 1 then
    local pending = table.remove(pending_memory_copy_h2d, 1)
    assert(pending ~= nil, "missing captured H2D payload for memory-copy packet")
    assert(
      pending.size == size,
      string.format("H2D payload size mismatch: capture=%d packet=%d", pending.size, size)
    )
    write_u64_le(bytes, 9, pending.address)
  end
  if dst_domain == 1 then
    local host_address = allocate_replay_host(size)
    write_u64_le(bytes, 17, host_address)
    table.insert(pending_memory_copy_d2h, {
      address = host_address,
      size = size,
    })
  end
  return bytes
end

local function i64_le(value)
  if value >= 0 then return u64_le(value) end
  local bytes = u64_le(-value - 1)
  for i = 1, 8 do
    bytes[i] = 255 - bytes[i]
  end
  return bytes
end

local function bytes_to_u64(bytes)
  local value = 0
  for i = math.min(#bytes, 8), 1, -1 do
    value = value * 256 + bytes[i]
  end
  return value
end

local function same_bytes(lhs, rhs)
  if #lhs ~= #rhs then return false, 0 end
  for i = 1, #lhs do
    if lhs[i] ~= rhs[i] then return false, i end
  end
  return true, 0
end

local function mismatch_message(seq, pos, expected, actual)
  return string.format(
    "event %d readback mismatch at +0x%x: expected 0x%02x got 0x%02x",
    seq,
    pos - 1,
    expected,
    actual
  )
end

local function stderr_is_tty()
  local ok, is_tty = pcall(function()
    if not U.isatty then return false end
    return U.isatty(2)
  end)
  return ok and is_tty
end

local function format_bytes(n)
  if type(n) ~= "number" then return "0 B" end
  local units = { "B", "KiB", "MiB", "GiB" }
  local value = n
  local unit = 1
  while value >= 1024 and unit < #units do
    value = value / 1024
    unit = unit + 1
  end
  if unit == 1 then return string.format("%d %s", value, units[unit]) end
  return string.format("%.1f %s", value, units[unit])
end

local event_traits = {
  dma_h2d = { shows_size = true, transfer = true, legacy = true },
  dma_d2h = { shows_size = true, transfer = true, legacy = true },
  memory_copy_h2d = { shows_size = true, transfer = true, host_payload = true },
  memory_copy_d2h = { shows_size = true, transfer = true, host_payload = true },
  boot_descriptor = { host_payload = true },
  scratchpad_write = { shows_size = true },
}

local function event_detail(event)
  local traits = event_traits[event.type]
  if traits and traits.shows_size then
    return string.format(" size=%s", format_bytes(event.size))
  end
  if event.type == "completion_slot" then return string.format(" addr=0x%x", event.addr) end
  return ""
end

local progress_tty = stderr_is_tty()
local progress_enabled = not args.no_progress
local progress_interval = math.max(args.progress_interval, 0.05)
local progress_last_update = -math.huge
local progress_line_open = false
local progress_poll_stride = 1024

local colors = {
  reset = string.char(27) .. "[0m",
  dim = string.char(27) .. "[2m",
  green = string.char(27) .. "[32m",
  cyan = string.char(27) .. "[36m",
  yellow = string.char(27) .. "[33m",
  magenta = string.char(27) .. "[35m",
  clear_line = string.char(27) .. "[K",
}

local function c(color, text) return progress_tty and (color .. text .. colors.reset) or text end

local function progress_due(force)
  if not progress_enabled then return end
  local now = os.clock()
  if not force and progress_interval > 0 and now - progress_last_update < progress_interval then
    return
  end
  progress_last_update = now
  return true
end

local function progress_write(message)
  if progress_tty then
    io.stderr:write("\r" .. message .. colors.clear_line)
    io.stderr:flush()
    progress_line_open = true
  else
    io.stderr:write(message .. "\n")
  end
end

local function progress_newline()
  if progress_enabled and progress_tty and progress_line_open then
    io.stderr:write("\n")
    io.stderr:flush()
    progress_line_open = false
  end
end

local function event_color(event_type)
  if event_type == "completion_slot" then return colors.yellow end
  local traits = event_traits[event_type]
  if traits and traits.transfer then return colors.magenta end
  return colors.cyan
end

local events

local function progress_event(index, event, detail, force)
  if not progress_due(force) then return end
  local pct = #events > 0 and (index * 100 / #events) or 100
  progress_write(
    string.format(
      "%s event %s seq=%s type=%s%s%s",
      c(colors.green, string.format("[%6.2f%%]", pct)),
      c(colors.cyan, string.format("%d/%d", index, #events)),
      c(colors.dim, tostring(event.seq)),
      c(event_color(event.type), event.type),
      event_detail(event),
      detail and (" " .. detail) or ""
    )
  )
end

local manifest = load_manifest(args.replay_dir)
events = load_events(args.replay_dir)

assert(manifest.version == 2, "unsupported replay manifest version; recapture with the current HAL")
assert(manifest.global_mem_base == config.global_mem_base, "global_mem_base mismatch")
assert(
  manifest.global_mem_alloc_base == config.global_mem_alloc_base,
  "global_mem_alloc_base mismatch"
)
assert(manifest.global_mem_size == config.global_mem_size, "global_mem_size mismatch")
assert(manifest.fsa_mmio_base == config.fsa_mmio_base, "fsa_mmio_base mismatch")
assert(manifest.cache_block_size == config.cache_block_size, "cache_block_size mismatch")

local max_payload_size = 8
local replay_host_payload_bytes = 0
for _, event in ipairs(events) do
  local traits = event_traits[event.type]
  if traits and traits.legacy then
    error("legacy direct-DMA capture is unsupported; recapture with firmware-managed copies")
  end
  if traits and traits.host_payload then
    if event.size > max_payload_size then max_payload_size = event.size end
    replay_host_payload_bytes = replay_host_payload_bytes + align_up(event.size, 64)
  end
end

local system = System("System", make_agent_socket_path(), config, make_sm, {
  replay = true,
  replay_host_mem_size = replay_host_address + replay_host_payload_bytes + max_payload_size + 64,
})

local replay = assert(system._replay_initiator, "missing replay initiator")
local replay_host_mem = assert(system._replay_host_mem, "missing replay host memory")

local function wait_completed(before, label)
  local waited = 0
  while replay:completed_count() <= before do
    system:start(1)
    waited = waited + 1
    if waited > args.max_wait_cycles then
      error("timeout waiting for replay transaction: " .. label)
    end
  end
end

local function write_bytes(addr, bytes, label)
  local before = replay:completed_count()
  replay:add_payload({ addr = addr, data = bytes })
  wait_completed(before, label)
end

local function read_bytes(addr, size, label)
  local before = replay:completed_count()
  replay:add_payload({ addr = addr, size = size })
  wait_completed(before, label)
  local data = replay:get_read_data()
  assert(data ~= nil, "missing read data for " .. label)
  return data
end

local function write64(addr, value, label) write_bytes(addr, i64_le(value), label) end

local function read64(addr, label) return bytes_to_u64(read_bytes(addr, 8, label)) end

local function read_completion_slot(addr, label)
  local bytes = read_bytes(addr, 8, label)
  local result = bytes[1] + bytes[2] * 256
  local alloc_tag = 0
  for i = 8, 3, -1 do
    alloc_tag = alloc_tag * 256 + bytes[i]
  end
  return alloc_tag, result
end

local fsa_mmio_base = config.fsa_mmio_base
-- CP CSR offsets relative to fsa_mmio_base (see formosa_addr_map.h FSA_CP_OFF_*).
local CP_OFF_FW_HOST_ADDR = 0x108
local CP_OFF_FW_SIZE = 0x110
local CP_OFF_FW_STATUS = 0x140
local CP_OFF_FW_FAULT = 0x158
local FW_STATUS_RESET = 0
local FW_STATUS_READY = 2
local FW_STATUS_FAULT = 3
local cp_reset_addr = config.clint_base
local strict_mismatch_count = 0
local max_unstrict_mismatch_logs = 10

local function memory_copy_host_transfer(blob, host_to_dev, event)
  if host_to_dev then
    local address = allocate_replay_host(#blob)
    replay_host_mem:write_bytes(address, blob)
    table.insert(pending_memory_copy_h2d, {
      address = address,
      size = #blob,
    })
    return
  end

  -- fsa_poll_completion() records the D2H payload immediately before it
  -- records completion_slot.  Keep the expected bytes until the
  -- completion event is replayed; reading here would race the firmware DMA.
  local pending = pending_memory_copy_d2h[1]
  assert(pending ~= nil, "missing memory-copy D2H packet before payload")
  assert(
    pending.size == #blob,
    string.format("D2H payload size mismatch: packet=%d capture=%d", pending.size, #blob)
  )
  pending.blob = blob
  pending.event = event
end

local function validate_memory_copy_d2h(event, result)
  if #pending_memory_copy_d2h == 0 then return end
  local pending = table.remove(pending_memory_copy_d2h, 1)
  if result ~= 1 then return end
  assert(pending.blob ~= nil, "missing captured D2H payload at completion")

  local blob = pending.blob
  local actual = replay_host_mem:read_bytes(pending.address, #blob)
  local ok, pos = same_bytes(blob, actual)
  if ok then return end
  local message = mismatch_message(pending.event.seq, pos, blob[pos], actual[pos])
  strict_mismatch_count = strict_mismatch_count + 1
  if args.check == "strict" then
    error(
      message
        .. "\nstrict_mismatch: replayed firmware-managed device-to-host bytes differ from capture."
        .. "\nHint: use --check unstrict for perf-only replay of workloads whose"
        .. " legal outputs may vary across scheduler/configuration changes."
    )
  elseif strict_mismatch_count <= max_unstrict_mismatch_logs then
    progress_newline()
    io.stderr:write("unstrict: ignoring " .. message .. "\n")
  end
end

local function wait_completion_slot(event_index, event)
  local size = memory_copy_sizes_by_slot_addr[event.addr]
  local max_polls = size and memory_copy_wait_budget(size)
  local polls = 0
  local alloc_tag, result = read_completion_slot(event.addr, "completion_slot " .. event.seq)
  while result == 0 do
    polls = polls + 1
    if polls % progress_poll_stride == 0 then
      progress_event(event_index, event, "polls=" .. polls, false)
    end
    if max_polls and polls > max_polls then
      error(
        string.format(
          "timeout waiting for completion_slot %d polls=%d budget=%d",
          event.seq,
          polls,
          max_polls
        )
      )
    end
    alloc_tag, result = read_completion_slot(event.addr, "completion_slot " .. event.seq)
  end
  if alloc_tag ~= event.alloc_tag then
    error(
      string.format(
        "completion_slot %d alloc-tag mismatch: expected %d got %d",
        event.seq,
        event.alloc_tag,
        alloc_tag
      )
    )
  end
  if result ~= event.value then
    error(
      string.format(
        "completion_slot %d result mismatch: expected %d got %d",
        event.seq,
        event.value,
        result
      )
    )
  end
  if size then
    memory_copy_sizes_by_slot_addr[event.addr] = nil
    validate_memory_copy_d2h(event, result)
  end
end

for event_index, event in ipairs(events) do
  progress_event(event_index, event, nil, event_index == 1)
  if
    event.type == "hal_init"
    or event.type == "malloc"
    or event.type == "addr_malloc"
    or event.type == "malloc_noncache"
    or event.type == "free"
    or event.type == "mmio_read"
    or event.type == "cmd_packet"
  then
    -- These are observations made by the HAL after the command/completion
    -- writes have already been replayed.  They do not mutate simulator state.
  elseif event.type == "mmio_write" then
    -- Boot Descriptor host pointer/size are applied from boot_descriptor with
    -- a replay-local Host address; skip the captured absolute pointer values.
    if event.addr == CP_OFF_FW_HOST_ADDR or event.addr == CP_OFF_FW_SIZE then
      -- deferred to boot_descriptor
    else
      write64(fsa_mmio_base + event.addr, event.value, "mmio_write " .. event.seq)
    end
  elseif event.type == "cp_reset" then
    write_bytes(cp_reset_addr, u32_le(1), "cp_reset")
    -- Cooperative reboot: wait for ROM Reset before the next Boot Descriptor.
    local polls = 0
    while read64(fsa_mmio_base + CP_OFF_FW_STATUS, "fw status after reboot") ~= FW_STATUS_RESET do
      polls = polls + 1
      if polls > args.max_wait_cycles then
        error("timeout waiting for Firmware Reboot Reset " .. event.seq)
      end
    end
  elseif event.type == "boot_descriptor" then
    local blob = read_blob(args.replay_dir, event.blob)
    local address = allocate_replay_host(#blob)
    replay_host_mem:write_bytes(address, blob)
    write64(fsa_mmio_base + CP_OFF_FW_HOST_ADDR, address, "boot_descriptor host addr")
    write64(fsa_mmio_base + CP_OFF_FW_SIZE, #blob, "boot_descriptor size")
    -- Wait for ROM Host-DMA to finish (FW_SIZE cleared) and firmware READY.
    local polls = 0
    while true do
      local remaining = read64(fsa_mmio_base + CP_OFF_FW_SIZE, "boot_descriptor size poll")
      local status = read64(fsa_mmio_base + CP_OFF_FW_STATUS, "boot_descriptor status")
      if remaining == 0 and status == FW_STATUS_READY then break end
      if status == FW_STATUS_FAULT then
        local fault = read64(fsa_mmio_base + CP_OFF_FW_FAULT, "boot fault")
        error(string.format("boot descriptor %d faulted with code %d", event.seq, fault))
      end
      polls = polls + 1
      if polls > args.max_wait_cycles then
        error("timeout waiting for boot descriptor " .. event.seq)
      end
    end
  elseif event.type == "scratchpad_write" then
    local payload = patch_memory_copy_packet_host_address(read_blob(args.replay_dir, event.blob))
    write_bytes(event.addr, payload, "scratchpad_write " .. event.seq)
  elseif event.type == "completion_pool_write" then
    -- Pending 8-byte Completion Slot word; firmware updates it on retire.
    local blob = read_blob(args.replay_dir, event.blob)
    assert(#blob == 8, "completion_pool_write must be 8 bytes, got " .. #blob)
    write_bytes(event.addr, blob, "completion_pool_write " .. event.seq)
  elseif event.type == "completion_slot" then
    wait_completion_slot(event_index, event)
  elseif event.type == "memory_copy_h2d" then
    memory_copy_host_transfer(read_blob(args.replay_dir, event.blob), true, event)
  elseif event.type == "memory_copy_d2h" then
    memory_copy_host_transfer(read_blob(args.replay_dir, event.blob), false, event)
  elseif event.type == "memory_copy_completion" or event.type == "wait_completion" then
    error(
      "legacy completion event "
        .. event.type
        .. " at seq "
        .. event.seq
        .. "; re-capture with ABI v3 completion_slot"
    )
  else
    error("unknown replay event " .. event.seq .. " type " .. event.type)
  end
end

if args.drain > 0 then
  if progress_due(true) then
    progress_write(string.format("draining %d cycles after replay events", args.drain))
  end
  system:start(args.drain)
end
assert(#pending_memory_copy_d2h == 0, "unmatched memory-copy D2H capture event")
assert(#pending_memory_copy_h2d == 0, "unmatched memory-copy H2D capture event")
assert(next(memory_copy_sizes_by_slot_addr) == nil, "unmatched memory-copy size for completion")
sc.stop()

progress_event(#events, events[#events] or { seq = 0, type = "done" }, "done", true)
progress_newline()

if args.stats and system.stats and system.stats.dump_toml then
  local stat_file = assert(io.open(args.stats, "w"), "Cannot open " .. args.stats)
  stat_file:write(system.stats:dump_toml())
  stat_file:close()
end

if args.check == "unstrict" and strict_mismatch_count > max_unstrict_mismatch_logs then
  io.stderr:write(
    string.format("unstrict: ignored %d device-to-host mismatches total\n", strict_mismatch_count)
  )
end

print(string.format("Replay passed: %d events", #events))
