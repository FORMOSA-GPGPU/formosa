---
name: integrate-module
description: Produce a reusable Lunaverse module. Use when writing a thin binding, adding a Lua composite, wrapping Verilated RTL, or implementing native SystemC; for wiring changes inside an existing hierarchy, use compose-system instead.
---

# Integrate Module

Integrate an implementation as a minimal, idiomatic Lunaverse module. Producing a reusable module uses this skill; wiring changes inside an existing hierarchy use `compose-system`.

Consult [lunaverse-modules.md](lunaverse-modules.md) for the decision tree, placement rules, gotchas, and Definition of Done.

## Procedure

1. **Inspect and route**
   - Follow the decision tree and placement rules in `lunaverse-modules.md`.
   - *Criterion*: Route selected and target directory confirmed.

2. **Implement minimal interface**
   - Expose only constructor, configuration, connectivity, and essential methods, observing the gotchas in the reference.
   - Expose stats or traces only when the module itself requires observation.
   - *Criterion*: Module implements required public surface with no unused API exposure.

3. **Register module**
   - Register source files with the owning build target or place in the project Lua root per reference placement rules and gotchas.
   - *Criterion*: Source registered in CMake or placed in Lua root.

4. **Verify Definition of Done**
   - Build owning target to compile and regenerate `.cache/lv_meta/lv_api.lua`.
   - Prove constructible from Lua via an existing path; commit a new test only when no existing path constructs this module.
   - Verify every item on the Definition of Done checklist in `lunaverse-modules.md`.
   - When the integration edits an API-producing declaration, apply the interface-sensitive branch of the [Lua API compatibility gate](../references/lua-api-compatibility.md).
   - *Criterion*: All checklist items pass and every affected Lua API delta is required by the task.
