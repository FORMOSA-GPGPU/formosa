<!-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# LV guest drivers

This directory holds public guest drivers for devices exposed by LV bindings.
Keep each driver separate from the host simulation implementation, with public
headers in `include/` and guest source files in `src/`. Consumers compile these
sources with their own toolchain, ISA, and ABI; this directory is not a CMake
subproject and its sources are not linked into the host LV executable.

The PLIC driver uses `lv/drivers/include/plic.h` and `lv/drivers/src/plic.c` to
access the register interface modeled by `simple.Plic`. Add `lv/drivers/include`
to the guest include path, use `#include <plic.h>`, and compile `plic.c` alongside
the guest program with the same RISC-V compiler flags. The platform supplies the
PLIC base address, source and context IDs, and CPU interrupt/trap configuration.
Callers serialize updates to the same enable bitmap word.

`tests/plic` demonstrates this integration with an RV64 guest and Lua signal
inputs. Cross-component integration tests belong in the top-level `tests/`
directory; model-specific tests remain in `lv/tests/unit/`.
