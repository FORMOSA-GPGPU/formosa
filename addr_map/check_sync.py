#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0
"""Verify Lua addr_map literals match formosa_addr_map.h base constants."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HEADER = ROOT / "formosa_addr_map.h"
LUA = ROOT.parent / "tests" / "formosa" / "lua" / "addr_map.lua"

# header define -> lua field
PAIRS = [
    ("FSA_CP_ROM_BASE", "cp_rom_base"),
    ("FSA_CP_CTRL_BASE", "cp_ctrl_base"),
    ("FSA_CP_TCM_BASE", "cp_tcm_base"),
    ("FSA_CLINT_BASE", "clint_base"),
    ("FSA_MMIO_BASE", "fsa_mmio_base"),
    ("FSA_SM_MMIO_BASE", "sm_mmio_base"),
    ("FSA_ONCHIP_GMEM_BASE", "onchip_gmem_base"),
    ("FSA_GLOBAL_MEM_BASE", "global_mem_base"),
    ("FSA_STACK_BASE", "stack_base"),
    ("FSA_GLOBAL_ALLOC_BASE", "global_alloc_base"),
    ("FSA_LMEM_SIZE", "lmem_size"),
    ("FSA_SM_MMIO_STRIDE", "sm_mmio_stride"),
    ("FSA_SM_MMIO_REG_SIZE", "sm_mmio_reg_size"),
]


def parse_header_literals(text: str) -> dict[str, int]:
    # Only plain 0x..ull assigns, not parenthesized expressions.
    return {
        name: int(val, 16)
        for name, val in re.findall(r"#define\s+(FSA_\w+)\s+(0x[0-9a-fA-F]+)ull", text)
    }


def parse_lua_literals(text: str) -> dict[str, int]:
    return {
        name: int(val, 16)
        for name, val in re.findall(r"(\w+)\s*=\s*(0x[0-9a-fA-F]+)", text)
    }


def main() -> int:
    hdr = parse_header_literals(HEADER.read_text())
    lua = parse_lua_literals(LUA.read_text())
    failed = 0
    for hname, lname in PAIRS:
        if hname not in hdr:
            print(f"MISSING header literal: {hname}")
            failed += 1
            continue
        if lname not in lua:
            print(f"MISSING lua field: {lname}")
            failed += 1
            continue
        if hdr[hname] != lua[lname]:
            print(f"MISMATCH {hname}=0x{hdr[hname]:x} vs {lname}=0x{lua[lname]:x}")
            failed += 1
        else:
            print(f"OK {hname} == {lname} == 0x{hdr[hname]:x}")

    # Derived checks (expression-style header macros)
    if hdr["FSA_MMIO_BASE"] + 0x100 != lua["cp_csr_base"]:
        print("MISMATCH CP CSR base vs MMIO+0x100")
        failed += 1
    if hdr["FSA_MMIO_BASE"] + 0x1000 != lua["scratch_base"]:
        print("MISMATCH scratch base vs MMIO+0x1000")
        failed += 1
    if hdr["FSA_GLOBAL_MEM_BASE"] + 0x100000 != lua["noncache_alloc_base"]:
        print("MISMATCH noncache base vs GLOBAL+1MiB")
        failed += 1
    # ABI v3: Command Ring at scratch base, Completion Pool immediately after.
    entries = lua.get("cmd_ring_entries", 64)
    packet_bytes = lua.get("cmd_packet_size", 64)
    slot_bytes = lua.get("completion_slot_bytes", 8)
    ring_base = lua["scratch_base"]
    ring_bytes = entries * packet_bytes
    pool_bytes = entries * slot_bytes
    pool_base = ring_base + ring_bytes
    if ring_base % 64 != 0:
        print(f"MISMATCH cmd_ring_base not 64B aligned: 0x{ring_base:x}")
        failed += 1
    if lua.get("cmd_ring_base") != ring_base:
        print(
            f"MISMATCH cmd_ring_base: lua=0x{lua.get('cmd_ring_base', 0):x} "
            f"expected=0x{ring_base:x}"
        )
        failed += 1
    if lua.get("completion_pool_base") != pool_base:
        print(
            f"MISMATCH completion_pool_base: lua=0x{lua.get('completion_pool_base', 0):x} "
            f"expected=0x{pool_base:x}"
        )
        failed += 1
    if lua.get("completion_pool_bytes") != pool_bytes:
        print(
            f"MISMATCH completion_pool_bytes: lua={lua.get('completion_pool_bytes')} "
            f"expected={pool_bytes}"
        )
        failed += 1
    if lua["scratch_size"] < ring_bytes + pool_bytes:
        print("MISMATCH scratch too small for ring+pool")
        failed += 1
    if failed == 0:
        print(
            f"OK ABI v3 layout ring=0x{ring_base:x} pool=0x{pool_base:x} "
            f"ring={ring_bytes} pool_bytes={pool_bytes}"
        )

    if failed:
        print(f"FAILED ({failed} issues)")
        return 1
    print("ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
