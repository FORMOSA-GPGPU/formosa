-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local plic = simple.Plic("plic", { num_sources = 65, num_contexts = 2 })
plic.clock = clock
local initial_plic = simple.Plic("initial_plic", { num_sources = 1, num_contexts = 1 })
initial_plic.clock = clock
local initial_source = sc.signal("initial_source", true)
initial_plic.sources[1](initial_source)
local initial_irq = sc.signal("initial_irq_terminal", true)
initial_plic.irq[1](initial_irq)
local initial_host = simple.Initiator("initial_host")
initial_host.clock = clock
initial_host.target = initial_plic.port
local bus = simple.XBar("bus", 4, { { addr = 0, size = 0x4000000 } })
bus.clock = clock
bus.mem_side[1].target = plic.port

local host = simple.Initiator("host")
host.clock = clock
host.target = bus.core_side[1].port

local claimant_a = simple.OutstandingInitiator("a")
local claimant_b = simple.OutstandingInitiator("b")
claimant_a.target = bus.core_side[2].port
claimant_b.target = bus.core_side[3].port

local debugger = dbg.MemoryDebugger("debugger")
debugger.target = bus.core_side[4].port

-- Keep sources around bitmap word boundaries, including the partial last word.
local source_ids = { 1, 2, 3, 31, 32, 33, 65 }
local sources = {}
for _, id in ipairs(source_ids) do
  sources[id] = sc.signal("source_" .. id, false)
