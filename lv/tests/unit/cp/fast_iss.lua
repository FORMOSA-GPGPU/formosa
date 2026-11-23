-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- cp.FastISS runs a known program from private code and data memories and
-- marks its progress in a shared one. Each case is one process, selected by
-- flags such as --q 4 or --irq (see lv/tests/xmake.lua). Exact timestamps of
-- the shared markers are the timing oracle; the memories' own transaction and
-- DMI-request counts, independent of the CPU, check the transport path.
--
-- The program stores 128 additions, reads them back, reads the shared memory,
-- and reads the data again after the test moves the data memory to new
-- storage with 7 at offset 0. Only a CPU that dropped its invalidated grant
-- sees 7 and writes 129 + 7 = 136 as its result.

local options = {
  q = 16,
  cost = 2500,
  fetch = 2500,
  read = 5000,
  write = 7500,
  boundary = -1,
}
do
  local args = { ... }
  local flags = { irq = true, ["no-dmi"] = true, excluded = true, ["read-only"] = true }
  flags.compressed, flags.ebreak = true, true
  local i = 1
  while i <= #args do
    local name = assert(args[i]:match("^%-%-(.+)$"), "unknown argument " .. args[i])
    if flags[name] then
      options[name:gsub("-", "_")] = true
    else
      assert(options[name] ~= nil, "unknown argument --" .. name)
      i = i + 1
      options[name] = assert(tonumber(args[i]), "--" .. name .. " expects a number")
    end
    i = i + 1
  end
end

local SHARED_LATENCY = 17500
local ADDITIONS = 128
-- After the start marker: 128 ADDI, SW, LW, external LW, LW, ADD, end SW.
local INSTRUCTIONS = ADDITIONS + 6
local QUANTUM = options.q * 2500
local STEP = options.cost + options.fetch

local function ps(value) return sc.time(value, sc.time_unit.PS) end
local function now() return sc.time_stamp() / ps(1) end
local function word(bytes)
  return bytes[1] + bytes[2] * 0x100 + bytes[3] * 0x10000 + bytes[4] * 0x1000000
end

-- Only the initial edge is needed. A long period lets delta counts witness CPU
-- scheduling without unrelated periodic clock events, and the 1 ns first edge
-- shows the CPU waits for it.
local clock = dbg.offset_clock("clock", sc.time(1, sc.time_unit.MS), ps(1000))
local gnd = sc.signal("gnd", false)
local timer = sc.signal("timer", false)
local software = sc.signal("software", false)
-- Pause at a marker, then drive events from Lua between sc.start calls.
-- Ordered insertion preserves arrival order for equal timestamps.
local pending = {}
local function after(delay, callback)
  local event = { at = now() + delay / ps(1), callback = callback }
  local i = #pending + 1
  while i > 1 and pending[i - 1].at > event.at do
    pending[i] = pending[i - 1]
    i = i - 1
  end
  pending[i] = event
  sc.pause()
end

local code = dbg.TestMemory("code", {
  size = 0x1000,
  read_latency = ps(options.fetch),
  write_latency = ps(options.fetch),
  dmi = options.no_dmi and "none" or "read_write",
})
local data = dbg.TestMemory("data", {
  size = 0x1000,
  read_latency = ps(options.read),
  write_latency = ps(options.write),
  dmi = options.no_dmi and "none" or options.read_only and "read_only" or "read_write",
})

