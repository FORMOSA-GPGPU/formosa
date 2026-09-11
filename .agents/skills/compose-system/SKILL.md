---
name: compose-system
description: Compose a Lua system hierarchy from available modules. Use when modifying PipelinedSM or formosa.system, replacing interconnected modules, or changing address-map wiring.
---

# Compose System

Compose a Lunaverse system hierarchy from available modules. Producing a reusable module uses `integrate-module`; wiring changes within an existing hierarchy use this skill.

## Procedure

1. **Inspect current hierarchy and API**
   - Rebuild the owning target so `.cache/lv_meta/lv_api.lua` regenerates; the file records no provenance, so never trust a copy from an unknown build.
   - Inspect target hierarchy (`simtix/lua/pipelined_sm.lua` or `tests/formosa/lua/system.lua`) and available modules in the regenerated file.
   - If the task also edits an API-producing declaration, capture its affected public surface using the interface-sensitive branch of the [Lua API compatibility gate](../references/lua-api-compatibility.md).
   - *Criterion*: Target hierarchy and candidate modules identified; affected API baseline captured only when the task can change the generated API.

2. **Verify module availability**
   - Reuse existing modules. If a required module is missing or lacks bindings, run `integrate-module` first.
   - *Criterion*: All required modules verified available in Lua API.

3. **Wire the Lua hierarchy**
   - Implement architectural changes purely in Lua.
   - Keep module wiring, clock/reset distribution, and configuration parameters explicit.
   - If modifying the Formosa address map, keep `addr_map/formosa_addr_map.h` and `tests/formosa/lua/addr_map.lua` in sync.
   - Preserve existing public module interfaces and port names unless explicitly directed to alter them.
   - *Criterion*: Hierarchy updated with explicit connections and synchronized address maps.

4. **Validate elaboration**
   - Prove the hierarchy elaborates; prefer running the smallest existing test that constructs the modified hierarchy.
   - Apply the matching branch of the [Lua API compatibility gate](../references/lua-api-compatibility.md): wiring-only for pure Lua composition, or interface-sensitive when an API-producing declaration changed.
   - *Criterion*: Hierarchy elaborates and the applicable compatibility criterion passes.
