/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Formosa specific CSRs (XFormosa)

// Per-warp CSRs (read-only)
#define CSR_XWID 0xFC0
// Active lanes of the executing instruction, least significant 64 lanes.
#define CSR_XACTIVEMASK 0xFC2

// Per-thread CSRs (read-only)
#define CSR_XLANEID 0xFD0

// Per-core CSRs (read-only)
#define CSR_XLANES 0xFE0

// Work-item CSRs (read/write). The runtime writes them before a kernel starts.
// Per-warp
#define CSR_XSGSIZE 0xBC0
// Per-thread
#define CSR_XLOCALID_X 0xBD0
#define CSR_XLOCALID_Y 0xBD1
#define CSR_XLOCALID_Z 0xBD2
// Per-work-group, one copy per warp
#define CSR_XGROUPID_X 0xBF0
#define CSR_XGROUPID_Y 0xBF1
#define CSR_XGROUPID_Z 0xBF2
#define CSR_XLOCALSIZE_X 0xBF4
#define CSR_XLOCALSIZE_Y 0xBF5
#define CSR_XLOCALSIZE_Z 0xBF6
#define CSR_XGLOBALOFFSET_X 0xBF8
#define CSR_XGLOBALOFFSET_Y 0xBF9
#define CSR_XGLOBALOFFSET_Z 0xBFA
