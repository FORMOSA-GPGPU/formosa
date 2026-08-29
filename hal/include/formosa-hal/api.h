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

#define FSA_FIRMWARE_FAULT_NONE 0
#define FSA_FIRMWARE_FAULT_MEMORY_COPY_TIMEOUT 1
#define FSA_FIRMWARE_FAULT_DMA_PROTOCOL 2
#define FSA_FIRMWARE_FAULT_CACHE_TIMEOUT 3
#define FSA_FIRMWARE_FAULT_BOOT_DESCRIPTOR 4
#define FSA_FIRMWARE_FAULT_BOOT_DMA 5

#define FSA_FIRMWARE_STATUS_RESET 0
#define FSA_FIRMWARE_STATUS_BOOTING 1
#define FSA_FIRMWARE_STATUS_READY 2
#define FSA_FIRMWARE_STATUS_FAULT 3

#define FSA_MEMORY_TRANSFER_ABI_VERSION 2u
#define FSA_COMMAND_ABI_VERSION_V3 3u

#define FSA_COMPLETION_TOKEN_BYTES 8u
#define FSA_COMPLETION_SLOT_BYTES 8ull
#define FSA_COMPLETION_SLOT_COUNT 64u

#define FSA_COMPLETION_BOOT_GENERATION_SHIFT 48u
#define FSA_COMPLETION_ALLOC_GENERATION_SHIFT 16u
#define FSA_COMPLETION_SLOT_INDEX_SHIFT 0u

#define FSA_COMPLETION_BOOT_GENERATION_MASK UINT64_C(0x0000FFFF)
#define FSA_COMPLETION_ALLOC_GENERATION_MASK UINT64_C(0x00000000FFFFFFFF)
#define FSA_COMPLETION_SLOT_INDEX_MASK UINT64_C(0x000000000000FFFF)
#define FSA_COMPLETION_ALLOC_TAG_MASK UINT64_C(0x0000FFFFFFFFFFFF)

#define FSA_COMPLETION_TOKEN_BOOT_GENERATION_MASK UINT64_C(0xFFFF000000000000)
#define FSA_COMPLETION_TOKEN_ALLOC_GENERATION_MASK UINT64_C(0x0000FFFFFFFF0000)
#define FSA_COMPLETION_TOKEN_SLOT_INDEX_MASK UINT64_C(0x000000000000FFFF)
#define FSA_COMPLETION_TOKEN_ALLOC_TAG_MASK UINT64_C(0xFFFFFFFFFFFF0000)

#define FSA_COMPLETION_RESULT_PENDING UINT16_C(0x0000)
#define FSA_COMPLETION_RESULT_SUCCESS UINT16_C(0x0001)
#define FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN UINT16_C(0x0002)
#define FSA_COMPLETION_RESULT_COMMAND_FAILURE_MAX UINT16_C(0xFFFE)
#define FSA_COMPLETION_RESULT_FIRMWARE_REBOOT UINT16_C(0xFFFF)
#define FSA_COMPLETION_RESULT_MASK UINT64_C(0x000000000000FFFF)
#define FSA_COMPLETION_ALLOC_GENERATION_INITIAL UINT32_C(1)

typedef uint64_t FsaCompletionToken;
typedef uint64_t FsaCompletionAllocTag;
typedef uint64_t FsaCompletionSlot;
typedef uint16_t FsaCompletionResult;
typedef uint64_t FsaDeviceAddress;
typedef uint64_t FsaMemoryAddress;

static inline FsaCompletionToken fsa_completion_token_make(
    uint64_t boot_generation, uint32_t alloc_generation, uint16_t slot_index) {
  if (alloc_generation == 0u) {
    alloc_generation = FSA_COMPLETION_ALLOC_GENERATION_INITIAL;
  }

  return (((uint64_t)boot_generation & FSA_COMPLETION_BOOT_GENERATION_MASK)
          << FSA_COMPLETION_BOOT_GENERATION_SHIFT) |
         (((uint64_t)alloc_generation & FSA_COMPLETION_ALLOC_GENERATION_MASK)
          << FSA_COMPLETION_ALLOC_GENERATION_SHIFT) |
         (((uint64_t)slot_index & FSA_COMPLETION_SLOT_INDEX_MASK)
          << FSA_COMPLETION_SLOT_INDEX_SHIFT);
}

static inline uint16_t fsa_completion_token_boot_generation(
    FsaCompletionToken token) {
  return (uint16_t)((token & FSA_COMPLETION_TOKEN_BOOT_GENERATION_MASK) >>
                    FSA_COMPLETION_BOOT_GENERATION_SHIFT);
}