-- What the shared memory saw, and the drive that reacts to it.
local seen = { interrupts = {} }
local function on_shared(write, addr, bytes, delta)
  local value = word(bytes)
  if not write then
    assert(addr == 8, "unexpected shared read")
    seen.external = now()
    -- Move the data memory before the CPU continues; it must reacquire.
    data:replace()
    data:write_bytes(0, { 7, 0, 0, 0 })
    if options.irq then
      after(ps(SHARED_LATENCY / 2), function()
        seen.software_injected = now()
        software:write(true)
      end)
    end
  elseif addr == 0 then
    assert(value == 1, "start marker value")
    seen.start, seen.start_delta = now(), delta
    if options.irq then
      after(ps(SHARED_LATENCY + QUANTUM + options.boundary), function()
        seen.timer_injected = now()
        timer:write(true)
      end)
    end
  elseif addr == 4 then
    seen.result, seen.finish, seen.finish_delta = value, now(), delta
    after(ps(SHARED_LATENCY + QUANTUM + 1000), function()
      seen.stopped = true
      sc.stop()
    end)
  else
    assert(addr == 16, "unexpected shared write")
    assert(value == 7 or value == 3, "unexpected trap cause")
    seen.interrupts[#seen.interrupts + 1] = { cause = value, time = now() }
    -- Lua is each signal's only driver. Lower the acknowledged line between
    -- sc.start calls; the next SystemC update publishes the value.
    local line = value == 7 and timer or software
    after(sc.ZERO_TIME, function() line:write(false) end)
  end
end
-- Deliberately grants DMI: the CPU policy must still refuse shared run-ahead.
local shared = dbg.TestMemory("shared", {
  size = 0x1000,
  read_latency = ps(SHARED_LATENCY),
  write_latency = ps(SHARED_LATENCY),
  on_access = on_shared,
})
shared:write_bytes(8, { 0x56, 0x04, 0, 0 })

local router = nic.Router("router", {
  { addr = 0x1000, size = 0x1000 },
  { addr = 0x2000, size = 0x1000 },
  { addr = 0x3000, size = 0x1000 },
})
router.to = code.port
router.to = data.port
router.to = shared.port

local cpu = cp.FastISS("cpu", 0, {
  instruction_cost = ps(options.cost),
  quantum = ps(QUANTUM),
  local_regions = {
    { addr = 0x1000, size = 0x1000 },
    -- Only the first data word is private when excluded.
    { addr = 0x2000, size = options.excluded and 4 or 0x1000 },
  },
})
cpu.clock = clock
cpu.target = router.from
cpu.ext_int(gnd)
cpu.sw_int(software)
cpu.timer_int(timer)

do
  local image, offset = {}, 0
  local function put(value, size)
    for i = 1, size do
      image[offset + i] = value % 256
      value = math.floor(value / 256)
    end
    offset = offset + size
  end
  local function emit(instruction) put(instruction, 4) end
  if options.irq then
    emit(0x00001fb7) -- lui t6,1
    emit(0x300f8f93) -- addi t6,t6,0x300 (handler at 0x1300)
    emit(0x305f9073) -- csrw mtvec,t6
    emit(0x08800f93) -- li t6,0x88 (MTIE and MSIE)
    emit(0x304f9073) -- csrw mie,t6
    emit(0x30046073) -- csrsi mstatus,8
  end
  emit(0x000032b7) -- lui t0,3 (shared markers)
  emit(0x00002337) -- lui t1,2 (private data)
  emit(0x00100393) -- li t2,1
  emit(0x0072a023) -- sw t2,0(t0): start marker
  if options.ebreak then emit(0x00100073) end -- non-debug firmware EBREAK
  for _ = 1, ADDITIONS do
    if options.compressed then
      put(0x0385, 2) -- c.addi t2,1
    else
      emit(0x00138393) -- addi t2,t2,1
    end
  end
  emit(0x00732023) -- sw t2,0(t1)
  -- The store already cached a full-page grant. In the excluded case, offset
  -- 4 is outside the private promise and must still use timed AT.
  emit(options.excluded and 0x00432e03 or 0x00032e03) -- lw t3,4/0(t1)
  emit(0x0082ae83) -- lw t4,8(t0): external AT
  emit(0x00032e03) -- lw t3,0(t1): must reacquire invalidated data grant
  emit(0x01c383b3) -- add t2,t2,t3
  emit(0x0072a223) -- sw t2,4(t0): end marker
  emit(0x0000006f) -- j .
  while offset < 0x300 do
    put(0, 1)
  end
  emit(0x34202ff3) -- csrr t6,mcause
  emit(0x01f2a823) -- sw t6,16(t0): independent handler-entry marker/ack
  emit(0x30200073) -- mret
  code:write_bytes(0, image)
end
cpu:set_pc(0x1000)

local deadline = now() + 10000000
while not seen.stopped and now() < deadline do
  local target = pending[1] and math.min(pending[1].at, deadline) or deadline
  sc.start(ps(math.max(0, target - now())))
  while pending[1] and pending[1].at <= now() do
    table.remove(pending, 1).callback()
  end
end
assert(seen.stopped, "FastISS timed out (AT handshake or trap return)")

assert(seen.result == 136, "result mismatch: stale DMI/trap return/ISA error")
assert(shared.dmi_requests == 0, "DMI capability incorrectly authorized shared access")
-- Read a counter's value from the group's dump.
local stats_dump = cpu.stats:dump_toml()
local function count(name)
  local section = ("cpu." .. name):gsub("([^%w])", "%%%1")
  local record =
    assert(stats_dump:match("%[" .. section .. "%]([^%[]*)"), "missing statistic " .. name)
  return assert(tonumber(record:match("\nval = ([^\n]+)")), "missing value " .. name)
end
-- Execution continues in a private loop after the finish marker. Its last
-- fetch can be waiting when the simulation stops, so require the known prefix
-- rather than equating partially completed fetch and issue counters.
local executed = INSTRUCTIONS + 4
assert(count("instructions") >= executed, "instruction count misses the program")
if options.no_dmi then
  -- Every denied query falls back to AT: each fetch asks again, and data
  -- takes the SW, two LWs and the reacquiring LW through transport.
  assert(
    count("dmi_fetches") == 0 and count("dmi_reads") == 0 and count("dmi_writes") == 0,
    "denied DMI still used a pointer"
  )
  assert(
    count("at_fetches") >= executed and count("at_reads") == 3 and count("at_writes") == 3,
    "AT fallback misclassified"
  )
  assert(
    code.transactions >= executed and code.dmi_requests >= executed,
    "fetch did not fall back after a denied DMI query"
  )
  assert(
    data.transactions == 3 and data.dmi_requests == 3,
    "data did not fall back after a denied DMI query"
  )
else
  -- A read-only grant still permits timed writes through the target. The
  -- independent transaction count proves that the CPU did not write via DMI.
  local data_transactions = (options.excluded and 1 or 0) + (options.read_only and 1 or 0)
  local data_requests = (options.read_only and not options.excluded) and 3 or 2
  assert(
    code.transactions == 0
      and data.transactions == data_transactions
      and code.dmi_requests == 1
      and data.dmi_requests == data_requests,
    "DMI permissions, transport path, or grant reacquisition mismatch"
  )
  assert(
    count("dmi_fetches") >= executed
      and count("dmi_reads") == (options.excluded and 1 or 2)
      and count("dmi_writes") == (options.read_only and 0 or 1)
      and count("at_fetches") == 0
      and count("at_reads") == (options.excluded and 2 or 1)
      and count("at_writes") == (options.irq and 4 or 2) + (options.read_only and 1 or 0),
    "initiator statistics misclassify fetch/data or DMI/AT accesses"
  )
end

if not options.irq and not options.no_dmi then
  -- Three setup instructions precede the first shared store. Checking its
  -- absolute timestamp catches a common early shift that two markers could
  -- cancel out in the total-duration equation.
  local start = 1000 + 4 * STEP
  assert(seen.start == start, "shared start marker timestamp mismatch (fetch/issue settlement)")
  -- The intermediate external read follows 128 ADDI, a local store and a
  -- local read. Its own fetch/issue costs must precede the shared effect.
  local ebreak = options.ebreak and 1 or 0
  local external = start
    + SHARED_LATENCY
    + (ADDITIONS + 3 + ebreak) * STEP
    + options.write
    + options.read
  assert(seen.external == external, "external read timestamp mismatch (local/AT settlement)")
  local elapsed = (INSTRUCTIONS + ebreak) * STEP
    + 2 * options.read
    + options.write
    + 2 * SHARED_LATENCY
  assert(
    seen.finish - seen.start == elapsed,
    "external timing equation mismatch (fixed/fetch/data/AT accounting)"
  )
  if options.q >= 16 and options.cost == 2500 and options.fetch == 2500 then
    assert(
      seen.finish_delta - seen.start_delta < INSTRUCTIONS,
      "local execution yielded per instruction instead of batching"
    )
  end
elseif options.irq then
  local interrupts = seen.interrupts
  assert(
    seen.timer_injected == seen.start + SHARED_LATENCY + QUANTUM + options.boundary
      and seen.software_injected == seen.external + SHARED_LATENCY / 2,
    "Lua drive missed the requested interrupt injection time"
  )
  assert(
    #interrupts == 2 and interrupts[1].cause == 7 and interrupts[2].cause == 3,
    "timer/software interrupt lost or repeated"
  )
  -- Frozen contract: one scalar boundary and two handler-prefix instructions.
  local boundary = STEP + math.max(options.read, options.write)
  local prefix = 2 * STEP
  assert(
    interrupts[1].time >= seen.timer_injected
      and interrupts[1].time <= seen.timer_injected + QUANTUM + boundary + prefix,
    "local interrupt early or beyond frozen Q + boundary + prefix allowance"
  )
  assert(
    interrupts[2].time >= seen.software_injected
      and interrupts[2].time
        <= seen.software_injected + SHARED_LATENCY + QUANTUM + boundary + prefix,
    "interrupt during AT lost, early, or beyond unfinished-AT allowance"
  )
end

print(
  string.format(
    "Q=%d elapsed=%d ps deltas=%d DMI=%d/%d IRQ=%d Pass!",
    options.q,
    seen.finish - seen.start,
    seen.finish_delta - seen.start_delta,
    code.dmi_requests,
    data.dmi_requests,
    #seen.interrupts
  )
)
