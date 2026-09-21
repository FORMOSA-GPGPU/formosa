<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# Ilha

Ilha (Ilha Lua Hardware Architecture) is FORMOSA's concrete system platform.
It owns the Lua system composition, fixed hardware/software address ABI,
loadable hardware configurations, platform runfiles, and the LV plugin used by
the platform.

The default `apccas2026` configuration models one 16-warp by 4-lane SM and an
LPDDR4 DRAM. Launchers accept `--config <name-or-file.lua>` for a complete
configuration, plus `--system-config`, `--sm`, and `--sm-param` overrides.
Relative resource paths are resolved from the configuration file that supplies
them.

The maintained `large_sm` configuration keeps one SM but expands it to 32
warps by 32 threads (1024 threads total) and selects LPDDR4-3200. It can be
selected with `--config <runfiles>/configs/large_sm.lua`.

Configurations contain `system` and `sm` tables. Named tables merge recursively,
arrays replace the previous array, and explicit command-line overrides apply
last. Selecting a different SM clears the previous SM's parameters.

`ilha.config` is the APCCAS2026 configuration singleton. A runner constructs one
system per process and applies configuration files and command-line settings
through one `override()` method. Configuration-file loading belongs to
`ilha.config_cli`:

```lua
---@type ilha.config
local Config = require("ilha.config")
return Config:override({
  system = { threads_per_warp = 32, warps_per_core = 32 },
  sm = { param = { dcache = { cache_size_bytes = 0x8000 } } },
})
```

Class names and parameter annotations connect LuaLS to the actual tables;
field lists are inferred from those tables. The named
system defaults define its LuaLS-visible fields; methods calculate derived
values and `override()` validates structural overrides. SM-specific fields stay
in the owning module, such as `simtix.pipelined_sm.param`. Annotate SM parameter
files with the selected module's type for editor checks; runtime construction
checks hardware constraints without maintaining a second SM field whitelist.
I-cache and D-cache parameters use `simtix.Cache.Param`; unspecified fields use
the Cache `LV_SCHEMA` defaults. APCCAS2026 explicitly selects the D-cache values
that differ from those generic component defaults.

Software-visible platform data is generated from `lua/addr_map.lua` into the
build tree. CMake invokes the xmake Lua generator automatically, including for
HAL/firmware-only builds. Generated files are not committed.

SystemInfo at `0x60000` exposes runtime hardware capabilities through the fixed
address-map ABI. HAL reads this table after connecting to the simulator; hardware
capabilities are no longer published through environment variables. ABI v1 has
a valid length of `0x40`; each field is an unsigned 64-bit little-endian value
and must be read individually at its generated offset.

Simtix continues to own its Lua SM compositions. The atomic and pipelined SM
compositions instantiate Ilha bindings, so the Ilha plugin must be linked when
those complete platform compositions are used.

The DMA offsets and status values in the generator intentionally shadow LV's
component-owned constants. This temporary duplication remains until LV has a
generic component MMIO-to-software-header workflow (issue #41).
