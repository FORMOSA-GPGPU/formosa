---
name: spec-to-rtl
description: Convert a SystemC/C++ module as spec into SystemVerilog RTL.
---

# Spec to RTL

Author SystemVerilog RTL from a SystemC/C++ spec as a standalone implementation. Producing a reusable Lunaverse module uses `integrate-module`; wiring it into a hierarchy uses `compose-system`.

## Procedure

1. **Freeze spec**
   - Extract interface, configuration, and timing behavior from the SystemC/C++ source.
   - *Criterion*: Port list, parameters, and cycle behavior written down before any RTL.

2. **Define RTL contract**
   - Define clock/reset, valid/ready, and per-cycle behavior of the RTL.
   - *Criterion*: Contract states exact port names and cycle semantics.

3. **Implement SystemVerilog**
   - Write standalone synthesizable SystemVerilog under the owning module directory.
   - Expose only the ports and parameters from the contract.
   - Leave SystemC wrappers, Lua bindings, build registration, and hierarchy wiring to `integrate-module` and `compose-system`.
   - *Criterion*: RTL matches the contract, has no unused ports or parameters, and contains no integration-layer changes.

4. **Prove elaboration**
   - Prove the RTL elaborates with the smallest sufficient check.
   - *Criterion*: Elaboration passes; cycle-equivalence against the spec is explicitly out of scope.

5. **Handoff**
   - List the RTL files, contract, spec source, and elaboration evidence as the handoff package.
   - Stop here; do not proceed further. If the RTL goes into Lunaverse, `integrate-module`'s Verilated-RTL route takes this package.
   - *Criterion*: Handoff package delivered with no further step attempted.
