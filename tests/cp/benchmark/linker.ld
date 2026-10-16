/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

SECTIONS {
  /* Private result mailbox between the stack and text, leaving low RAM
   * available for reset/boot code. */
  .benchmark 0x8000 (NOLOAD) : { KEEP(*(.benchmark)) } > RAM
}
INSERT BEFORE .text;
ASSERT(SIZEOF(.benchmark) == 12, "benchmark mailbox layout mismatch");
ASSERT(_stack_start <= ADDR(.benchmark), "benchmark mailbox overlaps stack");
ASSERT(ADDR(.benchmark) + SIZEOF(.benchmark) <= ADDR(.text),
       "benchmark mailbox overlaps text");