end
local source_ports, irq_ports = plic.sources, plic.irq
assert(#source_ports == 65 and #irq_ports == 2, "unexpected PLIC port count")
local low = sc.signal("unused_sources_low", false)
for id = 1, 65 do
  source_ports[id](sources[id] or low)
end

-- Start channels high so initialize(false) must establish the inactive level.
local irqs = {}
for context = 1, 2 do
  irqs[context] = sc.signal("irq_" .. context, true)
  irq_ports[context](irqs[context])
  assert(irqs[context]:read())
end
assert(initial_irq:read())

-- LV_FATAL logs the diagnostic before throwing; pcall only verifies rejection.
local function rejects(fn, case)
  local ok = pcall(fn)
  assert(not ok, case .. " must be rejected")
end
rejects(function() plic.clock = nil end, "null clock")
rejects(function() plic.clock = clock end, "duplicate clock binding")
for i, param in ipairs({
  { num_sources = 0 },
  { num_sources = 1024 },
  { num_contexts = 0 },
  { num_contexts = 15873 },
}) do
  rejects(function() simple.Plic("invalid_" .. i, param) end, "invalid parameter case " .. i)
end

local function encode_word(value)
  return {
    value % 256,
    math.floor(value / 256) % 256,
    math.floor(value / 65536) % 256,
    math.floor(value / 16777216) % 256,
  }
end
local function decode_word(data)
  assert(data and #data == 4, "missing 32-bit response")
  return data[1] + data[2] * 256 + data[3] * 65536 + data[4] * 16777216
end

local function wait_count(initiator, count)
  for _ = 1, 1024 do
    if initiator:completed_count() == count then return end
    sc.start(period)
  end
  error("PLIC transaction timeout")
end
local function transact(payload)
  local count = host:completed_count() + 1
  host:add_payload(payload)
  wait_count(host, count)
end
local function write(address, value) transact({ addr = address, data = encode_word(value) }) end
local function read(address)
  transact({ addr = address, size = 4 })
  return decode_word(host:get_read_data())
end

local function enable(context, word_index, value)
  write(0x2000 + context * 0x80 + word_index * 4, value)
end
local function threshold(context, value) write(0x200000 + context * 0x1000, value) end
local function claim(context) return read(0x200004 + context * 0x1000) end
local function complete(context, id) write(0x200004 + context * 0x1000, id) end
local function irq(context, expected)
  assert(irqs[context + 1]:read() == expected, "unexpected IRQ on context " .. context)
end
local function pending(id)
  return bit.band(read(0x1000 + math.floor(id / 32) * 4), bit.lshift(1, id % 32)) ~= 0
end

sc.start(sc.ZERO_TIME)
irq(0, false)
irq(1, false)
assert(not initial_irq:read(), "unused output did not initialize its terminal low")
-- An initially asserted source has no rising event to wake the gateway.
initial_host:add_payload({ addr = 0x1000, size = 4 })
wait_count(initial_host, 1)
assert(decode_word(initial_host:get_read_data()) == 2, "initial high source was not accepted")
initial_source:write(false)
assert(read(4) == 0 and read(0x2000) == 0 and read(0x200000) == 0)
assert(read(0x1000) == 0 and claim(0) == 0)

-- WARL and fixed bitmap layout, including reserved/unimplemented sources.
write(4, 0xffffffff)
assert(read(4) == 7)
threshold(0, 0xffffffff)
assert(read(0x200000) == 7)
threshold(0, 0)
write(0, 7)
write(66 * 4, 7)
assert(read(0) == 0 and read(66 * 4) == 0)
enable(0, 0, 0xffffffff)
enable(0, 1, 0xffffffff)
enable(0, 2, 0xffffffff)
enable(0, 31, 0xffffffff)
assert(read(0x2000) == 0xfffffffe)
assert(read(0x2004) == 0xffffffff and read(0x2008) == 3)
assert(read(0x207c) == 0 and read(0x107c) == 0)
write(0x1000, 0xffffffff)
assert(read(0x1000) == 0, "pending must be read-only")

-- Source state is event-driven: a pulse wholly between positive clock edges
-- must be accepted, and falling low cannot retract the pending request.
sc.start(period / 2)
sources[1]:write(true)
sc.start(sc.time(1, sc.time_unit.NS))
assert(irqs[1]:read(), "source assertion waited for a clock edge")
sources[1]:write(false)
sc.start(sc.time(1, sc.time_unit.NS))
assert(irqs[1]:read(), "source deassertion retracted a pending request")
assert(pending(1) and claim(0) == 1)
complete(0, 1)
irq(0, false)

-- Held levels exercise priority ties and keep gateways busy after each claim.
write(4, 1)
write(8, 3)
write(12, 3)
sources[1]:write(true)
sources[2]:write(true)
sources[3]:write(true)
sc.start(2 * period)
assert(sources[1]:read() and not sources[65]:read())
irq(0, true)
irq(1, false)
assert(claim(0) == 2, "highest priority, then lowest ID must win")
assert(claim(0) == 3 and claim(0) == 1, "claim reopened a gateway before completion")
assert(claim(0) == 0, "claim must leave the gateway busy")
irq(0, false)

for _, id in ipairs({ 1, 2, 3 }) do
  sources[id]:write(false)
  complete(0, id)
end

-- Equal threshold suppresses notification, but must not suppress claim.
threshold(0, 3)
sources[2]:write(true)
sc.start(2 * period)
assert(pending(2))
irq(0, false)
threshold(0, 4)
irq(0, false)
threshold(0, 2)
irq(0, true)
threshold(0, 3)
irq(0, false)
assert(claim(0) == 2, "threshold must not gate claim")
sources[2]:write(false)
complete(0, 2)
threshold(0, 0)

-- Deassertion cannot retract an accepted request.
sources[1]:write(true)
sc.start(2 * period)
sources[1]:write(false)
sc.start(2 * period)
assert(pending(1) and claim(0) == 1)
complete(0, 1)
assert(claim(0) == 0)

-- Pending sources survive priority/enable masking and notify on unmask.
write(4, 0)
sources[1]:write(true)
sc.start(2 * period)
assert(pending(1))
irq(0, false)
assert(claim(0) == 0)
write(4, 2)
irq(0, true)
enable(0, 0, 0xfffffffc)
irq(0, false)
assert(claim(0) == 0 and pending(1))
enable(0, 0, 0xfffffffe)
irq(0, true)

-- Debug reads/writes must neither consume pending nor complete a source.
debugger:read_bytes(0x200004, 4)
assert(debugger:write_bytes(0x200004, encode_word(1)) == 0)
assert(pending(1) and claim(0) == 1)
debugger:read_bytes(0x200004, 4)
assert(debugger:write_bytes(0x200004, encode_word(1)) == 0)
sc.start(3 * period)
assert(not pending(1) and claim(0) == 0, "gateway reopened before completion")
complete(0, 0)
complete(0, 66)
complete(0, 0xffffffff)
assert(claim(0) == 0, "invalid completion must be ignored")

-- Completion is ignored if disabled, even if that context claimed the source.
enable(0, 0, 0xfffffffc)
complete(0, 1)
enable(0, 0, 0xfffffffe)
assert(claim(0) == 0)
complete(1, 1)
assert(claim(0) == 0, "disabled context completed a source")
enable(1, 0, 2)
complete(1, 1) -- An enabled context may complete another context's claim.
sc.start(2 * period)
assert(pending(1), "held level must repend after completion")
irq(0, true)
irq(1, true)
sources[1]:write(false)
assert(claim(1) == 1 and claim(0) == 0)
complete(1, 1)
enable(1, 0, 0)

-- Malformed accesses return an error and must not claim/complete anything.
sources[2]:write(true)
sc.start(2 * period)
for _, payload in ipairs({
  { addr = 0x200005, size = 4 },
  { addr = 0x200004, size = 8 },
  { addr = 0x1080, size = 4 },
  { addr = 0x2100, size = 4 }, -- unimplemented enable context 2
  { addr = 0x202004, size = 4 },
  { addr = 0x200008, size = 4 },
  { addr = 0x3fffffc, size = 4 },
}) do
  transact(payload)
  assert(host:get_read_data() == nil, "invalid read returned a successful response")
  assert(pending(2), "invalid read consumed pending")
end

assert(claim(0) == 2)
transact({ addr = 0x200005, data = encode_word(2) })
transact({ addr = 0x200004, data = { 2, 0 } })
assert(claim(0) == 0, "invalid write completed a source")
sources[2]:write(false)
complete(0, 2)

-- Two contexts compete through independent initiators for a single request.
enable(1, 0, 4)
sources[2]:write(true)
sc.start(2 * period)
irq(0, true)
irq(1, true)

-- Register changes must refresh IRQs even without a new source transition.
threshold(1, 3)
irq(0, true)
irq(1, false)
write(8, 0)
irq(0, false)
irq(1, false)
write(8, 3)
irq(0, true)
irq(1, false)
threshold(1, 0)
irq(0, true)
irq(1, true)

local a_completions, b_completions =
  claimant_a:completed_count() + 1, claimant_b:completed_count() + 1
claimant_a:add_payload({ addr = 0x200004, size = 4 })
claimant_b:add_payload({ addr = 0x201004, size = 4 })
wait_count(claimant_a, a_completions)
wait_count(claimant_b, b_completions)
local a_claim, b_claim =
  decode_word(claimant_a:get_read_data()), decode_word(claimant_b:get_read_data())
assert(
  (a_claim == 2 and b_claim == 0) or (a_claim == 0 and b_claim == 2),
  "claim was not atomic across contexts"
)
-- Claim clears shared pending, including the losing context's notification.
irq(0, false)
irq(1, false)

sources[2]:write(false)
complete(0, 2)
enable(1, 0, 0)

-- Queue more requests than the sink FIFO holds; each source is consumed once.
for _, id in ipairs(source_ids) do
  write(id * 4, 1)
  sources[id]:write(true)
end
sc.start(2 * period)
assert(pending(31) and pending(32) and pending(33) and pending(65))

a_completions = claimant_a:completed_count() + 96
for _ = 1, 96 do
  claimant_a:add_payload({ addr = 0x200004, size = 4 })
end
wait_count(claimant_a, a_completions)

for i = 1, 96 do
  assert(
    decode_word(claimant_a:get_read_data()) == (source_ids[i] or 0),
    "repeated or misordered claim in request burst"
  )
end
assert(claimant_a:get_read_data() == nil)
irq(0, false)
for _, id in ipairs(source_ids) do
  sources[id]:write(false)
  complete(0, id)
end
assert(read(0x1000) == 0 and read(0x1004) == 0 and read(0x1008) == 0)
print("PLIC register, gateway, context, and transaction tests passed")
