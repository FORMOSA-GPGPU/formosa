-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Helpers for defining Lua classes that own a SystemC hierarchy module.

local M = {}

local unpack = table.unpack or unpack

local function pack(...) return { n = select("#", ...), ... } end

local wrapped_classes = setmetatable({}, { __mode = "k" })

---@class lv.sc_module.wrap_options
---@field after_construct? fun(instance: table) Called after the backing module is attached and its hierarchy scope is closed.

---Make a Lua class hierarchy-aware.
---
---The class must define a name-first `new()` constructor that returns its Lua
---instance. The original constructor is invoked inside an `sc.Module` scope,
---and the resulting instance retains that module in `_sc_module`. An optional
---`after_construct` hook can perform work that must happen after the SystemC
---module constructor and its hierarchy scope have completed.
---@param class table
---@param options? lv.sc_module.wrap_options
---@return table class
function M.wrap(class, options)
  assert(type(class) == "table", "module class must be a table")
  assert(type(class.new) == "function", "module class must define new()")
  assert(not wrapped_classes[class], "module class is already wrapped")

  options = options or {}
  assert(type(options) == "table", "module class options must be a table")
  assert(
    options.after_construct == nil or type(options.after_construct) == "function",
    "after_construct must be a function"
  )

  local constructor = class.new
  local after_construct = options.after_construct

  class.new = function(name, ...)
    assert(type(name) == "string" and name ~= "", "module name must be a non-empty string")

    local args = pack(...)
    local instance
    local module = sc.Module(name, function()
      instance = constructor(name, unpack(args, 1, args.n))
      assert(type(instance) == "table", "module constructor must return a table")
      assert(
        rawget(instance, "_sc_module") == nil,
        "module constructor returned an already hierarchy-aware instance"
      )
    end)

    -- The module does not exist as a Lua userdata until its constructor
    -- callback returns, so attach it to the completed instance here.
    rawset(instance, "_sc_module", module)
    if after_construct then after_construct(instance) end
    return instance
  end

  wrapped_classes[class] = true
  return class
end

return M