static inline uint32_t fsa_completion_token_alloc_generation(
    FsaCompletionToken token) {
  return (uint32_t)((token & FSA_COMPLETION_TOKEN_ALLOC_GENERATION_MASK) >>
                    FSA_COMPLETION_ALLOC_GENERATION_SHIFT);
}

static inline uint16_t fsa_completion_token_slot_index(
    FsaCompletionToken token) {
  return (uint16_t)((token & FSA_COMPLETION_TOKEN_SLOT_INDEX_MASK) >>
                    FSA_COMPLETION_SLOT_INDEX_SHIFT);
}

static inline FsaCompletionAllocTag fsa_completion_token_alloc_tag(
    FsaCompletionToken token) {
  return (
      FsaCompletionAllocTag)((token & FSA_COMPLETION_TOKEN_ALLOC_TAG_MASK) >>
                             FSA_COMPLETION_ALLOC_GENERATION_SHIFT);
}

static inline int fsa_completion_token_has_valid_slot(
    FsaCompletionToken token) {
  return token != 0 && fsa_completion_token_alloc_generation(token) != 0 &&
         fsa_completion_token_slot_index(token) < FSA_COMPLETION_SLOT_COUNT;
}

static inline FsaCompletionSlot fsa_completion_slot_encode(
    FsaCompletionAllocTag alloc_tag, FsaCompletionResult result) {
  return ((alloc_tag & FSA_COMPLETION_ALLOC_TAG_MASK)
          << FSA_COMPLETION_ALLOC_GENERATION_SHIFT) |
         (uint64_t)result;
}

static inline FsaCompletionAllocTag fsa_completion_slot_alloc_tag(
    FsaCompletionSlot slot) {
  return (FsaCompletionAllocTag)((slot & FSA_COMPLETION_TOKEN_ALLOC_TAG_MASK) >>
                                 FSA_COMPLETION_ALLOC_GENERATION_SHIFT);
}

static inline FsaCompletionResult fsa_completion_slot_result(
    FsaCompletionSlot slot) {
  return (FsaCompletionResult)(slot & FSA_COMPLETION_RESULT_MASK);
}

static inline uint32_t fsa_completion_next_alloc_generation(
    uint32_t alloc_generation) {
  return alloc_generation == 0u || alloc_generation == UINT32_MAX
             ? FSA_COMPLETION_ALLOC_GENERATION_INITIAL
             : alloc_generation + 1u;
}

typedef enum {
  kMemoryDomainHost = 1,
  kMemoryDomainDevice = 2,
} MemoryDomain;

/* Memory-copy command failures occupy the shared 16-bit outcome space.  The
 * common Pending, Success, and FirmwareReboot values are named only through
 * FsaCompletionResult. */
typedef enum {
  kMemoryCopyStatusInvalidDomainPair = 2,
  kMemoryCopyStatusInvalidAddress = 3,
  kMemoryCopyStatusInvalidRange = 4,
  kMemoryCopyStatusOverlap = 5,
  kMemoryCopyStatusDmaBusError = 6,
  kMemoryCopyStatusInternalError = 7,
  kMemoryCopyStatusNotReady = 8,
  kMemoryCopyStatusTimeout = 9,
} MemoryCopyStatus;

typedef enum {
  kFirmwareFaultNone = FSA_FIRMWARE_FAULT_NONE,
  kFirmwareFaultMemoryCopyTimeout = FSA_FIRMWARE_FAULT_MEMORY_COPY_TIMEOUT,
  kFirmwareFaultDmaProtocol = FSA_FIRMWARE_FAULT_DMA_PROTOCOL,
  kFirmwareFaultCacheTimeout = FSA_FIRMWARE_FAULT_CACHE_TIMEOUT,
  kFirmwareFaultBootDescriptor = FSA_FIRMWARE_FAULT_BOOT_DESCRIPTOR,
  kFirmwareFaultBootDma = FSA_FIRMWARE_FAULT_BOOT_DMA,
} FirmwareFault;

typedef enum {
  kFirmwareStatusReset = FSA_FIRMWARE_STATUS_RESET,
  kFirmwareStatusBooting = FSA_FIRMWARE_STATUS_BOOTING,
  kFirmwareStatusReady = FSA_FIRMWARE_STATUS_READY,
  kFirmwareStatusFault = FSA_FIRMWARE_STATUS_FAULT,
} FirmwareStatus;

typedef enum {
  kFsaCommandSubmitAccepted = 0,
  kFsaCommandSubmitWouldBlock = 1,
  kFsaCommandSubmitInvalidArgument = -1,
  kFsaCommandSubmitTransportError = -2,
} FsaCommandSubmitStatus;

