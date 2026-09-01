/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __ASSEMBLER__
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
#else

#include <formosa-hal/api.h>
#include <stddef.h>
#include <stdint.h>

#define FSA_COMMAND_PACKET_BYTES 64u
#define FSA_COMPLETION_TOKEN_OFFSET \
  (FSA_COMMAND_PACKET_BYTES - FSA_COMPLETION_TOKEN_BYTES)

#if defined(__cplusplus)
#define FSA_STATIC_ASSERT static_assert
#define FSA_ALIGNOF alignof
#else
#define FSA_STATIC_ASSERT _Static_assert
#define FSA_ALIGNOF _Alignof
#endif

typedef enum {
  kKernelDispatchPacketHeader = 1,
  kCacheFlushPacketHeader,
  kCacheInvalidatePacketHeader,
  kBarrierPacketHeader,
  // Header 5 is retired and intentionally unassigned.
  kMemoryCopyPacketHeader = 6
} PacketHeader;

#define FSA_KERNEL_DISPATCH_DIM_MASK 0x3u
#define FSA_KERNEL_DISPATCH_HAS_PRINTF_META (1u << 2)
#define FSA_KERNEL_DISPATCH_STACK_REMAP (1u << 3)

typedef struct {
  // [15:0]   - header
  uint16_t header;

  // [17:16]  - dim,  [31:18] - dispatch flags
  uint16_t dim_reserved0;

  // [47:32]  - local_size_x
  uint16_t local_size_x;

  // [63:48]  - local_size_y
  uint16_t local_size_y;

  // [79:64]  - local_size_z
  uint16_t local_size_z;

  // [95:80]  - local_mem_size
  uint16_t local_mem_size;

  // [111:96]  - num_groups_x
  uint16_t num_groups_x;

  // [127:112] - num_groups_y
  uint16_t num_groups_y;

  // [143:128] - num_groups_z
  uint16_t num_groups_z;

  // [159:144] - global_offset_x
  uint16_t global_offset_x;

  // [175:160] - global_offset_y
  uint16_t global_offset_y;

  // [191:176] - global_offset_z
  uint16_t global_offset_z;

  // [255:192] - kernel_object
  uint64_t kernel_object;

  // [319:256] - kernarg_address
  uint64_t kernarg_address;

  // [383:320] - kernel_trampoline
  uint64_t kernel_trampoline;

  // [447:384] - kernel_status
  uint64_t kernel_status;

  // [511:448] - Host ABI v3 Completion Token.
  uint64_t completion_token;
} KernelDispatchPacket;

typedef struct {
  // [15:0]   - header
  uint16_t header;

  // [63:16]   - reserved0
  uint8_t reserved0[6];

  // [127:64]   - addr
  uint64_t addr;

  // [191:128]  - size
  uint64_t size;

  // [447:192] - reserved1
  uint8_t reserved1[32];

  // [511:448] - ABI v3 completion token (0 for firmware-internal ops)
  uint64_t completion_token;
} CacheOperationPacket;

typedef struct {
  // [15:0] - header
  uint16_t header;

  // [23:16] - source memory domain
  uint8_t src_domain;

  // [31:24] - destination memory domain
  uint8_t dst_domain;

  // [63:32] - reserved0
  uint8_t reserved0[4];

  // [127:64] - source DMA-visible address
  uint64_t src_addr;

  // [191:128] - destination DMA-visible address
  uint64_t dst_addr;

  // [255:192] - byte count
  uint64_t size;

  // [447:256] - reserved for ABI v3 command-specific data
  uint8_t reserved1[24];

  // [511:448] - ABI v3 completion token
  uint64_t completion_token;
} MemoryCopyPacket;

typedef struct {
  // [15:0]   - header
  uint16_t header;

  // [447:16]  - reserved
  uint8_t reserved[54];

  // [511:448] - ABI v3 Completion Token (Barrier)
  uint64_t completion_token;
} HeaderOnlyPacket;

