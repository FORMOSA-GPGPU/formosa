-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local memory = simple.Memory("memory", { size = 1024, latency = 2 })
local initiator = simple.Initiator("initiator")
memory.clock = clock
initiator.clock = clock
initiator.target = memory.port

local function request(payload)
  local target = initiator:completed_count() + 1
  initiator:add_payload(payload)
  for _ = 1, 100 do
    sc.start(period)
    if initiator:completed_count() == target then return end
  end
  error("memory request timed out")
end

local function value_text(group, path)
  local section = path:gsub("([^%w])", "%%%1")
  local record = group:dump_toml():match("%[" .. section .. "%]([^%[]*)")
  assert(record, "missing statistic " .. path)
  local value = assert(record:match("\nval = ([^\n]+)"), "missing value " .. path)
  return value
end

-- LV_FATAL writes the diagnostic to the log before raising the error.
local function expect_error(action, message)
  local ok, err = pcall(action)
  assert(not ok, tostring(err))
  assert(tostring(err):find(message or "std::exception", 1, true), tostring(err))
end

-- Explicit scalar lookup leaves normal class and method lookup untouched.
assert(type(stats.Group.get) == "function")
assert(stats.Group.missing == nil)

local summary = stats.Group("summary")
assert(type(summary.reset) == "function")
summary:add_sub_group(memory.stats)
local reads = memory.stats:get("total_reads")
local writes = memory.stats:get("total_writes")
assert(reads ~= nil and writes ~= nil, "statistics getters must return live expressions")
local requests =
  summary:add_integer_formula("requests", "Requests across the source", reads + writes)
summary:add_real_formula("read_percent", "Percentage of requests that read", 100 * reads / requests)

assert(value_text(summary, "summary.requests") == "0")
assert(value_text(summary, "summary.read_percent"):match("nan$"))
for i = 1, 3 do
  request({ addr = i * 4, data = { i, 0, 0, 0 } })
end
request({ addr = 4, size = 4 })
assert(initiator:get_read_data()[1] == 1)
assert(value_text(summary, "summary.requests") == "4")
assert(tonumber(value_text(summary, "summary.read_percent")) == 25)

-- A formula's registration type selects its own dump's arithmetic. References
-- use the consumer's arithmetic, including through a formula of another type.
-- Keep the denominator nonzero when the source counters are reset below.
local integer_ratio =
  summary:add_integer_formula("integer_ratio", "Integer division", reads / (writes + 1))
local real_ratio = summary:add_real_formula("real_ratio", "Real division", integer_ratio)
summary:add_integer_formula("integer_from_real", "Integer consumer", real_ratio)
summary:add_real_formula("real_from_lookup", "Real consumer", summary:get("integer_ratio"))
assert(value_text(summary, "summary.integer_ratio") == "0")
assert(tonumber(value_text(summary, "summary.real_ratio")) == 0.25)
assert(value_text(summary, "summary.integer_from_real") == "0")
assert(tonumber(value_text(summary, "summary.real_from_lookup")) == 0.25)

-- Exercise expression/expression and both number/expression operand orders.
local cases = {
  { "native_requests", memory.stats:get("total_requests"), 4 },
  { "add", reads + writes, 4 },
  { "subtract", writes - reads, 2 },
  { "multiply", writes * reads, 3 },
  { "divide", reads / requests, 0.25 },
  { "negate", -writes, -3 },
  { "add_right", reads + 0.5, 1.5 },
  { "add_left", 0.5 + reads, 1.5 },
  { "subtract_right", reads - 0.5, 0.5 },
  { "subtract_left", 0.5 - reads, -0.5 },
  { "multiply_right", writes * 0.5, 1.5 },
  { "multiply_left", 0.5 * writes, 1.5 },
  { "divide_right", writes / 0.5, 6 },
  { "divide_left", 0.5 / requests, 0.125 },
  { "integer_right", writes + 2, 5 },
  { "integer_left", 2 - writes, -1 },
}
for _, case in ipairs(cases) do
  summary:add_real_formula(case[1], "Arithmetic regression", case[2])
  assert(tonumber(value_text(summary, "summary." .. case[1])) == case[3], case[1])
