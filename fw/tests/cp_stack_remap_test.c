/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_stack_remap.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_thread_geometry_validation(void) {
  assert(cp_stack_remap_thread_geometry_valid(FSA_STACK_REMAP_GROUP_SIZE));
  assert(!cp_stack_remap_thread_geometry_valid(0));
  assert(!cp_stack_remap_thread_geometry_valid(FSA_STACK_REMAP_GROUP_SIZE / 2));
  assert(!cp_stack_remap_thread_geometry_valid(FSA_STACK_REMAP_GROUP_SIZE * 2));
}

static void test_reset_and_validation(void) {
  struct sm_mmio sm;
  memset(&sm, 0xFF, sizeof(sm));

  cp_stack_remap_reset(&sm);
  for (size_t i = 0; i < FSA_SM_STACK_REMAP_ENTRIES; ++i) {
    assert(sm.STACK_REMAP_TABLE[i] == 0);
  }

  int slot = -1;
  assert(cp_stack_remap_configure(NULL, FSA_STACK_BASE, &slot) ==
         kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(&sm, FSA_STACK_BASE + 8, &slot) ==
         kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(&sm, 1ull << 48, &slot) ==
         kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(&sm, FSA_STACK_BASE, NULL) ==
         kCpStackRemapInvalid);
}

static void test_allocate_release_and_reuse(void) {
  struct sm_mmio sm = {0};

  for (int i = 0; i < (int)FSA_SM_STACK_REMAP_ENTRIES; ++i) {
    const uint64_t base =
        FSA_STACK_BASE + (uint64_t)i * FSA_STACK_REMAP_REGION_SIZE;
    int slot = -1;
    assert(cp_stack_remap_configure(&sm, base, &slot) == kCpStackRemapOkay);
    assert(slot == i);
    assert(sm.STACK_REMAP_TABLE[i] == (base | FSA_STACK_REMAP_VALID_BIT));
  }

  int slot = -1;
  assert(
      cp_stack_remap_configure(&sm,
                               FSA_STACK_BASE + FSA_SM_STACK_REMAP_ENTRIES *
                                                    FSA_STACK_REMAP_REGION_SIZE,
                               &slot) == kCpStackRemapFull);

  cp_stack_remap_release(&sm, 3);
  assert(sm.STACK_REMAP_TABLE[3] == 0);

  const uint64_t replacement =
      FSA_STACK_BASE +
      (FSA_SM_STACK_REMAP_ENTRIES + 1) * FSA_STACK_REMAP_REGION_SIZE;
  assert(cp_stack_remap_configure(&sm, replacement, &slot) ==
         kCpStackRemapOkay);
  assert(slot == 3);
  assert(sm.STACK_REMAP_TABLE[3] == (replacement | FSA_STACK_REMAP_VALID_BIT));

  cp_stack_remap_release(&sm, -1);
  cp_stack_remap_release(&sm, FSA_SM_STACK_REMAP_ENTRIES);
  cp_stack_remap_release(NULL, 0);
}

int main(void) {
  test_thread_geometry_validation();
  test_reset_and_validation();
  test_allocate_release_and_reuse();
  puts("cp_stack_remap_test passed");
  return 0;
}