union Packet {
  uint8_t raw[FSA_COMMAND_PACKET_BYTES];
  KernelDispatchPacket kernel_dispatch_packet;
  CacheOperationPacket cache_operation_packet;
  MemoryCopyPacket memory_copy_packet;
  HeaderOnlyPacket header_only_packet;
};

FSA_STATIC_ASSERT((FsaCompletionResult)kMemoryCopyStatusInvalidDomainPair >=
                      FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN,
                  "memory-copy failure below command-failure range");
FSA_STATIC_ASSERT((FsaCompletionResult)kMemoryCopyStatusTimeout <=
                      FSA_COMPLETION_RESULT_COMMAND_FAILURE_MAX,
                  "memory-copy failure above command-failure range");
FSA_STATIC_ASSERT((FsaCompletionResult)kMemoryCopyStatusTimeout !=
                      FSA_COMPLETION_RESULT_FIRMWARE_REBOOT,
                  "memory-copy timeout collides with FirmwareReboot");
FSA_STATIC_ASSERT((FsaCompletionResult)kKernelCompletionBadDimension >=
                      FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN,
                  "kernel failure below command-failure range");
FSA_STATIC_ASSERT((FsaCompletionResult)kKernelCompletionUnknownError <=
                      FSA_COMPLETION_RESULT_COMMAND_FAILURE_MAX,
                  "kernel failure above command-failure range");
FSA_STATIC_ASSERT((FsaCompletionResult)kKernelCompletionUnknownError !=
                      FSA_COMPLETION_RESULT_FIRMWARE_REBOOT,
                  "kernel unknown error collides with FirmwareReboot");
FSA_STATIC_ASSERT(sizeof(FsaCompletionToken) == FSA_COMPLETION_TOKEN_BYTES,
                  "completion token ABI size mismatch");
FSA_STATIC_ASSERT(FSA_ALIGNOF(FsaCompletionToken) == FSA_COMPLETION_TOKEN_BYTES,
                  "completion token alignment mismatch");
FSA_STATIC_ASSERT(sizeof(FsaCompletionSlot) == FSA_COMPLETION_SLOT_BYTES,
                  "completion slot ABI size mismatch");
FSA_STATIC_ASSERT(FSA_ALIGNOF(FsaCompletionSlot) == FSA_COMPLETION_SLOT_BYTES,
                  "completion slot alignment mismatch");
FSA_STATIC_ASSERT(sizeof(FsaCompletionResult) == sizeof(uint16_t),
                  "completion result ABI width mismatch");
FSA_STATIC_ASSERT(FSA_COMPLETION_TOKEN_OFFSET == 56u,
                  "completion token ABI offset mismatch");
FSA_STATIC_ASSERT((FSA_COMPLETION_TOKEN_ALLOC_TAG_MASK &
                   FSA_COMPLETION_RESULT_MASK) == 0u,
                  "completion slot result overlaps alloc tag");
FSA_STATIC_ASSERT(sizeof(union Packet) == FSA_COMMAND_PACKET_BYTES,
                  "command packet ABI size mismatch");
FSA_STATIC_ASSERT(offsetof(KernelDispatchPacket, completion_token) ==
                      FSA_COMPLETION_TOKEN_OFFSET,
                  "kernel completion field ABI offset mismatch");
FSA_STATIC_ASSERT(offsetof(CacheOperationPacket, completion_token) ==
                      FSA_COMPLETION_TOKEN_OFFSET,
                  "cache completion field ABI offset mismatch");
FSA_STATIC_ASSERT(offsetof(MemoryCopyPacket, completion_token) ==
                      FSA_COMPLETION_TOKEN_OFFSET,
                  "memory-copy completion field ABI offset mismatch");
FSA_STATIC_ASSERT(sizeof(HeaderOnlyPacket) - FSA_COMPLETION_TOKEN_BYTES ==
                      FSA_COMPLETION_TOKEN_OFFSET,
                  "header-only completion field ABI offset mismatch");

#undef FSA_STATIC_ASSERT
#undef FSA_ALIGNOF

#endif /* __ASSEMBLER__ */
