/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

SECTIONS {
  /* Private result mailbox, below the stack and away from program data. */
  .benchmark 0x1000 (NOLOAD) : { KEEP(*(.benchmark)) } > RAM
}
INSERT BEFORE .text;
ASSERT(SIZEOF(.benchmark) == 12, "benchmark mailbox layout mismatch");
ASSERT(_stack_start >= 0x4000,
       "benchmark requires stack space above its mailbox");
