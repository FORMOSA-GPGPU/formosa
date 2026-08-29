/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocate memory on device with designated address.
 * \param dev_addr Device address to allocate memory.
 * \param size Size of memory to allocate.
 * \return 0 on success, -1 on failure.
 */
int fsa_addr_malloc(uintptr_t dev_addr, size_t size);

/**
 * Allocate memory on device.
 * \param dev_addr Pointer to device address to allocate memory.
 * \param size Size of memory to allocate.
 * \return 0 on success, -1 on failure.
 */
int fsa_malloc(void **dev_addr, size_t size);

/**
 * Allocate non-cacheable memory on device.
 * This is intended for small producer/firmware synchronization metadata that
 * must be visible to the command processor without relying on SM D-cache
 * writeback. Payload buffers should generally stay cacheable and be made
 * visible with explicit cache operations.
 */
int fsa_malloc_noncache(void **dev_addr, size_t size);

/**
 * Free memory on device.
 * \param dev_addr Device address to free memory.
 * \return 0 on success, -1 on failure.
 */
int fsa_free(void *dev_addr);

/* Return non-zero while the initialized Host transport and firmware session
 * can accept runtime work. A transport failure makes the session unavailable
 * until it is cleaned up and initialized again. */
int fsa_hal_is_available();

/**
 * Probe for available devices.
 * \return 0 on device found, -1 on no device found.
 */
int fsa_probe();

/**
 * Capabilities of one successfully initialized Formosa device.
 *
 * The caller owns this copy. It is written only when fsa_hal_init() succeeds.
 */
typedef struct {
  uint64_t num_cores;
  uint64_t max_threads_per_work_group;
  uint64_t local_mem_size_per_core;
  uint64_t cache_size;
  uint64_t cache_line_size;
  uint64_t global_mem_size;
  uint64_t max_allocation_size;
} FsaDeviceDescription;

/**
 * Initialize HAL.
 * \param description Caller-owned output for the initialized device
 *                    capabilities.
 * \return 0 on success, -1 on failure.
 */
int fsa_hal_init(FsaDeviceDescription *description);

/**
 * Check whether one work-group fits the Formosa Core occupancy limits.
 * \param group_size Number of threads in the work-group.
 * \param local_mem_per_group Local memory requested by one work-group.
 * \param max_local_mem Optional output for the largest request that preserves
 *                      the thread-limited occupancy.
 * \return 0 when dispatch is feasible, nonzero otherwise.
 */
int fsa_hal_check_occupancy(uint64_t group_size, uint64_t local_mem_per_group,
                            uint64_t *max_local_mem);

/**
 * Reset firmware and establish a new boot generation while preserving old
 * completion handles so they can be reported as device-reset failures.
 */
int fsa_hal_reset(uint64_t timeout_ms);

/**
 * Cleanup HAL.
 * \return 0 on success, -1 on failure.
 */
int fsa_hal_cleanup();

enum KernelStatusCode : uint64_t {
  kKernelOkay = 0,
  kKernelBadDimension = 1,
  kKernelException = 2,
  kKernelUnknownError = 3
};

typedef struct {
  enum KernelStatusCode code;
  uint64_t mcause;
  uint64_t mepc;
  uint64_t mtval;
} KernelStatus;

#ifdef __cplusplus
}
#endif
