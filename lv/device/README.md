<!-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# LV device interfaces

This standalone, header-only library holds the register contracts and guest
operations for devices exposed by LV bindings:

```
lv/device/
  include/lv/mmio/<binding-namespace>/<component>.h
  include/lv/drivers/<binding-namespace>/<component>.h
```

The outer directory names the whole device interface package. `lv/mmio/...`
contains register definitions; `lv/drivers/...` contains software operations.
The public include paths do not depend on the source directory name.

## Usage

Inside Formosa, link the interface target:

```cmake
target_link_libraries(my_program PRIVATE lv::device)
```

Without CMake, add `lv/device/include` to the include path. No simulation
libraries or separately compiled driver objects are required.
