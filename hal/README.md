<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# FORMOSA HAL

The Hardware Abstraction Layer (HAL) and Real Embedded Abstraction Layer (REAL) for the FORMOSA platform.

# Project Layout

```
.
├── include/                     Public HAL headers
│   └── formosa-hal/             HAL header files
├── src/                         Source files for the HAL and REAL
│   ├── common/                  Common source files shared by different platforms
│   ├── hal/                     HAL source files
│   └── real/                    REAL source files and private `real.h`
└── test/                        Test suite for the driver
    └── lv/                      Tests for the lv
```

# Test Steps

To run the tests, ensure you have built your project with `-DHAL_ENABLE_TESTING=ON` and then execute the following command:

```bash
ctest --test-dir build
```
