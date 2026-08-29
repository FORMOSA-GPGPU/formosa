/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_lmem_allocator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cp_trace.h"

#ifdef CP_LMEM_ALLOCATOR_USE_FREERTOS
/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */
#define CP_LMEM_ALLOCATOR_ENTER_CRITICAL() taskENTER_CRITICAL()
#define CP_LMEM_ALLOCATOR_EXIT_CRITICAL() taskEXIT_CRITICAL()
#else
#define CP_LMEM_ALLOCATOR_ENTER_CRITICAL()
#define CP_LMEM_ALLOCATOR_EXIT_CRITICAL()
#endif

lmem_allocator_t g_lmem_allocators[FORMOSA_MAX_NUM_SM] = {0};
static lmem_region_t g_lmem_free_regions[FORMOSA_MAX_NUM_SM]
                                        [FORMOSA_MAX_WG_RESIDENT_LIMIT + 1] = {
                                            0};
static lmem_active_alloc_t
    g_lmem_active_allocs[FORMOSA_MAX_NUM_SM][FORMOSA_MAX_WG_RESIDENT_LIMIT] = {
        0};

/**
 * Align a value up to the next alignment boundary.
 *
 * NOTE:
 * - This implementation assumes 'alignment' is a power of two.
 */
static inline uint64_t align_up_u64(const uint64_t addr,
                                    const uint64_t alignment) {
  if (alignment == 0) {
    return addr;  // No alignment requested
  }

  return (addr + alignment - 1) & ~(alignment - 1);
}

/**
 * Insert a freed region back into the free-region array and coalesce
 * with adjacent regions when possible.
 *
 * Invariants of free_regions[]:
 * - sorted by base address
 * - non-overlapping
 * - adjacent regions are always merged
 *
 * Returns:
 *   0  on success
 *  -1  on overlap error or no space to insert a new region
 */
static int free_region_insert_and_coalesce(lmem_allocator_t *alloc,
                                           uint64_t base, uint64_t size) {
  size_t insert_idx = 0;

  // Find insertion point so free_regions[] stays sorted by base.
  while (insert_idx < alloc->free_count &&
         alloc->free_regions[insert_idx].base < base) {
    ++insert_idx;
  }

  // Check overlap with previous region, if any.
  if (insert_idx > 0) {
    lmem_region_t *prev = &alloc->free_regions[insert_idx - 1];
    if (prev->base + prev->size > base) {
      return -1;  // Overlaps previous free region
    }
  }

  // Check overlap with next region, if any.
  if (insert_idx < alloc->free_count) {
    lmem_region_t *next = &alloc->free_regions[insert_idx];
    if (base + size > next->base) {
      return -1;  // Overlaps next free region
    }
  }

  // Try to merge with previous region.
  if (insert_idx > 0) {
    lmem_region_t *prev = &alloc->free_regions[insert_idx - 1];

    if (prev->base + prev->size == base) {
      // Extend previous region to cover this freed block.
      prev->size += size;

      // After merging with prev, we may also become adjacent to next.
      if (insert_idx < alloc->free_count) {
        lmem_region_t *next = &alloc->free_regions[insert_idx];

        if (prev->base + prev->size == next->base) {
          // Merge prev + freed + next into one region.
          prev->size += next->size;

          // Remove next from the array by shifting left.
          for (size_t i = insert_idx; i < alloc->free_count - 1; ++i) {
            alloc->free_regions[i] = alloc->free_regions[i + 1];
          }

          alloc->free_count--;
          alloc->free_regions[alloc->free_count].base = 0;
          alloc->free_regions[alloc->free_count].size = 0;
        }
      }

      return 0;
    }
  }

  // Try to merge with next region only.
  if (insert_idx < alloc->free_count) {
    lmem_region_t *next = &alloc->free_regions[insert_idx];

    if (base + size == next->base) {
      // Extend next backward to include this freed block.
      next->base = base;
      next->size += size;
      return 0;
    }
  }

  // No merge possible; insert a new free region.
  if (alloc->free_count >= alloc->free_capacity) {
    return -1;  // No slot available for a new free region
  }

  // Shift right to make room for the new region.
  for (size_t i = alloc->free_count; i > insert_idx; --i) {
    alloc->free_regions[i] = alloc->free_regions[i - 1];
  }

  alloc->free_regions[insert_idx].base = base;
  alloc->free_regions[insert_idx].size = size;
  alloc->free_count++;

  return 0;
}

int cp_lmem_allocator_init(void) {
  for (size_t i = 0; i < g_num_sm; ++i) {
    const size_t resident_limit = cp_hwinfo_sm_wg_resident_limit(i);
    if (resident_limit == 0 || resident_limit > FORMOSA_MAX_WG_RESIDENT_LIMIT) {
      fprintf(stderr,
              "\033[31mSM%lu WG_RESIDENT_LIMIT=%lu exceeds local-memory "
              "metadata capacity=%d\033[0m\r\n",
              (unsigned long)i, (unsigned long)resident_limit,
              FORMOSA_MAX_WG_RESIDENT_LIMIT);
      return -1;
    }

    // Initialize each SM with one big free region covering the whole
    // per-SM local memory space.
    memset(g_lmem_free_regions[i], 0, sizeof(g_lmem_free_regions[i]));
    memset(g_lmem_active_allocs[i], 0, sizeof(g_lmem_active_allocs[i]));

    g_lmem_allocators[i].free_capacity = resident_limit + 1;
    g_lmem_allocators[i].free_count = 1;
    g_lmem_allocators[i].free_regions = g_lmem_free_regions[i];

    g_lmem_allocators[i].active_capacity = resident_limit;
    g_lmem_allocators[i].active_count = 0;
    g_lmem_allocators[i].active_allocs = g_lmem_active_allocs[i];

    // Initialize the single free region for this SM.
    g_lmem_allocators[i].free_regions[0].base = PER_SM_LOCAL_MEM_BASE;
    g_lmem_allocators[i].free_regions[0].size = PER_SM_LOCAL_MEM_SIZE;
    g_lmem_allocators[i].usage_bytes = 0;
    DTRACE_COUNTER(cp_trace_lmem_usage_idx(i), 0);
  }

  return 0;
}

