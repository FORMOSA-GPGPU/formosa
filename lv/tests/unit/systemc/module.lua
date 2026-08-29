-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local sc_module = require("lv.sc_module")

---@class test.ChildModule
---@field protected _sc_module sc.Module
---@field memory simple.Memory
---@field argc integer
---@field optional any
local ChildModule = {}
ChildModule.__index = ChildModule

---@param name string
---@param optional any
---@param ... any
---@return test.ChildModule
function ChildModule.new(name, optional, ...)
  ---@type test.ChildModule
  local self = setmetatable({}, ChildModule)
  self.memory = simple.Memory("memory", { size = 16 })
  self.argc = select("#", optional, ...)
  self.optional = optional
  return self
end

setmetatable(ChildModule, {
  __call = function(class, ...) return class.new(...) end,
})

ChildModule = sc_module.wrap(ChildModule)

---@class test.ParentModule
---@field protected _sc_module sc.Module
---@field child test.ChildModule
---@field finalized boolean
local ParentModule = {}
ParentModule.__index = ParentModule

---@param name string
---@return test.ParentModule
function ParentModule.new(name)
  ---@type test.ParentModule
  local self = setmetatable({}, ParentModule)
  self.child = ChildModule("child", nil, "trailing")
  return self
end

setmetatable(ParentModule, {
  __call = function(class, ...) return class.new(...) end,
})

ParentModule = sc_module.wrap(ParentModule, {
  after_construct = function(instance)
    local module = assert(rawget(instance, "_sc_module"))
    assert(module.name == "parent")
    instance.finalized = true
  end,
})

local parent = ParentModule("parent")
local parent_module = assert(rawget(parent, "_sc_module"))
local child_module = assert(rawget(parent.child, "_sc_module"))

assert(parent_module.name == "parent")
assert(parent_module.basename == "parent")
assert(child_module.name == "parent.child")
assert(child_module.basename == "child")
assert(parent.child.argc == 2)
assert(parent.child.optional == nil)
assert(parent.finalized)

local parent_children = parent_module:child_names()
assert(#parent_children == 1)
assert(parent_children[1] == "parent.child")

local child_children = child_module:child_names()
assert(#child_children == 1)
assert(child_children[1] == "parent.child.memory")

local ok, err = pcall(sc_module.wrap, ChildModule)
assert(not ok)
assert(tostring(err):find("already wrapped", 1, true))

---@class test.BrokenModule
local BrokenModule = {}

---@param name string
function BrokenModule.new(name)
  simple.Memory("memory", { size = 16 })
  error("expected constructor failure")
end

BrokenModule = sc_module.wrap(BrokenModule)

ok, err = pcall(BrokenModule.new, "broken")
assert(not ok)
assert(tostring(err):find("expected constructor failure", 1, true))

-- A failed constructor must not leave its hierarchy scope active.
local after_error = ChildModule("after_error", nil)
local after_error_module = assert(rawget(after_error, "_sc_module"))
assert(after_error_module.name == "after_error")
