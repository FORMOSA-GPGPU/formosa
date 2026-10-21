<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# ILHA

ILHA (Ilha Lua Hardware Architecture) composes Lunaverse modules and SM
implementations into the FORMOSA system platform. It owns system configuration,
the hardware/software address-map ABI, platform runfiles, and platform-specific
lv bindings. simtix owns the SM implementations.

## Choose a system configuration

After building the simulator and its platform runfiles, launch the daemon from
the build directory's `bin/` folder. The examples below use `build/`:

```bash
# APCCAS2026 defaults: one SM, 16 warps × 4 lanes, LPDDR4.
./build/bin/daemon.lua

# One larger SM: 32 warps × 32 lanes (1024 threads), LPDDR4-3200.
./build/bin/daemon.lua --config large_sm
```

[`lua/config.lua`](lua/config.lua) holds the APCCAS2026 defaults.
[`configs/apccas2026.lua`](configs/apccas2026.lua) returns `{}`; named and external
configuration files supply overrides to those defaults. For example:

```lua
-- experiment.lua
return {
  system = {
    num_sm = 2,
    threads_per_warp = 32,
    warps_per_core = 32,
    dram_config = "dram/experiment.json",
  },
  sm = {
    module = "simtix.pipelined_sm",
    param = {
      dcache = { cache_size_bytes = 0x8000 },
    },
  },
}
```

```bash
./build/bin/daemon.lua --config ./experiment.lua
```

`dram/experiment.json` resolves relative to `experiment.lua`. Use an existing
DRAMSys configuration and its referenced resources at that path.

## Override system and SM parameters separately

`system` controls system-wide configuration; `sm.module` selects the SM
implementation and `sm.param` holds its microarchitecture parameters.

```lua
-- system.lua: contents of the system block only.
return {
  num_sm = 2,
  clock_period_ns = 2.5,
}
```

```lua
-- sm.lua: parameters for the selected SM only.
---@type simtix.pipelined_sm.param
local param = {
  core = { num_subcores = 4 },
  icache = { replacement_policy = "lru" },
  dcache = { num_banks = 4, cache_size_bytes = 0x8000 },
}
return param
```

```bash
./build/bin/daemon.lua --config large_sm \
  --system-config ./system.lua \
  --sm simtix.pipelined_sm --sm-param ./sm.lua
```

[`lua/config_cli.lua`](lua/config_cli.lua) applies settings in this fixed order,
regardless of argument order:

```text
APCCAS2026 defaults → --config → --system-config → --sm → --sm-param
```

Named tables merge recursively; arrays replace the previous array. Changing
`sm.module` clears the old SM parameters. Each runner process configures one
system through the `ilha.config` singleton.

I-cache fields use `simtix.Cache.Param`; D-cache fields use
`simtix.banked_cache.param`. Unspecified component fields use `LV_SCHEMA`
defaults or the owning Lua composition's defaults. APCCAS2026 explicitly
selects its D-cache capacity, WriteBack policy, and MSHR count in `ilha.config`.
Parameter annotations provide LuaLS completion and navigation; system fields
are inferred from the defaults table. CacheController is wired inside the SM
and is not an `sm.param` field.

## Compose and run the system in Lua

A custom runner can apply overrides directly before constructing the system:

```lua
-- Run inside lv, which supplies sc and the registered component bindings.
local Config = require("ilha.config")
local System = require("ilha.system")

Config:override({
  system = { num_sm = 2 },
  sm = { param = { dcache = { cache_size_bytes = 0x8000 } } },
})

local system = System("System", Config, {
  agent_socket_path = "/tmp/formosa.sock",
})
system:start(1000) -- simulation cycles
sc.stop()
```

Derived values are methods, so overrides need no separate recomputation:

```lua
local Config = require("ilha.config")
Config:override({ system = { threads_per_warp = 32, warps_per_core = 32 } })
assert(Config.system:threads_per_core() == 1024)
```

System validates the configuration, creates the shared fabric, memories, CP,
and host agent, and instantiates the selected SM for each ID. The SM constructor
receives only its system settings, parameters, and platform address map:

```lua
-- Equivalent to one SM construction inside ilha.system:
local make_sm = require(Config.sm.module)
local sm = make_sm("SM0", {
  id = 0,
  config = Config.system,
  params = Config.sm.param,
  address_map = require("ilha.addr_map"),
})
```

The atomic and pipelined simtix compositions instantiate ILHA bindings, so
those complete compositions require the ILHA plugin to be linked.

## Keep software and hardware consistent

[`lua/addr_map.lua`](lua/addr_map.lua) owns the fixed platform address map and
platform ABI. Component register contracts live in lv's shared C headers.
Xmake's `ilha-platform-data` target invokes two Lua generators, including for
HAL/firmware-only builds. The temporary CMake fallback reuses these generators:

```text
ilha/lua/addr_map.lua
  ├─ scripts/generate_header.lua → formosa_addr_map.h
  └─ scripts/generate_linker.lua → ilha_memory.ld
```

Xmake places the generated files in the target's autogen directory and supplies
that include path through its dependency. The CMake fallback uses
`<builddir>/generated/ilha`. Changing an experimental configuration does not
change the fixed address-map ABI.

Runtime capabilities come from the same settings that construct the hardware:

```text
Config.system.num_sm = 2
  ├─ System instantiates SM0 and SM1
  └─ SystemInfo.NUM_SM = 2 → HAL reads it when connecting
```

SystemInfo is a `simple.ConstantTable` on `fab_agent` at `0x60000`, accessible
to both the host and CP. ABI v1 contains version, length, SM count, warp/lane
counts, local-memory size, shared-cache size, and cache-block size. Its valid
length is `0x40`; each field is read individually as an unsigned 64-bit
little-endian value using generated offsets. HAL validates version and length
on connection; there is no hardware-configuration environment dump.

DMA offsets/status and CLINT offsets come from the headers under
`lv/device/include/lv/mmio/`. Software driver APIs live separately under
`lv/device/include/lv/drivers/`.
`formosa_addr_map.h` includes these contracts and retains the `FSA_DMA_*` and
`FSA_CLINT_*` compatibility names. The software-only CMake path stages copies
of the maintained headers next to the generated platform data and tracks their
changes; HAL/firmware builds need no lv executable or SystemC dependency.