end

-- Values above LuaJIT's exact-number range must stay int64 inside the AST,
-- including when another formula references the result.
local wide = summary:add_integer_formula("wide", "Exact int64 sum", requests * 2251799813685248 + 1)
summary:add_integer_formula(
  "low_bit",
  "Preserve the low bit through a formula reference",
  wide - requests * 2251799813685248
)
collectgarbage("collect")
assert(value_text(summary, "summary.wide") == "9007199254740993")
assert(value_text(summary, "summary.low_bit") == "1")
assert(summary:dump_toml():find("formula =", 1, true))
assert(tostring(reads):find("memory.total_reads", 1, true))

request({ addr = 8, size = 4 })
assert(initiator:get_read_data()[1] == 2)
assert(value_text(summary, "summary.requests") == "5", "formula captured a snapshot")
assert(tonumber(value_text(summary, "summary.read_percent")) == 40)
summary:reset()
assert(value_text(summary, "summary.requests") == "0")
assert(value_text(summary, "summary.read_percent"):match("nan$"))
request({ addr = 4, size = 4 })
assert(initiator:get_read_data()[1] == 1)
assert(value_text(summary, "summary.requests") == "1")
assert(tonumber(value_text(summary, "summary.read_percent")) == 100)

expect_error(function() return summary:get("missing") end)
local missing_name = "missing_dynamic"
expect_error(function() return summary:get(missing_name) end)
expect_error(function() return summary:get("memory") end)
expect_error(function() summary.requests = 42 end, "cannot set (new_index)")
expect_error(function() summary:add_real_formula("requests", "duplicate", reads) end)
expect_error(function() summary:add_real_formula("memory", "child collision", reads) end)
expect_error(function() summary:add_real_formula("", "empty name", reads) end)

-- Statistic names no longer share the method namespace.
for _, name in ipairs({
  "reset",
  "dump_toml",
  "get",
  "add_sub_group",
  "add_real_formula",
  "add_integer_formula",
}) do
  summary:add_integer_formula(name, "Method-name statistic", reads)
  assert(type(summary[name]) == "function")
  local alias = stats.Group("alias")
  alias:add_integer_formula("count", "Alias", summary:get(name))
  assert(value_text(alias, "alias.count") == "1")
end
local named_child = stats.Group("__index")
summary:add_sub_group(named_child)

-- Registration rejects collisions in either order and between child groups.
local formula_collision = stats.Group("requests")
expect_error(function() summary:add_sub_group(formula_collision) end)
local child_collision = stats.Group("memory")
expect_error(function() summary:add_sub_group(child_collision) end)
expect_error(function() stats.Group("") end)
assert(value_text(summary, "summary.requests") == "1", "rejected registration changed the group")

-- Expressions and groups retain statistics data, independently of source
-- Lua userdata. No module or group needs to be retained by expression nodes.
local retained, looked_up, retained_group
do
  local temporary = stats.Group("temporary")
  retained = temporary:add_integer_formula("count", "Temporary source", reads + 1)
  looked_up = temporary:get("count")
  retained_group = stats.Group("retained_group")
  retained_group:add_sub_group(temporary)
end
collectgarbage("collect")
for _, reference in ipairs({ retained, looked_up }) do
  local consumer = stats.Group("consumer")
  consumer:add_integer_formula("count", "Retained source", reference + 1)
  assert(value_text(consumer, "consumer.count") == "3")
end
assert(value_text(retained_group, "retained_group.temporary.count") == "2")
summary:reset()
assert(value_text(retained_group, "retained_group.temporary.count") == "1")

local retained_real
do
  local temporary = stats.Group("temporary_real")
  retained_real = temporary:add_real_formula("count", "Temporary real source", reads + 0.5)
end
collectgarbage("collect")
local consumer = stats.Group("real_consumer")
consumer:add_real_formula("count", "Retained real source", retained_real + 1)
assert(tonumber(value_text(consumer, "real_consumer.count")) == 1.5)

print("Pass!")
