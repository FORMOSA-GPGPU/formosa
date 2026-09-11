# Verilated RTL

Reference for the Verilated-RTL route: a Verilated model owned by an `sc_module`, exposed via `LV_BINDING`.

Follow `vclint` (`lv/src/lv/bindings/simple/vclint.sv`, `vclint.cc`, `lv/tests/unit/simple/vclint.lua`, `lv/src/liblv/vl_time.cc`).

## Rules

- New Verilated RTL needs a `verilate()` block on the owning target (`lv/src/lv/CMakeLists.txt`).
- A second `verilate()` target must mark the shared runtime copy `HEADER_FILE_ONLY`, or `verilated.cpp` links twice (duplicate symbols).
- Keep `OPT_GLOBAL -DVL_TIME_STAMP64` and define `vl_time_stamp64()` once in a TU including only `systemc.h` (verilated headers carry their own inline copy).
- Own an explicit `VerilatedContext` declared before the model so teardown order is deterministic.
- The first `eval()` only settles; hold `rst_n` across later clock pulses or reset never takes.
- Signal outputs need one `SC_METHOD` driver; funneling bus-op and clock updates through one helper double-drives (E115).
