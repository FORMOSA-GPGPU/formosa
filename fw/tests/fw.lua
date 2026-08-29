-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Fixed-aperture CP-private map (formosa.addr_map / formosa_addr_map.h):
--
--   0x0000_0000  CP_ROM
--   0x0000_1000  CP_CTRL (printbuf / exit)
--   0x0001_0000  CP_TCM
--   0x0006_0100  CP CSR (within MMIO scratch)
--
--          ┌────┐  ┌─────────┐
--          │ CP │ ┌┼Initiator│
--          └──┬─┘ │└─────────┘
-- ◄──┬─────┬──▼───▼─┬──►
--  ┌─▼─┐┌──▼──┴┐┌───▼────┐
--  │ROM││CP TCM││PrintBuf│
--  └───┘└──────┘└────────┘
--
-- Initiator used to poll an address for sim pause
local gu = require("util.gutil")
local addr = require("formosa.addr_map")

-- CP CSR offsets relative to addr.cp_csr_base (struct cp_mmio)
local CP_MMIO = {
  RESET = 0x0,
  FW_ADDR = 0x8,
  FW_SIZE = 0x10,
  CMD_RING_BASE = 0x18,
  CMD_SIZE = 0x20,
  CMD_RING_SIZE = 0x28,
  RD_PTR = 0x30,
  WR_PTR = 0x38,
}

local cp = cp.CommandProcessor("cp", 0, {
  core_trace = false,
  rsp_enable = false,
  rsp_port = 9982,
})
local rom = simple.Memory("rom", { size = addr.cp_rom_size, latency = 1, fifo_size = 1 })
local tcm = simple.Memory("tcm", { size = addr.cp_tcm_size, latency = 1, fifo_size = 1 })
-- Host/firmware-visible CSR bank (same pattern as formosa.system; not cp.csr_slave)
local cp_csr = simple.Memory("cp_csr", { size = addr.cp_csr_size, latency = 1, fifo_size = 1 })
local initiator = simple.Initiator("initiator")
local printbuf = simple.PrintBuf("printbuf", 1)

local xbar = simple.XBar("xbar", 3, {
  -- ROM
  { addr = addr.cp_rom_base, size = addr.cp_rom_size },
  -- CP_CTRL printbuf
  { addr = addr.cp_printbuf_base, size = 0x8 },
  -- TCM (gpufw.elf loads here @ 0x10000)
  { addr = addr.cp_tcm_base, size = addr.cp_tcm_size },
  -- CP CSR @ MMIO+0x100
  { addr = addr.cp_csr_base, size = addr.cp_csr_size },
})

local md = dbg.MemoryDebugger("md")

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local gnd = sc.signal("ground", false)

gu.log("yellow", "Bind ports")
cp.clock = clock
initiator.clock = clock
xbar.clock = clock
rom.clock = clock
tcm.clock = clock
cp_csr.clock = clock
printbuf.clock = clock

xbar.mem_side[1].target = rom.port
xbar.mem_side[2].target = printbuf.port
xbar.mem_side[3].target = tcm.port
xbar.mem_side[4].target = cp_csr.port
cp.target = xbar.core_side[1].port
initiator.target = xbar.core_side[2].port
md.target = xbar.core_side[3].port

cp.ext_int = gnd
cp.sw_int = gnd
cp.timer_int = gnd

gu.log("yellow", "Load elf")
local elf_path = gu.runfile("bin/gpufw.elf")
local elf = workload.ELF(elf_path)
md:load_elf(elf)

gu.log("yellow", "Set CP PC to " .. string.format("0x%x", elf.entry))
cp:set_pc(elf.entry)

initiator:add_payload({ poll = true, addr = 0, value = 1 })

gu.log("yellow", "Start simulation")
sc.start()

gu.log("green", "End simulation")
