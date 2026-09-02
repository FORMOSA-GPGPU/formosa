/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_lmem_allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf(COLOR_RED "[FAIL] %s:%d: %s\n" COLOR_RESET, __FILE__, __LINE__, \
             #cond);                                                         \
      return false;                                                          \
    }                                                                        \
  } while (0)

int pass_count = 0;
int fail_count = 0;

#define RUN_TEST(test_fn)                                        \
  do {                                                           \
    printf(COLOR_YELLOW "[RUN] %s\n" COLOR_RESET, #test_fn);     \
    if (test_fn()) {                                             \
      printf(COLOR_GREEN "[PASS] %s\n\n" COLOR_RESET, #test_fn); \
      pass_count++;                                              \
    } else {                                                     \
      printf(COLOR_RED "[FAIL] %s\n\n" COLOR_RESET, #test_fn);   \
      fail_count++;                                              \
    }                                                            \
  } while (0)

extern lmem_allocator_t g_lmem_allocators[FORMOSA_MAX_NUM_SM];

#define TEST_WG_RESIDENT_LIMIT 4

// override the information in cp_hwinfo.c to avoid MMIO access in tests
uint64_t g_num_sm = 1;

void cp_hwinfo_init(void) {}

uint64_t cp_hwinfo_sm_wg_resident_limit(size_t sm_id) {
  return sm_id < g_num_sm ? TEST_WG_RESIDENT_LIMIT : 0;
}
// end of overrides for cp_hwinfo.c

bool cp_lmem_allocator_check_invariants(size_t sm_id) {
  if (sm_id >= g_num_sm) {
    return false;
  }

  lmem_allocator_t *alloc = &g_lmem_allocators[sm_id];

  if (alloc->free_count > alloc->free_capacity) {
    return false;
  }

  if (alloc->active_count > alloc->active_capacity) {
    return false;
  }

  uint64_t free_sum = 0;
  uint64_t active_sum = 0;
  size_t counted_active = 0;

  // Check free regions:
  // - valid count range
  // - non-zero size
  // - sorted by base
  // - non-overlapping
  // - non-adjacent (adjacent regions should already be coalesced)
  for (size_t i = 0; i < alloc->free_count; ++i) {
    const lmem_region_t *r = &alloc->free_regions[i];

    if (r->size == 0) {
      return false;
    }

    if (r->base + r->size > PER_SM_LOCAL_MEM_SIZE) {
      return false;
    }

    if (i > 0) {
      const lmem_region_t *prev = &alloc->free_regions[i - 1];

      // Must be strictly separated:
      // prev.end < curr.base
      // If prev.end == curr.base, coalescing failed.
      if (prev->base + prev->size >= r->base) {
        return false;
      }
    }

    free_sum += r->size;
  }

  // Unused free-region slots should be empty.
  for (size_t i = alloc->free_count; i < alloc->free_capacity; ++i) {
    if (alloc->free_regions[i].base != 0 || alloc->free_regions[i].size != 0) {
      return false;
    }
  }

  // Check active allocations:
  // - valid entries counted correctly
  // - non-zero size
  // - in range
  // - size aligned
  for (size_t i = 0; i < alloc->active_capacity; ++i) {
    const lmem_active_alloc_t *a = &alloc->active_allocs[i];

    if (!a->valid) {
      if (a->base != 0 || a->size != 0) {
        return false;
      }
      continue;
    }

    counted_active++;

    if (a->size == 0) {
      return false;
    }

    if ((a->size & (LMEM_ALLOC_ALIGNMENT - 1)) != 0) {
      return false;
    }

    if (a->base + a->size > PER_SM_LOCAL_MEM_SIZE) {
      return false;
    }

    active_sum += a->size;
  }

  if (counted_active != alloc->active_count) {
    return false;
  }

  // Check active allocations do not overlap each other.
  for (size_t i = 0; i < alloc->active_capacity; ++i) {
    const lmem_active_alloc_t *a = &alloc->active_allocs[i];
    if (!a->valid) {
      continue;
    }

    uint64_t a_begin = a->base;
    uint64_t a_end = a->base + a->size;

    for (size_t j = i + 1; j < alloc->active_capacity; ++j) {
      const lmem_active_alloc_t *b = &alloc->active_allocs[j];
      if (!b->valid) {
        continue;
      }

      uint64_t b_begin = b->base;
      uint64_t b_end = b->base + b->size;

      // Overlap iff [a_begin, a_end) intersects [b_begin, b_end)
      if (!(a_end <= b_begin || b_end <= a_begin)) {
        return false;
      }
    }
  }

  // Check active allocations do not overlap any free region.
  for (size_t i = 0; i < alloc->active_capacity; ++i) {
    const lmem_active_alloc_t *a = &alloc->active_allocs[i];
    if (!a->valid) {
      continue;
    }

    uint64_t a_begin = a->base;
    uint64_t a_end = a->base + a->size;

    for (size_t j = 0; j < alloc->free_count; ++j) {
      const lmem_region_t *r = &alloc->free_regions[j];

      uint64_t r_begin = r->base;
      uint64_t r_end = r->base + r->size;

      if (!(a_end <= r_begin || r_end <= a_begin)) {
        return false;
      }
    }
  }

  // Total accounted memory must match the whole per-SM local memory size.
  if (free_sum + active_sum != PER_SM_LOCAL_MEM_SIZE) {
    return false;
  }

  return true;
}

static bool expect_allocator_reset(void) {
  for (size_t i = 0; i < g_num_sm; ++i) {
    lmem_allocator_t *alloc = &g_lmem_allocators[i];

    // Check the allocator is in the initial state
    CHECK(alloc->free_regions != NULL);
    CHECK(alloc->active_allocs != NULL);
    CHECK(alloc->free_capacity == get_lmem_max_regions(i));
    CHECK(alloc->active_capacity == cp_hwinfo_sm_wg_resident_limit(i));

    CHECK(cp_lmem_allocator_check_invariants(i));
    // Check there is exactly one free region covering the whole space,
    // and no active allocations
    CHECK(alloc->free_count == 1);
    CHECK(alloc->free_regions[0].base == 0);
    CHECK(alloc->free_regions[0].size == PER_SM_LOCAL_MEM_SIZE);
    CHECK(alloc->active_count == 0);
  }
  return true;
}

static bool test_init_state(void) {
  cp_hwinfo_init();

  CHECK(cp_lmem_allocator_init() == 0);
  CHECK(expect_allocator_reset());
  cp_lmem_allocator_deinit();
  return true;
}

static bool test_invalid_requests(void) {
  uint64_t base = UINT64_MAX;

  cp_hwinfo_init();

  CHECK(cp_lmem_allocator_init() == 0);

  CHECK(cp_lmem_allocator_alloc(g_num_sm, 0x100, &base) == -1);
  CHECK(cp_lmem_allocator_alloc(0, 0, &base) == -1);
  CHECK(cp_lmem_allocator_alloc(0, 0x100, NULL) == -1);
  CHECK(cp_lmem_allocator_alloc(0, PER_SM_LOCAL_MEM_SIZE + 1, &base) == -1);
  CHECK(cp_lmem_allocator_free(g_num_sm, 0) == -1);
  CHECK(cp_lmem_allocator_free(0, 0x1234) == -1);
  CHECK(cp_lmem_allocator_check_invariants(0));

  cp_lmem_allocator_deinit();
  return true;
}

static bool test_alignment_and_exact_fit(void) {
  uint64_t first = UINT64_MAX;
  uint64_t second = UINT64_MAX;
  uint64_t full = UINT64_MAX;

  cp_hwinfo_init();

  CHECK(cp_lmem_allocator_init() == 0);

  CHECK(cp_lmem_allocator_alloc(0, 1, &first) == 0);
  CHECK(first == 0);
  CHECK(g_lmem_allocators[0].active_allocs[0].size == LMEM_ALLOC_ALIGNMENT);

  CHECK(cp_lmem_allocator_alloc(0, LMEM_ALLOC_ALIGNMENT - 1, &second) == 0);
  CHECK(second == LMEM_ALLOC_ALIGNMENT);

  CHECK(cp_lmem_allocator_free(0, first) == 0);
  CHECK(cp_lmem_allocator_free(0, second) == 0);
  CHECK(expect_allocator_reset());

  CHECK(cp_lmem_allocator_alloc(0, PER_SM_LOCAL_MEM_SIZE, &full) == 0);
  CHECK(full == 0);
  CHECK(g_lmem_allocators[0].free_count == 0);
  CHECK(g_lmem_allocators[0].active_count == 1);
  CHECK(cp_lmem_allocator_free(0, full) == 0);
  CHECK(expect_allocator_reset());

  // Allocate more than available after alignment.
  CHECK(cp_lmem_allocator_alloc(0, 0x100, &first) == 0);
  CHECK(cp_lmem_allocator_alloc(0, PER_SM_LOCAL_MEM_SIZE - 0x100 + 1,
                                &second) == -1);

  cp_lmem_allocator_deinit();
  return true;
}

static bool test_first_fit_and_coalescing(void) {
  uint64_t a = UINT64_MAX;
  uint64_t b = UINT64_MAX;
  uint64_t c = UINT64_MAX;
  uint64_t d = UINT64_MAX;

  cp_hwinfo_init();

  CHECK(cp_lmem_allocator_init() == 0);

  CHECK(cp_lmem_allocator_alloc(0, 0x100, &a) == 0);
  CHECK(cp_lmem_allocator_alloc(0, 0x200, &b) == 0);
  CHECK(cp_lmem_allocator_alloc(0, 0x300, &c) == 0);

  CHECK(a == 0x0);
  CHECK(b == 0x100);
  CHECK(c == 0x300);

  CHECK(cp_lmem_allocator_free(0, b) == 0);
  CHECK(cp_lmem_allocator_free(0, a) == 0);
  CHECK(g_lmem_allocators[0].free_count == 2);
  CHECK(g_lmem_allocators[0].free_regions[0].base == 0x0);
  CHECK(g_lmem_allocators[0].free_regions[0].size == 0x300);

  CHECK(cp_lmem_allocator_alloc(0, 0x180, &d) == 0);
  CHECK(d == 0x0);
  CHECK(g_lmem_allocators[0].free_regions[0].base == 0x200);
  CHECK(g_lmem_allocators[0].free_regions[0].size == 0x100);

  CHECK(cp_lmem_allocator_free(0, d) == 0);
  CHECK(cp_lmem_allocator_free(0, c) == 0);
  CHECK(expect_allocator_reset());

  cp_lmem_allocator_deinit();

  return true;
}

static bool test_wg_resident_limit_and_double_free(void) {
  uint64_t base[TEST_WG_RESIDENT_LIMIT] = {0};
  uint64_t extra = UINT64_MAX;

  cp_hwinfo_init();

  CHECK(cp_lmem_allocator_init() == 0);

  for (size_t i = 0; i < TEST_WG_RESIDENT_LIMIT; ++i) {
    CHECK(cp_lmem_allocator_alloc(0, LMEM_ALLOC_ALIGNMENT, &base[i]) == 0);
  }

  CHECK(g_lmem_allocators[0].active_count == TEST_WG_RESIDENT_LIMIT);
  CHECK(cp_lmem_allocator_alloc(0, LMEM_ALLOC_ALIGNMENT, &extra) == -1);

  CHECK(cp_lmem_allocator_free(0, base[1]) == 0);
  CHECK(cp_lmem_allocator_free(0, base[1]) == -1);
  CHECK(cp_lmem_allocator_check_invariants(0));

  CHECK(cp_lmem_allocator_free(0, base[0]) == 0);
  CHECK(cp_lmem_allocator_free(0, base[2]) == 0);
  CHECK(cp_lmem_allocator_free(0, base[3]) == 0);
  CHECK(expect_allocator_reset());

  cp_lmem_allocator_deinit();

  return true;
}

static bool test_fragmentation_stress(void) {
  uint64_t base[TEST_WG_RESIDENT_LIMIT] = {0};
  uint64_t reuse1 = UINT64_MAX;
  uint64_t reuse2 = UINT64_MAX;

  cp_hwinfo_init();
  CHECK(cp_lmem_allocator_init() == 0);

  // Step 1:
  // Fill the allocator up to the resident limit.
  // After this, there should be no free region left.
  for (size_t i = 0; i < TEST_WG_RESIDENT_LIMIT; ++i) {
    CHECK(cp_lmem_allocator_alloc(0, LMEM_ALLOC_ALIGNMENT, &base[i]) == 0);
    CHECK(cp_lmem_allocator_check_invariants(0));
  }

  // Step 2:
  // Free selected allocations to create "holes" (fragmentation).
  // Now free list should contain multiple small regions.
  CHECK(cp_lmem_allocator_free(0, base[1]) == 0);
  CHECK(cp_lmem_allocator_check_invariants(0));

  CHECK(cp_lmem_allocator_free(0, base[3]) == 0);
  CHECK(cp_lmem_allocator_check_invariants(0));

  // Step 3:
  // Allocate again to test whether allocator reuses fragmented holes.
  // Since this is first-fit, it should pick the earliest suitable hole.
  CHECK(cp_lmem_allocator_alloc(0, LMEM_ALLOC_ALIGNMENT, &reuse1) == 0);
  CHECK(cp_lmem_allocator_check_invariants(0));

  CHECK(cp_lmem_allocator_alloc(0, LMEM_ALLOC_ALIGNMENT, &reuse2) == 0);
  CHECK(cp_lmem_allocator_check_invariants(0));

  // Step 4:
  // Free remaining original allocations in a different order.
  // This stresses coalescing logic (non-sequential free).
  for (size_t i = 0; i < TEST_WG_RESIDENT_LIMIT; ++i) {
    if (i == 1 || i == 3) {
      continue;
    }
    CHECK(cp_lmem_allocator_free(0, base[i]) == 0);
    CHECK(cp_lmem_allocator_check_invariants(0));
  }

  // Step 5:
  // Free reused allocations.
  // At this point all memory should be returned.
  CHECK(cp_lmem_allocator_free(0, reuse1) == 0);
  CHECK(cp_lmem_allocator_check_invariants(0));

  CHECK(cp_lmem_allocator_free(0, reuse2) == 0);
  CHECK(cp_lmem_allocator_check_invariants(0));

  // Step 6:
  // Final state should be fully coalesced back to a single region.
  CHECK(expect_allocator_reset());

  cp_lmem_allocator_deinit();
  return true;
}

int main(void) {
  RUN_TEST(test_init_state);
  RUN_TEST(test_invalid_requests);
  RUN_TEST(test_alignment_and_exact_fit);
  RUN_TEST(test_first_fit_and_coalescing);
  RUN_TEST(test_wg_resident_limit_and_double_free);
  RUN_TEST(test_fragmentation_stress);

  printf("=================================\n");
  printf(COLOR_GREEN "PASS: %d\n" COLOR_RESET, pass_count);
  printf(COLOR_RED "FAIL: %d\n" COLOR_RESET, fail_count);

  return (fail_count == 0) ? 0 : 1;
}