void cp_lmem_allocator_deinit(void) {
  for (size_t i = 0; i < g_num_sm; ++i) {
    g_lmem_allocators[i].free_regions = NULL;
    g_lmem_allocators[i].free_count = 0;
    g_lmem_allocators[i].free_capacity = 0;

    g_lmem_allocators[i].active_allocs = NULL;
    g_lmem_allocators[i].active_count = 0;
    g_lmem_allocators[i].active_capacity = 0;
    g_lmem_allocators[i].usage_bytes = 0;
  }
}

int cp_lmem_allocator_alloc(size_t sm_id, uint64_t size, uint64_t *base_out) {
  if (sm_id >= g_num_sm || size == 0 || base_out == NULL) {
    return -1;  // Invalid input
  }

  lmem_allocator_t *allocator = &g_lmem_allocators[sm_id];

  // Round requested size up to allocator alignment.
  uint64_t req_size = align_up_u64(size, LMEM_ALLOC_ALIGNMENT);

  // Reject requests that do not fit in one SM after alignment.
  if (req_size > PER_SM_LOCAL_MEM_SIZE) {
    return -1;
  }

  int rc = -1;
  uint64_t alloc_base = 0;

  CP_LMEM_ALLOCATOR_ENTER_CRITICAL();

  // Hardware resident limit: cannot exceed the number of active threadblocks
  // allowed on this SM.
  if (allocator->active_count >= allocator->active_capacity) {
    goto out;
  }

  // First-fit policy:
  // choose the first free region that is large enough
  size_t target_idx = (size_t)-1;

  for (size_t i = 0; i < allocator->free_count; ++i) {
    if (allocator->free_regions[i].size >= req_size) {
      target_idx = i;
      break;
    }
  }

  if (target_idx == (size_t)-1) {
    goto out;  // No free region can satisfy this request
  }

  lmem_region_t *region = &allocator->free_regions[target_idx];
  alloc_base = region->base;

  if (region->size == req_size) {
    // Exact fit: remove this free region entirely.
    for (size_t i = target_idx; i < allocator->free_count - 1; ++i) {
      allocator->free_regions[i] = allocator->free_regions[i + 1];
    }

    allocator->free_count--;
    allocator->free_regions[allocator->free_count].base = 0;
    allocator->free_regions[allocator->free_count].size = 0;
  } else {
    // Allocate from the front of the free region.
    // The remainder stays as a smaller free region.
    region->base += req_size;
    region->size -= req_size;
  }

  // Find an empty active-allocation slot to track this allocation.
  size_t slot = (size_t)-1;
  for (size_t i = 0; i < allocator->active_capacity; ++i) {
    if (!allocator->active_allocs[i].valid) {
      slot = i;
      break;
    }
  }

  if (slot == (size_t)-1) {
    // Should not happen because active_count was checked earlier.
    // Roll back by reinserting the allocated region into the free list.
    (void)free_region_insert_and_coalesce(allocator, alloc_base, req_size);
    goto out;
  }

  // Record this allocation in the active table.
  allocator->active_allocs[slot].base = alloc_base;
  allocator->active_allocs[slot].size = req_size;
  allocator->active_allocs[slot].valid = 1;
  allocator->active_count++;
  allocator->usage_bytes += req_size;
  DTRACE_COUNTER(cp_trace_lmem_usage_idx(sm_id), (int)allocator->usage_bytes);

  rc = 0;

out:
  CP_LMEM_ALLOCATOR_EXIT_CRITICAL();

  if (rc == 0) {
    *base_out = alloc_base;
  }
  return rc;
}

int cp_lmem_allocator_free(size_t sm_id, uint64_t base) {
  if (sm_id >= g_num_sm) {
    return -1;  // Invalid SM id
  }

  lmem_allocator_t *alloc = &g_lmem_allocators[sm_id];

  int rc = -1;
  size_t slot = (size_t)-1;
  uint64_t size = 0;

  CP_LMEM_ALLOCATOR_ENTER_CRITICAL();

  // Look up the active allocation by base address.
  // Only allocations currently tracked in active_allocs[] may be freed.
  for (size_t i = 0; i < alloc->active_capacity; ++i) {
    if (alloc->active_allocs[i].valid && alloc->active_allocs[i].base == base) {
      slot = i;
      size = alloc->active_allocs[i].size;
      break;
    }
  }

  if (slot == (size_t)-1) {
    goto out;  // Unknown base or double free
  }

  if (free_region_insert_and_coalesce(alloc, base, size) != 0) {
    goto out;  // Failed to insert back into free list (should not happen)
  }

  // Remove the allocation from the active table first.
  alloc->active_allocs[slot].base = 0;
  alloc->active_allocs[slot].size = 0;
  alloc->active_allocs[slot].valid = 0;
  alloc->active_count--;
  alloc->usage_bytes -= size;
  DTRACE_COUNTER(cp_trace_lmem_usage_idx(sm_id), (int)alloc->usage_bytes);

  rc = 0;

out:
  CP_LMEM_ALLOCATOR_EXIT_CRITICAL();
  return rc;
}
