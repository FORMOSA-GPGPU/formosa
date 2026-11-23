-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

require("cp.cores")

-- Memory ports are writable target-socket properties, like SM.target.
-- A core has either mem_target (one port), or imem_target and dmem_target (two).
-- Capabilities are class fields and can be read without constructing a model.
---@class cp.core
---@field isa string
---@field num_mem_ports 1|2
---@field has_ext_int boolean
---@field has_sw_int boolean
---@field has_timer_int boolean
---@field has_reset boolean Whether reset_n prevents execution during the reset interval.
---@field clock sc.clock
---@field reset_n sc.signal Active-low reset input; adapters without reset support ignore it.
---@field mem_target? sc.Socket Unified memory target; only for num_mem_ports == 1.
---@field imem_target? sc.Socket Instruction target; only for num_mem_ports == 2.
---@field dmem_target? sc.Socket Data target; only for num_mem_ports == 2.
---@field ext_int? sc.BoolIn External interrupt port; only when has_ext_int.
---@field sw_int? sc.BoolIn Software interrupt port; only when has_sw_int.
---@field timer_int? sc.BoolIn Timer interrupt port; only when has_timer_int.
---@field boot fun(self: cp.core, entry: integer)
---@field protected _sc_module sc.Module
---@alias cp.core_ctor fun(name: string): cp.core

--- Standard single-hart RISC-V board: RAM, UART, and an optional CLINT.
--- Address layout belongs to the program suite; adapters own core differences.
---@class cp.system
---@field core cp.core
---@field ext_int? sc.signal Signal driven by interrupt tests.
---@field sw_int? sc.signal
---@field timer_int? sc.signal
---@field private _clint? simple.Clint
---@field private _period sc.time
---@field private _clock sc.clock
---@field private _reset_n sc.signal
---@field private _reset_cycles integer
---@field private _ram simple.LtMemory
---@field private _ram_base integer
---@field private _log_path string
---@field private _uart simple.PrintBuf
---@field private _bus nic.XBar
---@field private _loader dbg.MemoryDebugger
---@field protected _sc_module sc.Module
---@overload fun(name: string, core_module: string, platform?: table): cp.system
local System = {}
System.__index = System

