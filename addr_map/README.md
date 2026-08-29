<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# FORMOSA Address Map

Single design for the simulation (and future silicon-friendly) physical map.

## Sources of truth

| Consumer | File |
|----------|------|
| C / asm / firmware / HAL defaults | [`formosa_addr_map.h`](formosa_addr_map.h) |
| Lua system model / OpenCL env | [`../tests/formosa/lua/addr_map.lua`](../tests/formosa/lua/addr_map.lua) |

Numeric constants in those two files **must match**. `addr_map.lua` runs
`validate()` on require.

## Layout (simulation)

```
0x0000_0000  CP_ROM          4 KiB
0x0000_1000  CP_CTRL         4 KiB   printbuf / exit / pfreader
0x0000_2000  DMA CSRs                Host DMA + Device DMA (fab_cp)
0x0001_0000  CP_TCM        256 KiB
0x0005_0000  CLINT          64 KiB   SiFive mtime/mtimecmp offsets (fab_agent)
0x0006_0000  MMIO           64 KiB   HAL fsa_mmio_base (fab_agent scratch)
               +0x0100 CP CSR bank
               +0x1000 scratch (cmd ring then completion pool)
0x0007_0000  SM_MMIO        64 KiB   SM[i] at + i*4 KiB
0x0010_0000  ONCHIP_GMEM   512 KiB
0x8000_0000  DDR window
               +0x0000_0000 WGI 1 MiB
               +0x0010_0000 noncache heap
               +0x00FC_0000 firmware staging
               +0x0100_0000 stack pool
               +0x0200_0000 OpenCL / falloc heap
```

## Design rules

1. **Fixed apertures** — never derive `fsa_mmio_base` as `tcm_end + clint_size + …`.
2. **Power-of-two windows** — prefer 4 KiB / 64 KiB decode quanta.
3. **Stride for arrays** — SM MMIO uses 4 KiB stride even if registers only need `0xE8`.
4. **64 B alignment** for command packets and DMA-friendly buffers.
5. **Identity high addresses** — SM data path uses the same `0x8xxx_xxxx` DDR map as CP/HAL (no `0xE000` cutover).

## PoCL

PoCL does not hardcode this map; it uses HAL (`fsa_*`) which reads
`LV_FORMOSA_*` exported from `formosa.config` (derived from this map).
