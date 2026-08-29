/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_LMEM_ALLOCATOR_H
#define CP_LMEM_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cp_hwinfo.h"

// 256-byte alignment for local memory allocations
// Must be a power of 2 for the align_up_u64 function to work correctly
#define LMEM_ALLOC_ALIGNMENT 256ULL

static inline size_t get_lmem_max_regions(size_t sm_id) {
  // The maximum number of resident workgroups on this SM, plus one extra region
  // for the allocator to track the remaining free memory after all workgroups'
  // allocations.
  return cp_hwinfo_sm_wg_resident_limit(sm_id) + 1;
}

typedef struct {
  uint64_t base;
  uint64_t size;
} lmem_region_t;

_Static_assert(sizeof(lmem_region_t) == 16,
               "local-memory free-region metadata size changed");

typedef struct {
  uint64_t base;
  uint64_t size;
  uint8_t valid;
} lmem_active_alloc_t;

_Static_assert(sizeof(lmem_active_alloc_t) == 24,
               "local-memory active-allocation metadata size changed");

typedef struct {
  lmem_region_t *free_regions;
  size_t free_count;
  size_t free_capacity;

  lmem_active_alloc_t *active_allocs;
  size_t active_count;
  size_t active_capacity;

  uint64_t usage_bytes;
} lmem_allocator_t;

/**
 * Initializes the local memory allocator for all SMs.
 * @return 0 on success, -1 on failure.
 */
int cp_lmem_allocator_init(void);

/**
 * Allocates local memory for a given SM.
 * @param sm_id The SM ID.
 * @param size The size of the memory to allocate.
 * @param base_out A pointer to store the base address of the allocated memory.
 * @return 0 on success, -1 on failure.
 */
int cp_lmem_allocator_alloc(size_t sm_id, uint64_t size, uint64_t *base_out);

/**
 * Frees local memory for a given SM.
 * @param sm_id The SM ID.
 * @param base The base address of the memory to free.
 * @return 0 on success, -1 on failure.
 */
int cp_lmem_allocator_free(size_t sm_id, uint64_t base);

/**
 * Deinitializes the local memory allocator and frees any allocated resources.
 * Should be called when the allocator is no longer needed to prevent memory
 * leaks.
 * @return void
 */
void cp_lmem_allocator_deinit(void);

#endif  // CP_LMEM_ALLOCATOR_H
