/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Formosa specific CSRs (XFormosa)

// Per-warp CSRs (read-only)
#define CSR_XWID 0xFC0

// Per-thread CSRs (read-only)
#define CSR_XLANEID 0xFD0

// Per-core CSRs (read-only)
#define CSR_XLANES 0xFE0
