-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local frontend = import("workflow.frontend")

local function quote(value)
  local replacements = {
    ['"'] = '\\"',
    ["\\"] = "\\\\",
    ["\b"] = "\\b",
    ["\f"] = "\\f",
    ["\n"] = "\\n",
    ["\r"] = "\\r",
    ["\t"] = "\\t",
  }
  return '"'
    .. value:gsub(
      '[%z\1-\31"\\]',
      function(character)
        return replacements[character] or string.format("\\u%04x", character:byte())
      end
    )
    .. '"'
end

function json(value, indentation)
  indentation = indentation or ""
  if type(value) == "table" then
    local next_indentation = indentation .. "  "
    local items = {}
    if frontend.is_array(value) then
      for _, item in ipairs(value) do
        items[#items + 1] = next_indentation .. json(item, next_indentation)
      end
      if #items == 0 then return "[]" end
      return "[\n" .. table.concat(items, ",\n") .. "\n" .. indentation .. "]"
    end
    for _, key in ipairs(frontend.keys(value)) do
      items[#items + 1] = next_indentation
        .. quote(key)
        .. ": "
        .. json(value[key], next_indentation)
    end
    if #items == 0 then return "{}" end
    return "{\n" .. table.concat(items, ",\n") .. "\n" .. indentation .. "}"
  elseif type(value) == "string" then
    return quote(value)
  elseif type(value) == "boolean" or type(value) == "number" then
    return tostring(value)
  end
  raise("Cannot serialize JSON value of type %s", type(value))
end
