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

#define TEST_ENTRY_COUNT 4
#define TEST_THREADS_PER_CORE 1024
#define TEST_GROUP_SIZE 256
#define TEST_REGION_SIZE (TEST_THREADS_PER_CORE * FSA_PER_THREAD_STACK_SIZE)

static void set_valid_config(struct sm_mmio *sm) {
  sm->THREADS_PER_CORE = TEST_THREADS_PER_CORE;
  sm->STACK_REMAP_ENTRY_COUNT = TEST_ENTRY_COUNT;
  sm->STACK_REMAP_GROUP_SIZE = TEST_GROUP_SIZE;
}

static void test_config_validation(void) {
  struct sm_mmio sm = {0};
  set_valid_config(&sm);

  assert(cp_stack_remap_validate_config(&sm));
  assert(!cp_stack_remap_validate_config(NULL));

  sm.STACK_REMAP_ENTRY_COUNT = 0;
  assert(!cp_stack_remap_validate_config(&sm));
  sm.STACK_REMAP_ENTRY_COUNT = FSA_SM_STACK_REMAP_MAX_ENTRIES + 1;
  assert(!cp_stack_remap_validate_config(&sm));
  sm.STACK_REMAP_ENTRY_COUNT = TEST_ENTRY_COUNT;

  sm.THREADS_PER_CORE = 0;
  assert(!cp_stack_remap_validate_config(&sm));
  sm.THREADS_PER_CORE = 768;
  assert(!cp_stack_remap_validate_config(&sm));
  sm.THREADS_PER_CORE = TEST_THREADS_PER_CORE;

  sm.STACK_REMAP_GROUP_SIZE = 0;
  assert(!cp_stack_remap_validate_config(&sm));
  sm.STACK_REMAP_GROUP_SIZE = 192;
  assert(!cp_stack_remap_validate_config(&sm));
  sm.STACK_REMAP_GROUP_SIZE = TEST_THREADS_PER_CORE * 2;
  assert(!cp_stack_remap_validate_config(&sm));
}

static void test_reset_and_validation(void) {
  struct sm_mmio sm;
  memset(&sm, 0xFF, sizeof(sm));
  set_valid_config(&sm);

  cp_stack_remap_reset(&sm);
  for (size_t i = 0; i < TEST_ENTRY_COUNT; ++i) {
    assert(sm.STACK_REMAP_TABLE[i] == 0);
  }
  assert(sm.STACK_REMAP_TABLE[TEST_ENTRY_COUNT] == UINT64_MAX);

  int slot = -1;
  assert(cp_stack_remap_configure(NULL, FSA_STACK_BASE, &slot) ==
         kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(
             &sm, FSA_STACK_BASE + TEST_GROUP_SIZE * FSA_PER_THREAD_STACK_SIZE,
             &slot) == kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(&sm, FSA_STACK_BASE + 8, &slot) ==
         kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(&sm, 1ull << 48, &slot) ==
         kCpStackRemapInvalid);
  assert(cp_stack_remap_configure(&sm, FSA_STACK_BASE, NULL) ==
         kCpStackRemapInvalid);

  sm.STACK_REMAP_ENTRY_COUNT = FSA_SM_STACK_REMAP_MAX_ENTRIES + 1;
  assert(cp_stack_remap_configure(&sm, FSA_STACK_BASE, &slot) ==
         kCpStackRemapInvalid);
}

static void test_allocate_release_and_reuse(void) {
  struct sm_mmio sm = {0};
  set_valid_config(&sm);

  for (int i = 0; i < TEST_ENTRY_COUNT; ++i) {
    const uint64_t base = FSA_STACK_BASE + (uint64_t)i * TEST_REGION_SIZE;
    int slot = -1;
    assert(cp_stack_remap_configure(&sm, base, &slot) == kCpStackRemapOkay);
    assert(slot == i);
    assert(sm.STACK_REMAP_TABLE[i] == (base | FSA_STACK_REMAP_VALID_BIT));
  }

  int slot = -1;
  assert(cp_stack_remap_configure(
             &sm, FSA_STACK_BASE + TEST_ENTRY_COUNT * TEST_REGION_SIZE,
             &slot) == kCpStackRemapFull);

  cp_stack_remap_release(&sm, 3);
  assert(sm.STACK_REMAP_TABLE[3] == 0);

  const uint64_t replacement =
      FSA_STACK_BASE + (TEST_ENTRY_COUNT + 1) * TEST_REGION_SIZE;
  assert(cp_stack_remap_configure(&sm, replacement, &slot) ==
         kCpStackRemapOkay);
  assert(slot == 3);
  assert(sm.STACK_REMAP_TABLE[3] == (replacement | FSA_STACK_REMAP_VALID_BIT));

  cp_stack_remap_release(&sm, -1);
  cp_stack_remap_release(&sm, TEST_ENTRY_COUNT);
  cp_stack_remap_release(NULL, 0);
}

int main(void) {
  test_config_validation();
  test_reset_and_validation();
  test_allocate_release_and_reuse();
  puts("cp_stack_remap_test passed");
  return 0;
}
