-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local M = {}

function M.fill(size, byte)
  local data = {}
  for i = 1, size do
    data[i] = byte
  end
  return data
end

function M.clone(bytes)
  local out = {}
  for i = 1, #bytes do
    out[i] = bytes[i]
  end
  return out
end

function M.sequence(size, first, stride)
  local data = {}
  for i = 0, size - 1 do
    data[#data + 1] = (first + i * stride) % 256
  end
  return data
end

function M.slice(bytes, offset, size)
  local out = {}
  for i = 1, size do
    out[i] = bytes[offset + i]
  end
  return out
end

function M.with_patch(bytes, offset, patch)
  local out = M.clone(bytes)
  for i = 1, #patch do
    out[offset + i] = patch[i]
  end
  return out
end

function M.bytes_equal(a, b)
  if a == nil or b == nil or #a ~= #b then return false end
  for i = 1, #a do
    if a[i] ~= b[i] then return false end
  end
  return true
end

function M.format_bytes(bytes)
  if bytes == nil then return "<nil>" end
  local parts = {}
  for i = 1, #bytes do
    parts[i] = string.format("%02x", bytes[i])
  end
  return table.concat(parts, " ")
end

function M.assert_bytes_equal(actual, expected, label)
  assert(actual ~= nil, "missing data: " .. label)
  assert(
    #actual == #expected,
    string.format("%s: size mismatch got=%d expected=%d", label, #actual, #expected)
  )

  for i = 1, #expected do
    assert(
      actual[i] == expected[i],
      string.format(
        "%s: byte %d mismatch got=0x%02x expected=0x%02x",
        label,
        i,
        actual[i],
        expected[i]
      )
    )
  end
end

function M.assert_unique_expected(entries, label)
  for i = 1, #entries do
    for j = i + 1, #entries do
      assert(
        not M.bytes_equal(entries[i].data, entries[j].data),
        string.format(
          "%s: fixtures %q and %q have indistinguishable response data",
          label,
          entries[i].label,
          entries[j].label
        )
      )
    end
  end
end

function M.drain_expected_reads(initiator, expected_reads, label)
  while #expected_reads > 0 do
    local actual = initiator:get_read_data()
    assert(actual ~= nil, label .. ": missing read response")

    local match_index = nil
    for index, expected in ipairs(expected_reads) do
      if M.bytes_equal(actual, expected.data) then
        match_index = index
        break
      end
    end

    assert(
      match_index ~= nil,
      string.format("%s: unexpected read response [%s]", label, M.format_bytes(actual))
    )
    table.remove(expected_reads, match_index)
  end

  local extra = initiator:get_read_data()
  assert(
    extra == nil,
    string.format("%s: unexpected extra read response [%s]", label, M.format_bytes(extra))
  )
end

function M.wait_until_completed(source, target_count, max_cycles, period, label)
  for _ = 1, max_cycles do
    if source:completed_count() >= target_count then return end
    sc.start(period)
  end

  assert(
    source:completed_count() >= target_count,
    string.format(
      "%s: timed out waiting for completions got=%d expected=%d",
      label,
      source:completed_count(),
      target_count
    )
  )
end

function M.wait_until(predicate, max_cycles, period, label)
  for _ = 1, max_cycles do
    if predicate() then return end
    sc.start(period)
  end
  assert(predicate(), label .. ": timed out")
end

function M.wait_read_data(source, max_cycles, period, label)
  for _ = 1, max_cycles do
    local data = source:get_read_data()
    if data ~= nil then return data end
    sc.start(period)
  end
  error(label .. ": timed out waiting for read response")
end

function M.u64_bytes(value)
  local data = {}
  for i = 1, 8 do
    data[i] = value % 256
    value = math.floor(value / 256)
  end
  return data
end

function M.u64_from_bytes(data)
  assert(data ~= nil and #data == 8, "u64 response must contain eight bytes")
  local value = 0
  for i = 8, 1, -1 do
    value = value * 256 + data[i]
  end
  return value
end

function M.mmio_write_u64(source, base, offset, value, period, max_cycles, label)
  local target_count = source:completed_count() + 1
  source:add_payload({ addr = base + offset, data = M.u64_bytes(value) })
  M.wait_until_completed(source, target_count, max_cycles or 512, period, label)
end

function M.mmio_read_u64(source, base, offset, period, max_cycles, label)
  local target_count = source:completed_count() + 1
  source:add_payload({ addr = base + offset, size = 8 })
  M.wait_until_completed(source, target_count, max_cycles or 512, period, label)
  return M.u64_from_bytes(M.wait_read_data(source, 1, period, label))
end

function M.wait_mmio_idle(source, base, start_offset, period, max_polls, label)
  for _ = 1, max_polls do
    if M.mmio_read_u64(source, base, start_offset, period, 512, label) == 0 then return end
    sc.start(period)
  end
  error(label .. ": timed out waiting for MMIO operation to become idle")
end

return M