---@param _ string Module name, consumed by lv.sc_module.wrap.
---@param core_module string A directly require-able CPU adapter module.
---@param platform? table RAM/UART addresses, clock period, and optional clint_base. Defaults to cp.platform.
---@return cp.system
function System.new(_, core_module, platform)
  platform = platform or require("cp.platform")
  local self = setmetatable({}, System --[[@as table]])
  ---@type cp.core_ctor
  local Core = require(core_module)
  self.core = Core("core")
  local ports = self.core.num_mem_ports
  assert(ports == 1 or ports == 2, "expected one or two memory ports")
  self._period = sc.time(platform.clock_ns or 10, sc.time_unit.NS)
  self._clock = sc.clock("clock", self._period)
  self._reset_n = sc.signal("reset_n", false)
  self._reset_cycles = platform.reset_cycles or 5
  self._ram_base = platform.ram_base
  self._ram = simple.LtMemory("ram", { size = platform.ram_size, latency = self._period })
  self._log_path = os.tmpname()
  self._uart = simple.PrintBuf("uart", 1, self._log_path)
  local mapping = {
    { addr = platform.ram_base, size = platform.ram_size },
    { addr = platform.serial_base, size = 4 },
  }
  if platform.clint_base then
    self._clint = simple.Clint("clint", 1)
    self._clint.clock = self._clock
    mapping[#mapping + 1] = { addr = platform.clint_base, size = 0xc000 }
  end
  self._bus = nic.XBar("bus", ports + 1, mapping)
  self._bus.mem_side = self._ram.port
  self._bus.mem_side = self._uart.port
  if self._clint then self._bus.mem_side = self._clint.port end
  self._uart.clock = self._clock
  self._loader = dbg.MemoryDebugger("loader")
  self._loader.target = self._bus.core_side
  self.core.clock = self._clock
  self.core.reset_n = self._reset_n
  if ports == 1 then
    self.core.mem_target = self._bus.core_side
  else
    self.core.imem_target = self._bus.core_side
    self.core.dmem_target = self._bus.core_side
  end
  for _, name in ipairs({ "ext_int", "sw_int", "timer_int" }) do
    if self.core["has_" .. name] or (self._clint and name ~= "ext_int") then
      self[name] = sc.signal(name, false)
      if self.core["has_" .. name] then self.core[name](self[name]) end
    end
  end
  if self._clint then
    self._clint.msip_irq[1](self.sw_int)
    self._clint.timer_irq[1](self.timer_int)
  end
  return self
end

---@param elf workload.ELF
---@return boolean
function System:load_elf(elf)
  -- Multi-socket debug callbacks are connected during elaboration. Initialize
  -- the owned RAM before any CPU process starts, with PT_LOAD physical
  -- addresses relative to RAM and BSS zeroing. CPU images are identity-linked.
  self._ram:load_elf(elf, self._ram_base)
  return true
end

---@param addr integer
---@param bytes integer[]
function System:write_bytes(addr, bytes)
  assert(self._loader:write_bytes(addr, bytes) == #bytes, "incomplete memory write")
end

---@param addr integer
---@param size integer
---@return integer[]
function System:read_bytes(addr, size) return self._loader:read_bytes(addr, size) end

---@param entry integer
function System:boot(entry)
  entry = assert(tonumber(entry), "invalid entry address")
  self._reset_n:write(false)
  self.core:boot(entry)
  if self.core.has_reset then self:step(self._reset_cycles) end
  self._reset_n:write(true)
end

---@param cycles integer
function System:step(cycles) sc.start(cycles * self._period) end

function System:output()
  self._uart:flush()
  local file = assert(io.open(self._log_path, "r"))
  local text = file:read("*a")
  file:close()
  return text
end

function System:close()
  self._uart:flush()
  self._uart:close_file()
  os.remove(self._log_path)
end

local function positive_integer(value)
  return type(value) == "number" and value >= 1 and value < math.huge and value == math.floor(value)
end

--- Load a program and run it until its checker reports completion.
--- check(system, elapsed_cycles): return nil while pending, a truthy result on
--- success, or raise an error on failure. It may inspect memory/output or drive
--- test inputs. No particular output protocol or manifest is required.
--- Always closes the supplied system, including on load/check/timeout failures.
---@param elf_path string
---@param check fun(system: cp.system, elapsed_cycles: integer): any
---@param opts? { max_cycles?: integer, chunk_cycles?: integer }
---@return any result
function System:run(elf_path, check, opts)
  local ok, result = xpcall(function()
    opts = opts or {}
    local max_cycles = opts.max_cycles or 2097152
    local chunk_cycles = opts.chunk_cycles or 16384
    assert(positive_integer(max_cycles), "max_cycles must be a positive integer")
    assert(positive_integer(chunk_cycles), "chunk_cycles must be a positive integer")
    assert(type(check) == "function", "expected a program completion checker")
    local elf = workload.ELF(elf_path)
    assert(self:load_elf(elf), "failed to load CPU test ELF")
    self:boot(elf.entry)

    local elapsed = 0
    while elapsed < max_cycles do
      local cycles = math.min(chunk_cycles, max_cycles - elapsed)
      self:step(cycles)
      elapsed = elapsed + cycles
      local passed = check(self, elapsed)
      assert(passed ~= false, "program checker reported failure")
      if passed ~= nil then return passed end
    end
    error("CPU test timed out after " .. elapsed .. " cycles")
  end, debug.traceback)

  if not ok then
    local read, output = pcall(self.output, self)
    if read and type(output) == "string" then
      result = result .. "\nCPU test output:\n" .. output
    end
  end
  local closed, close_error = pcall(self.close, self)
  if not ok then
    if not closed then result = result .. "\nCleanup failed: " .. tostring(close_error) end
    error(result, 0)
  end
  assert(closed, close_error)
  return result
end

setmetatable(System --[[@as table]], { __call = function(cls, ...) return cls.new(...) end })
return require("lv.sc_module").wrap(System)
