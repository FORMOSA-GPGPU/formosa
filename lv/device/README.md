<!-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# lv device interfaces

This standalone, header-only library holds the register contracts and guest
operations for devices exposed by lv bindings:

```
lv/device/
  include/lv/mmio/<binding-namespace>/<component>.h
  include/lv/drivers/<binding-namespace>/<component>.h
```

The outer directory names the whole device interface package. `lv/mmio/...`
contains register definitions; `lv/drivers/...` contains software operations.
The public include paths do not depend on the source directory name.

## Usage

Inside FORMOSA, depend on the header-only target:

```lua
add_deps("lv.device")
```

For a standalone consumer, add `lv/device/include` to the include path. No simulation
libraries or separately compiled driver objects are required.
