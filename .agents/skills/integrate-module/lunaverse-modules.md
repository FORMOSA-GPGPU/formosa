# Lunaverse Modules

Reference for authoring, binding, and validating Lunaverse modules.

## Decision tree

Select the first viable route:

1. **Lua composition** — assemble existing Lunaverse modules in Lua
2. **thin binding** — expose existing SystemC/C++ via `LV_BINDING` and `LV_SCHEMA`
3. **Verilated RTL** — SystemVerilog RTL wrapped in an `sc_module`; see [verilated-rtl.md](verilated-rtl.md)
4. **native SystemC** — implement a new in-tree model

## Placement

- **Generic component**: `lv/src/lv/bindings/<ns>/`
- **GPGPU arch**: `simtix/src/<area>/`
- **Lua composite**: `$project/lua/` → `require("$project.name")` via `lv_register_project_lua_roots`
- **Formosa system wiring**: `tests/formosa/lua/`

## Canonical examples

Inspect these files for conventions and patterns:

| Pattern | Reference Path |
| :--- | :--- |
| Lua composition | `simtix/lua/banked_memory.lua` |
| Lua hierarchy | `simtix/lua/pipelined_sm.lua` |
| native SystemC | `lv/src/lv/bindings/simple/memory.h`, `lv/src/lv/bindings/simple/memory.cc` |
| thin binding | `lv/src/lv/bindings/dramsys/dramsys.cc` + `lv/tests/unit/dramsys/dramsys.lua` |
| RTL source | `lv/src/lv/bindings/simple/vclint.sv` |
| Verilated wrapper | `lv/src/lv/bindings/simple/vclint.cc` + `lv/tests/unit/simple/vclint.lua` |
| Verilated time | `lv/src/liblv/vl_time.cc` |
| Lua test registration | `lv/tests/unit/CMakeLists.txt` |
| Independent CMake target | `simtix/src/CMakeLists.txt` (`lv_register_binding(simtix)`) |
| Generated API | `.cache/lv_meta/lv_api.lua` |

## Gotchas

- New `.cc` must be listed on the owning CMake target
- New CMake library target must call `lv_register_binding()`
- Lua classes that own a SystemC hierarchy use `require("lv.sc_module").wrap`
- Constructors are name-first

## Definition of Done

A Lunaverse module is complete when:

- [ ] builds
- [ ] constructible from Lua
- [ ] required configuration is exposed
- [ ] required ports/signals are exposed
- [ ] appears in generated Lua API
