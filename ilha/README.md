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
selects its D-cache capacity, WriteThrough policy, and MSHR count in
`ilha.config`. L1 D-caches are not coherent with each other, so
`simtix.pipelined_sm` rejects a WriteBack D-cache when `num_sm` is above 1.
Parameter annotations provide LuaLS completion and navigation; system fields
are inferred from the defaults table. CacheController is wired inside the SM
and is not an `sm.param` field.

## Compose and run the system in Lua

A custom runner can apply overrides directly before constructing the system:

```lua
-- Run inside lv, which supplies sc and the registered component bindings.
local Config = require("ilha.config")
local System = require("ilha.system")
local Host = require("ilha.host")

Config:override({
  system = { num_sm = 2 },
  sm = { param = { dcache = { cache_size_bytes = 0x8000 } } },
})

local system = System("System", Config, {
  host = Host.ipc("/tmp/formosa.sock"),
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

## Trace host commands

When lv is built with Perfetto, `system.trace.commands = true` in a
`--system-config` file records these tracks without changing simulated timing:

| Track | Recorded by | One slice per |
|---|---|---|
| `Host commands/Kernels`, `Memory copies`, `Cache commands`, `Barriers` | `ilha.command_trace` on a `dbg.AccessWatcher` in front of `scratch` | Host command, from the CP consuming it to the CP publishing its completion |
| `Host commands/Submissions` | the same | Host doorbell (instant) |

A command slice starts when the CP's write advancing RD_PTR completes on the
scratch link, after the CP has read the packet, and ends when its write of the
command's completion slot completes there. It includes the firmware's own work
on the command: dispatching a kernel and retiring its last work-group, or, for
a memory copy, waiting for earlier kernels and cache commands, the cache
maintenance and the DMA transfer. The time a command waits in the ring shows as
the gap after its `Submissions` instant; the host noticing the completion is
not traced. The WG slot tracks show when work-groups run on each SM.

A command slice is named by the `label` the host passed in
`FsaKernelLaunchInfo` or `FsaMemoryCopyInfo`, such as the kernel name, or else
by its type, such as `memcpy H2D` or `kernel 0x830030c0` (the kernel's
trampoline); sizes and geometry are its arguments.
Overlapping commands of one type open further lanes (`Cache commands #2`).
With `trace.commands = false`, the default, the watcher is not on the link.
The decoder reads packets through the LuaJIT FFI, from the declarations of
`hal/src/hal/command_packet.h` that `ilha.command_packet_abi` carries, preprocessed
at build time, so it holds no copy of the packet layout. `ilha.command_packet_abi`
also holds the constants the FFI cannot see: the bytes of the completion token
and slot fields, the result codes and the kernel dispatch flags.

The firmware phases of a command have their own switches, all true by
default:

| Track | Switch | Recorded by | One slice per |
|---|---|---|---|
| `host_dma/Transfer`, `device_dma/Transfer` | `trace.dma` | `dma.DMA` | DMA transfer; an empty transfer, an invalid descriptor and a bus error are instants |
| `…CacheController/Operations` | `trace.cache_operations` | `ilha.CacheController` | Flush or invalidate on one cache, including those a memory copy issues internally |
| `…wg_init/WG slot N` | `trace.wg_slots` | `ilha.WGInitializer` | Work group resident in that slot of one SM, from dispatch to completion |

Module-level Perfetto switches use `pftrace` (default: false). For example,
`dma.DMA("dma", { pftrace = true })` traces transfers. `simtix.BankedCache` passes
its `pftrace` to each cache bank, as `simtix.BankedMemory` does, and
`controller_pftrace` switches its maintenance controller's Operations track. In
the pipelined SM, `system.trace.cache_operations` sets `controller_pftrace` for
the D-cache composite. The system-level classification switches above and the
`lv-enable-perfetto` build option remain separate.

## Keep software and hardware consistent

[`lua/addr_map.lua`](lua/addr_map.lua) owns the fixed platform address map and
platform ABI. Component register contracts live in lv's shared C headers.
Xmake's `ilha-platform-data` target invokes two Lua generators, including for
HAL/firmware-only builds:

```text
ilha/lua/addr_map.lua
  ├─ scripts/generate_header.lua → formosa_addr_map.h
  └─ scripts/generate_linker.lua → ilha_memory.ld
```

Xmake places the generated files in the target's autogen directory and supplies
that include path through its dependency. Changing an experimental configuration
does not change the fixed address-map ABI.

Runtime capabilities come from the same settings that construct the hardware:

```text
Config.system.num_sm = 2
  ├─ System instantiates SM0 and SM1
  └─ SystemInfo.NUM_SM = 2 → HAL reads it when connecting
```

SystemInfo is a `simple.ConstantTable` on `fab_agent` at `0x60000`, accessible
to both the host and CP. ABI v1 contains version, length, SM count, warp/lane
counts, local-memory size, shared-cache size, cache-block size, the
global-allocation base, and the timebase frequency (CLINT `mtime` ticks per
second). Its valid length is `0x50`; each field is read
individually as an unsigned 64-bit little-endian value using generated offsets.
HAL validates version and length on connection; there is no
hardware-configuration environment dump.

Work-group stacks start at the fixed `stack_base`. Firmware gives every
resident work-group slot a stack for `threads_per_core` threads, so the system
reserves `num_sm × wg_resident_limit × threads_per_core × per_thread_stack_size`
bytes and starts global allocations at the next multiple of
`global_alloc_align` (`SystemConfig:global_alloc_base()`). SystemInfo reports
that base, so a larger configuration needs no address-map or SDK rebuild.

DMA offsets/status and CLINT offsets come from the headers under
`lv/device/include/lv/mmio/`. Software driver APIs live separately under
`lv/device/include/lv/drivers/`.
`formosa_addr_map.h` includes these contracts and retains the `FSA_DMA_*` and
`FSA_CLINT_*` compatibility names. The `lv.device` dependency supplies the
maintained headers; HAL/firmware builds need no lv executable or SystemC dependency.