typedef enum {
  kFsaCompletionPollTerminal = 0,
  kFsaCompletionPollPending = 1,
  kFsaCompletionPollInvalidToken = -1,
  kFsaCompletionPollTransportError = -2,
} FsaCompletionPollStatus;

typedef enum {
  kFsaCompletionWaitSuccess = 0,
  kFsaCompletionWaitTimeout = -1,
  kFsaCompletionWaitInvalidToken = -2,
  kFsaCompletionWaitTransportError = -3,
} FsaCompletionWaitStatus;

typedef enum {
  kFsaCompletionReleaseAccepted = 0,
  kFsaCompletionReleaseInvalidToken = -1,
  kFsaCompletionReleaseTransportError = -2,
  kFsaCompletionReleasePending = -3,
} FsaCompletionReleaseStatus;

typedef struct {
  FsaMemoryAddress address;
  uint64_t size;
} FsaMemoryRange;

typedef struct {
  MemoryDomain domain;
  FsaMemoryRange range;
} FsaMemoryRegion;

typedef struct {
  uint32_t struct_size;
  uint32_t dimensions;
  uint32_t local_size[3];
  uint32_t num_groups[3];
  uint64_t global_offset[3];
  uint32_t local_mem_size;
  uint32_t has_printf_meta;
  uint32_t enable_stack_remap;
  FsaDeviceAddress kernel_entry;
  FsaDeviceAddress kernarg_address;
  FsaDeviceAddress kernel_trampoline;
  FsaDeviceAddress kernel_status;
} FsaKernelLaunchInfo;

typedef struct {
  uint32_t struct_size;
  FsaMemoryRegion source;
  FsaMemoryRegion destination;
} FsaMemoryCopyInfo;

/**
 * Enqueue a host kernel dispatch with a shared Completion Token.
 * \return FsaCommandSubmitStatus.
 */
FsaCommandSubmitStatus fsa_cmd_start_kernel(const FsaKernelLaunchInfo *info,
                                            FsaCompletionToken *completion);

FsaCommandSubmitStatus fsa_cmd_cache_flush(const FsaMemoryRange *range,
                                           FsaCompletionToken *completion);

FsaCommandSubmitStatus fsa_cmd_cache_invalidate(const FsaMemoryRange *range,
                                                FsaCompletionToken *completion);

/**
 * Enqueue a firmware-managed memory-copy command.
 *
 * The firmware treats this command as an implicit barrier: preceding device
 * work drains, the relevant cache ranges are maintained, the appropriate DMA
 * engine performs the copy, and the destination range is made coherent before
 * completion.
 * \param info Source and destination Memory Regions for the copy.
 * \param completion On Accepted, receives an owned Completion Token. On
 * TransportError after write-pointer publication was attempted, may also
 * receive a token that remains owned until reset reclaims it; otherwise 0.
 * \return FsaCommandSubmitStatus.
 */
FsaCommandSubmitStatus fsa_cmd_memory_copy(const FsaMemoryCopyInfo *info,
                                           FsaCompletionToken *completion);

/**
 * Submit a host-to-device memory-copy command and wait for terminal success.
 * Convenience around fsa_cmd_memory_copy and fsa_wait_completion.
 * \return 0 on success, -1 on failure.
 */
int fsa_copy_to_dev(uintptr_t dev_addr, const void *host_ptr, size_t size);

/**
 * Submit a device-to-host memory-copy command and wait for terminal success.
 * Convenience around fsa_cmd_memory_copy and fsa_wait_completion.
 * \return 0 on success, -1 on failure.
 */
int fsa_copy_from_dev(uintptr_t dev_addr, void *host_ptr, size_t size);

/** Wait for any ABI v3 completion and auto-release a terminal slot.
 *  Success means a terminal result was observed and released; check *result
 *  for command success vs command-specific failure / FirmwareReboot. */
FsaCompletionWaitStatus fsa_wait_completion(FsaCompletionToken token,
                                            uint64_t timeout_ms,
                                            FsaCompletionResult *result);

/** Poll any ABI v3 completion without releasing a terminal slot. */
FsaCompletionPollStatus fsa_poll_completion(FsaCompletionToken token,
                                            FsaCompletionResult *result);

/** Release a terminal ABI v3 completion slot. */
FsaCompletionReleaseStatus fsa_release_completion(FsaCompletionToken token);

/**
 * Barrier: Success is published only after every preceding Host command is
 * terminal. Following commands cannot pass while predecessors remain active.
 * \param completion On Accepted, receives an owned Completion Token.
 * \return FsaCommandSubmitStatus.
 */
FsaCommandSubmitStatus fsa_cmd_barrier(FsaCompletionToken *completion);

#ifdef __cplusplus
}
#endif
