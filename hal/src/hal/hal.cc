// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <formosa-hal/api.h>
#include <formosa-hal/hal.h>
#include <formosa_addr_map.h>
#include <real/real.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "command_packet.h"
#include "common/util.h"
#include "falloc.h"
#include "gpufw.elf.h"
#include "hal-capture-recorder.h"

static std::vector<Packet> host_cmd_ring;
static uint32_t host_rd_ptr;
static uint32_t host_wr_ptr;
static uint64_t dev_cmd_ring_base;
static uint32_t next_alloc_generation = FSA_COMPLETION_ALLOC_GENERATION_INITIAL;
static uint64_t runtime_boot_generation = 0;
static uint64_t boot_generation_before_reset = 0;
static std::atomic_bool firmware_ready = false;
static std::atomic_bool transport_failed = false;
static std::atomic_bool firmware_reboot_failed = false;
static bool noncache_alloc_ready = false;
static const formosa::real::ConfigurationSnapshot *active_configuration =
    nullptr;
static std::mutex command_mtx;

/* Shared completion-pool ownership. */
struct MemoryCopyCaptureMetadata {
  uint64_t src_addr = 0;
  uint64_t dst_addr = 0;
  uint64_t size = 0;
};
struct CompletionSlotMetadata {
  bool in_use = false;
  FsaCompletionToken token = 0;
  MemoryCopyCaptureMetadata capture;
};
static std::vector<CompletionSlotMetadata> completion_slots;
static std::vector<FsaCompletionToken> stale_completion_tokens;

static_assert(FSA_COMPLETION_SLOT_COUNT == FSA_COMPLETION_POOL_ENTRIES,
              "ABI slot count must match address-map pool capacity");

namespace {

formosa::hal::CaptureRecorder recorder;

bool checked_add(uint64_t lhs, uint64_t rhs, uint64_t &result) {
  if (lhs > std::numeric_limits<uint64_t>::max() - rhs) return false;
  result = lhs + rhs;
  return true;
}

bool checked_mul(uint64_t lhs, uint64_t rhs, uint64_t &result) {
  if (rhs != 0 && lhs > std::numeric_limits<uint64_t>::max() / rhs)
    return false;
  result = lhs * rhs;
  return true;
}

bool is_power_of_two(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

bool derive_device_description(
    const formosa::real::ConfigurationSnapshot &config,
    FsaDeviceDescription &description) {
  if (config.num_cores == 0 || config.num_cores > FSA_SM_MMIO_MAX_SMS ||
      config.threads_per_warp == 0 || config.warps_per_core == 0 ||
      config.local_mem_size == 0 || config.shared_cache_size == 0 ||
      config.cache_line_size == 0 || config.global_mem_size == 0 ||
      config.global_mem_noncache_alloc_size == 0 ||
      config.stack_size_per_thread == 0 ||
      !is_power_of_two(config.cache_line_size) ||
      config.cache_line_size > config.shared_cache_size ||
      config.shared_cache_size % config.cache_line_size != 0) {
    return false;
  }

  uint64_t threads_per_core = 0;
  uint64_t total_threads = 0;
  uint64_t global_mem_end = 0;
  uint64_t noncache_end = 0;
  uint64_t fsa_mmio_end = 0;
  uint64_t clint_end = 0;
  uint64_t stack_end = 0;
  uint64_t stack_bytes = 0;
  if (!checked_mul(config.threads_per_warp, config.warps_per_core,
                   threads_per_core) ||
      !checked_mul(threads_per_core, config.num_cores, total_threads) ||
      !checked_add(config.global_mem_base, config.global_mem_size,
                   global_mem_end) ||
      !checked_add(config.global_mem_noncache_alloc_base,
                   config.global_mem_noncache_alloc_size, noncache_end) ||
      !checked_add(config.fsa_mmio_base, FSA_MMIO_SIZE, fsa_mmio_end) ||
      !checked_add(config.clint_base, FSA_CLINT_SIZE, clint_end) ||
      !checked_mul(total_threads, config.stack_size_per_thread, stack_bytes) ||
      !checked_add(FSA_STACK_BASE, FSA_STACK_POOL_SIZE, stack_end)) {
    return false;
  }

  if (config.global_mem_alloc_base < config.global_mem_base ||
      config.global_mem_alloc_base < stack_end ||
      config.global_mem_alloc_base >= global_mem_end ||
      config.global_mem_noncache_alloc_base < config.global_mem_base ||
      noncache_end > global_mem_end ||
      noncache_end > config.global_mem_alloc_base ||
      FSA_STACK_BASE < config.global_mem_base || stack_end > global_mem_end ||
      stack_bytes > FSA_STACK_POOL_SIZE ||
      (config.global_mem_noncache_alloc_base < stack_end &&
       noncache_end > FSA_STACK_BASE)) {
    return false;
  }

  description.num_cores = config.num_cores;
  description.max_threads_per_work_group = threads_per_core;
  description.local_mem_size_per_core = config.local_mem_size;
  description.cache_size = config.shared_cache_size;
  description.cache_line_size = config.cache_line_size;
  description.global_mem_size = config.global_mem_size;
  description.max_allocation_size =
      global_mem_end - config.global_mem_alloc_base;
  return true;
}

void reset_runtime_state() noexcept {
  firmware_ready.store(false, std::memory_order_release);
  transport_failed.store(false, std::memory_order_release);
  firmware_reboot_failed.store(false, std::memory_order_release);
  completion_slots.clear();
  stale_completion_tokens.clear();
  host_cmd_ring.clear();
  host_rd_ptr = 0;
  host_wr_ptr = 0;
  runtime_boot_generation = 0;
  boot_generation_before_reset = 0;
  next_alloc_generation = FSA_COMPLETION_ALLOC_GENERATION_INITIAL;
  dev_cmd_ring_base = 0;
  noncache_alloc_ready = false;
  active_configuration = nullptr;
}

void rollback_initialization() noexcept {
  reset_runtime_state();
  try {
    fsa_real_abort();
  } catch (...) {
  }
  falloc_clean();
  falloc_noncache_clean();
  try {
    recorder.Close();
  } catch (...) {
  }
}

enum FirmwareReadyResult {
  kFirmwareReadyResultSuccess = 0,
  kFirmwareReadyResultTimeout = -1,
  kFirmwareReadyResultTransportError = -2,
  kFirmwareReadyResultFault = -3,
};

void mark_transport_failure(int status) {
  if (status != 0) {
    transport_failed.store(true, std::memory_order_release);
    firmware_ready.store(false, std::memory_order_release);
  }
}

int copy_from_scratchpad(uintptr_t dev_addr, void *host_ptr, size_t size) {
  const int status = fsa_real_copy_from_scratchpad(dev_addr, host_ptr, size);
  mark_transport_failure(status);
  return status;
}

int real_mmio(uint64_t offset, int64_t wr_val, uint64_t *rd_ptr,
              bool latch_transport_failure = true) {
  int status = fsa_real_mmio(offset, wr_val, rd_ptr);
  if (latch_transport_failure) mark_transport_failure(status);
  if (rd_ptr == nullptr) {
    recorder.Event("mmio_write", offset, sizeof(uint64_t), wr_val, status);
  } else if (status == 0) {
    recorder.Event("mmio_read", offset, sizeof(uint64_t),
                   static_cast<int64_t>(*rd_ptr), status);
  } else {
    recorder.Event("mmio_read", offset, sizeof(uint64_t), 0, status);
  }
  return status;
}

int real_cp_reset() {
  int status = fsa_real_cp_reset();
  mark_transport_failure(status);
  if (status == 0) {
    host_rd_ptr = 0;
    host_wr_ptr = 0;
  }
  recorder.Event("cp_reset", 0, 0, 0, status);
  return status;
}

int copy_to_scratchpad_recorded(uintptr_t dev_addr, const void *host_ptr,
                                size_t size, std::string_view type,
                                bool latch_transport_failure = true) {
  int status = fsa_real_copy_to_scratchpad(dev_addr, host_ptr, size);
  if (latch_transport_failure) mark_transport_failure(status);
  if (status == 0 && size > 0) {
    recorder.BlobEvent(type, dev_addr, host_ptr, size, 0, status);
  } else {
    recorder.Event(type, dev_addr, size, 0, status);
  }
  return status;
}

bool firmware_session_failed() {
  return transport_failed.load(std::memory_order_acquire) ||
         firmware_reboot_failed.load(std::memory_order_acquire);
}

struct FirmwareSnapshot {
  uint64_t generation;
};

bool read_firmware_snapshot(FirmwareSnapshot *snapshot) {
  return snapshot != nullptr &&
         real_mmio(FSA_CP_OFF_FW_BOOT_GENERATION, 0, &snapshot->generation) ==
             0 &&
         !firmware_session_failed();
}

bool firmware_rebooted(const FirmwareSnapshot &snapshot,
                       FsaCompletionToken token) {
  return static_cast<uint16_t>(snapshot.generation) !=
         fsa_completion_token_boot_generation(token);
}

int sync_rd_ptr() {
  uint64_t cmd_rd_ptr;
  if (real_mmio(FSA_CP_OFF_RD_PTR, 0, &cmd_rd_ptr, false) != 0) return -1;
  host_rd_ptr = cmd_rd_ptr;
  return 0;
}

enum class ReserveCommandStatus {
  Ready = 0,
  WouldBlock = 1,
  PreWpFailure = 2,
  Unavailable = 3,
};

Packet *reserve_command_slot(ReserveCommandStatus *status) {
  if (status == nullptr) return nullptr;
  if (!fsa_hal_is_available()) {
    *status = ReserveCommandStatus::Unavailable;
    return nullptr;
  }
  if (sync_rd_ptr() != 0) {
    *status = ReserveCommandStatus::PreWpFailure;
    return nullptr;
  }
  if ((host_wr_ptr + 1) % host_cmd_ring.size() == host_rd_ptr) {
    *status = ReserveCommandStatus::WouldBlock;
    return nullptr;
  }

  Packet *pkt = &host_cmd_ring[host_wr_ptr];
  memset(pkt, 0, sizeof(*pkt));
  *status = ReserveCommandStatus::Ready;
  return pkt;
}

std::optional<size_t> find_completion_slot(FsaCompletionToken token) {
  if (token == 0 || !fsa_completion_token_has_valid_slot(token)) {
    return std::nullopt;
  }
  const size_t slot_index = fsa_completion_token_slot_index(token);
  if (slot_index >= completion_slots.size()) {
    return std::nullopt;
  }
  const CompletionSlotMetadata &metadata = completion_slots[slot_index];
  return metadata.in_use && metadata.token == token
             ? std::optional<size_t>(slot_index)
             : std::nullopt;
}

std::optional<size_t> allocate_completion_slot() {
  for (size_t i = 0; i < completion_slots.size(); ++i) {
    if (!completion_slots[i].in_use) {
      completion_slots[i] = CompletionSlotMetadata{};
      completion_slots[i].in_use = true;
      completion_slots[i].token = fsa_completion_token_make(
          runtime_boot_generation, next_alloc_generation,
          static_cast<uint16_t>(i));
      if (std::find(stale_completion_tokens.begin(),
                    stale_completion_tokens.end(), completion_slots[i].token) !=
          stale_completion_tokens.end()) {
        /* Do not recycle an exact stale token while its owner still exists.
         * The generation space is intentionally bounded; refusing this one
         * allocation preserves use-after-release safety at wrap. */
        completion_slots[i] = CompletionSlotMetadata{};
        next_alloc_generation =
            fsa_completion_next_alloc_generation(next_alloc_generation);
        continue;
      }
      next_alloc_generation =
          fsa_completion_next_alloc_generation(next_alloc_generation);
      return i;
    }
  }
  return std::nullopt;
}

void maybe_record_d2h_capture(const MemoryCopyCaptureMetadata &metadata,
                              FsaCompletionResult result) {
  if (result != FSA_COMPLETION_RESULT_SUCCESS || metadata.size == 0 ||
      metadata.dst_addr == 0) {
    return;
  }
  recorder.BlobEvent("memory_copy_d2h", metadata.src_addr,
                     reinterpret_cast<const void *>(metadata.dst_addr),
                     metadata.size);
}

enum class SendCmdStatus {
  Ok = 0,
  /* Packet write failed before WP doorbell: safe to free the host slot. */
  PreWpFailure = 1,
  /* WP doorbell was attempted: delivery ambiguous — keep slot until reset. */
  WpAttemptedFailure = 2,
};

SendCmdStatus send_cmd(const Packet &cmd) {
  uint64_t cmd_wr_addr = dev_cmd_ring_base + (host_wr_ptr * sizeof(cmd.raw));

  if (copy_to_scratchpad_recorded(cmd_wr_addr, &cmd, sizeof(cmd.raw),
                                  "scratchpad_write", false) != 0) {
    return SendCmdStatus::PreWpFailure;
  }
  recorder.BlobEvent("cmd_packet", cmd_wr_addr, &cmd, sizeof(cmd.raw),
                     host_wr_ptr, 0);

  uint32_t next_wr_ptr = (host_wr_ptr + 1) % host_cmd_ring.size();
  if (real_mmio(FSA_CP_OFF_WR_PTR, next_wr_ptr, nullptr) != 0) {
    return SendCmdStatus::WpAttemptedFailure;
  }
  host_wr_ptr = next_wr_ptr;
  return SendCmdStatus::Ok;
}

template <typename FillPacket, typename AfterCompletionPrepared>
FsaCommandSubmitStatus submit_command(
    FsaCompletionToken *completion, FillPacket fill,
    AfterCompletionPrepared after_completion_prepared) {
  if (completion == nullptr) return kFsaCommandSubmitInvalidArgument;
  *completion = 0;

  std::lock_guard<std::mutex> lock(command_mtx);
  if (!fsa_hal_is_available()) return kFsaCommandSubmitTransportError;

  ReserveCommandStatus reserve_status = ReserveCommandStatus::Unavailable;
  Packet *pkt = reserve_command_slot(&reserve_status);
  if (pkt == nullptr) {
    switch (reserve_status) {
      case ReserveCommandStatus::WouldBlock:
        return kFsaCommandSubmitWouldBlock;
      case ReserveCommandStatus::PreWpFailure:
      case ReserveCommandStatus::Unavailable:
        return kFsaCommandSubmitTransportError;
      case ReserveCommandStatus::Ready:
        break;
    }
    return kFsaCommandSubmitTransportError;
  }
  fill(*pkt);

  const FsaCompletionToken empty_token = 0;
  memcpy(pkt->raw + FSA_COMPLETION_TOKEN_OFFSET, &empty_token,
         sizeof(empty_token));
  const auto slot_index = allocate_completion_slot();
  if (!slot_index.has_value()) return kFsaCommandSubmitWouldBlock;

  CompletionSlotMetadata &metadata = completion_slots[*slot_index];
  const FsaCompletionSlot slot =
      fsa_completion_slot_encode(fsa_completion_token_alloc_tag(metadata.token),
                                 FSA_COMPLETION_RESULT_PENDING);
  const uintptr_t slot_address =
      FSA_COMPLETION_POOL_BASE + *slot_index * FSA_COMPLETION_SLOT_BYTES;
  if (copy_to_scratchpad_recorded(slot_address, &slot, sizeof(slot),
                                  "completion_pool_write", false) != 0) {
    completion_slots[*slot_index] = CompletionSlotMetadata{};
    return kFsaCommandSubmitTransportError;
  }
  memcpy(pkt->raw + FSA_COMPLETION_TOKEN_OFFSET, &metadata.token,
         sizeof(metadata.token));
  *completion = metadata.token;
  after_completion_prepared(*pkt, *completion);

  const SendCmdStatus rc = send_cmd(*pkt);
  if (rc == SendCmdStatus::PreWpFailure) {
    completion_slots[*slot_index] = CompletionSlotMetadata{};
    *completion = 0;
    return kFsaCommandSubmitTransportError;
  }
  if (rc != SendCmdStatus::Ok) return kFsaCommandSubmitTransportError;
  return kFsaCommandSubmitAccepted;
}

template <typename FillPacket>
FsaCommandSubmitStatus submit_command(FsaCompletionToken *completion,
                                      FillPacket fill) {
  return submit_command(completion, fill, [](Packet &, FsaCompletionToken) {});
}

/* Ensure CP is in ROM Reset before publishing a Boot Descriptor. */
int ensure_rom_reset_state(uint64_t deadline_ms, uint64_t *elapsed_ms) {
  uint64_t local_elapsed = 0;
  uint64_t *elapsed = elapsed_ms ? elapsed_ms : &local_elapsed;
  bool msip_sent = false;
  while (true) {
    uint64_t status = kFirmwareStatusBooting;
    CHECK_ERR(real_mmio(FSA_CP_OFF_FW_STATUS, 0, &status));
    if (status == kFirmwareStatusReset) return 0;
    if (!msip_sent &&
        (status == kFirmwareStatusReady || status == kFirmwareStatusFault)) {
      CHECK_ERR(real_cp_reset());
      msip_sent = true;
    }
    usleep(1000);
    ++(*elapsed);
    if (deadline_ms > 0 && *elapsed >= deadline_ms) return -1;
  }
}

FsaCommandSubmitStatus send_cache_operation_cmd(
    uint16_t header, uint64_t dev_addr, uint64_t size,
    FsaCompletionToken *completion) {
  return submit_command(completion, [=](Packet &packet) {
    CacheOperationPacket &cmd = packet.cache_operation_packet;
    cmd.header = header;
    cmd.addr = dev_addr;
    cmd.size = size;
  });
}

static bool valid_memory_domain(MemoryDomain domain) {
  return domain == kMemoryDomainHost || domain == kMemoryDomainDevice;
}

FsaCommandSubmitStatus send_memory_copy_cmd(MemoryDomain src_domain,
                                            uint64_t src_addr,
                                            MemoryDomain dst_domain,
                                            uint64_t dst_addr, uint64_t size,
                                            FsaCompletionToken *completion) {
  return submit_command(
      completion,
      [=](Packet &packet) {
        MemoryCopyPacket &cmd = packet.memory_copy_packet;
        cmd.header = kMemoryCopyPacketHeader;
        cmd.src_domain = src_domain;
        cmd.dst_domain = dst_domain;
        cmd.src_addr = src_addr;
        cmd.dst_addr = dst_addr;
        cmd.size = size;
      },
      [=](Packet &, FsaCompletionToken token) {
        /* Capture H2D host payload at the runtime command seam.  D2H is
         * recorded when the terminal result is observed. */
        if (src_domain == kMemoryDomainHost && size != 0 && src_addr != 0) {
          recorder.BlobEvent("memory_copy_h2d", dst_addr,
                             reinterpret_cast<const void *>(src_addr), size);
        }
        if (dst_domain == kMemoryDomainHost && size != 0 && dst_addr != 0) {
          MemoryCopyCaptureMetadata &capture =
              completion_slots[fsa_completion_token_slot_index(token)].capture;
          capture.src_addr = src_addr;
          capture.dst_addr = dst_addr;
          capture.size = size;
        }
      });
}

int wait_for_copy_completion(FsaCompletionToken completion) {
  FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
  return fsa_wait_completion(completion, 0, &result) ==
                     kFsaCompletionWaitSuccess &&
                 result == FSA_COMPLETION_RESULT_SUCCESS
             ? 0
             : -1;
}

}  // namespace

int fsa_copy_to_dev(uintptr_t dev_addr, const void *host_ptr, size_t size) {
  if (!fsa_hal_is_available()) return -1;

  FsaCompletionToken completion = 0;
  FsaMemoryCopyInfo info{};
  info.struct_size = sizeof(info);
  info.source.domain = kMemoryDomainHost;
  info.source.range.address = reinterpret_cast<uintptr_t>(host_ptr);
  info.source.range.size = size;
  info.destination.domain = kMemoryDomainDevice;
  info.destination.range.address = dev_addr;
  info.destination.range.size = size;
  if (fsa_cmd_memory_copy(&info, &completion) != kFsaCommandSubmitAccepted) {
    return -1;
  }
  return wait_for_copy_completion(completion);
}

int fsa_copy_from_dev(uintptr_t dev_addr, void *host_ptr, size_t size) {
  if (!fsa_hal_is_available()) return -1;

  FsaCompletionToken completion = 0;
  FsaMemoryCopyInfo info{};
  info.struct_size = sizeof(info);
  info.source.domain = kMemoryDomainDevice;
  info.source.range.address = dev_addr;
  info.source.range.size = size;
  info.destination.domain = kMemoryDomainHost;
  info.destination.range.address = reinterpret_cast<uintptr_t>(host_ptr);
  info.destination.range.size = size;
  if (fsa_cmd_memory_copy(&info, &completion) != kFsaCommandSubmitAccepted) {
    return -1;
  }
  return wait_for_copy_completion(completion);
}

/* Publish Boot Descriptor only.  ROM programs Host DMA into the fixed staging
 * aperture and copies into TCM.  Returns after ROM clears FW_SIZE. */
static int upload_firmware(const void *fw_ptr, size_t fw_size) {
  if (fw_ptr == nullptr || fw_size == 0 || fw_size > FSA_CP_TCM_SIZE) return -1;

  host_rd_ptr = 0;
  host_wr_ptr = 0;

  CHECK_ERR(real_mmio(FSA_CP_OFF_FW_HOST_ADDR,
                      reinterpret_cast<uintptr_t>(fw_ptr), nullptr));
  CHECK_ERR(
      real_mmio(FSA_CP_OFF_FW_SIZE, static_cast<int64_t>(fw_size), nullptr));
  recorder.BlobEvent("boot_descriptor", 0, fw_ptr, fw_size);

  /* Wait until ROM has finished the Host→staging DMA (FW_SIZE cleared). */
  uint64_t elapsed_ms = 0;
  while (true) {
    uint64_t remaining = 1;
    CHECK_ERR(real_mmio(FSA_CP_OFF_FW_SIZE, 0, &remaining));
    if (remaining == 0) break;
    uint64_t status = kFirmwareStatusReset;
    uint64_t fault = kFirmwareFaultNone;
    CHECK_ERR(real_mmio(FSA_CP_OFF_FW_STATUS, 0, &status));
    CHECK_ERR(real_mmio(FSA_CP_OFF_FW_FAULT_CODE, 0, &fault));
    if (status == kFirmwareStatusFault) return -1;
    usleep(1000);
    if (++elapsed_ms >= 5000) return -1;
  }
  return 0;
}

int fsa_addr_malloc(uintptr_t dev_addr, size_t size) {
  int status = falloc_addr_malloc(dev_addr, size);
  recorder.Event("addr_malloc", dev_addr, size, 0, status);
  return status;
}

int fsa_malloc(void **dev_addr, size_t size) {
  int status = falloc_malloc(dev_addr, size);
  recorder.Event("malloc", reinterpret_cast<uintptr_t>(*dev_addr), size, 0,
                 status);
  return status;
}

int fsa_malloc_noncache(void **dev_addr, size_t size) {
  if (!noncache_alloc_ready) {
    *dev_addr = nullptr;
    return -1;
  }
  void *allocated_addr = nullptr;
  int status = falloc_noncache_malloc(&allocated_addr, size);
  *dev_addr = status == 0 ? allocated_addr : nullptr;
  recorder.Event("malloc_noncache", reinterpret_cast<uintptr_t>(*dev_addr),
                 size, 0, status);
  return status;
}

int fsa_free(void *dev_addr) {
  int status = falloc_free(dev_addr);
  if (status != 0 && noncache_alloc_ready) {
    status = falloc_noncache_free(dev_addr);
  }
  recorder.Event("free", reinterpret_cast<uintptr_t>(dev_addr), 0, 0, status);
  return status;
}

static bool kernel_geometry_fits_packet(uint32_t dimensions,
                                        const uint32_t local_size[3],
                                        const uint32_t num_groups[3],
                                        const uint64_t global_offset[3],
                                        uint32_t local_mem_size) {
  if (local_mem_size > UINT16_MAX) return false;
  for (uint32_t i = 0; i < dimensions; ++i) {
    if (local_size[i] == 0 || local_size[i] > UINT16_MAX) return false;
    if (num_groups[i] == 0 || num_groups[i] > UINT16_MAX) return false;
    if (global_offset[i] > UINT16_MAX) return false;
  }
  return true;
}

FsaCommandSubmitStatus fsa_cmd_start_kernel(const FsaKernelLaunchInfo *info,
                                            FsaCompletionToken *completion) {
  if (completion == nullptr) return kFsaCommandSubmitInvalidArgument;
  *completion = 0;
  if (info == nullptr || info->struct_size != sizeof(FsaKernelLaunchInfo) ||
      info->dimensions < 1 || info->dimensions > 3 ||
      !kernel_geometry_fits_packet(info->dimensions, info->local_size,
                                   info->num_groups, info->global_offset,
                                   info->local_mem_size)) {
    return kFsaCommandSubmitInvalidArgument;
  }

  const uint32_t dim = info->dimensions;
  uint16_t dispatch_flags = static_cast<uint16_t>(dim);
  if (info->has_printf_meta != 0) {
    dispatch_flags |= FSA_KERNEL_DISPATCH_HAS_PRINTF_META;
  }
  if (info->enable_stack_remap != 0) {
    dispatch_flags |= FSA_KERNEL_DISPATCH_STACK_REMAP;
  }

  return submit_command(completion, [=](Packet &packet) {
    KernelDispatchPacket &cmd = packet.kernel_dispatch_packet;
    cmd.header = kKernelDispatchPacketHeader;
    cmd.dim_reserved0 = dispatch_flags;
    cmd.local_size_x = static_cast<uint16_t>(info->local_size[0]);
    cmd.local_size_y = dim > 1 ? static_cast<uint16_t>(info->local_size[1]) : 1;
    cmd.local_size_z = dim > 2 ? static_cast<uint16_t>(info->local_size[2]) : 1;
    cmd.local_mem_size = static_cast<uint16_t>(info->local_mem_size);
    cmd.num_groups_x = static_cast<uint16_t>(info->num_groups[0]);
    cmd.num_groups_y = dim > 1 ? static_cast<uint16_t>(info->num_groups[1]) : 1;
    cmd.num_groups_z = dim > 2 ? static_cast<uint16_t>(info->num_groups[2]) : 1;
    cmd.global_offset_x = static_cast<uint16_t>(info->global_offset[0]);
    cmd.global_offset_y =
        dim > 1 ? static_cast<uint16_t>(info->global_offset[1]) : 0;
    cmd.global_offset_z =
        dim > 2 ? static_cast<uint16_t>(info->global_offset[2]) : 0;
    cmd.kernel_status = info->kernel_status;
    cmd.kernel_object = info->kernel_entry;
    cmd.kernarg_address = info->kernarg_address;
    cmd.kernel_trampoline = info->kernel_trampoline;
  });
}

FsaCommandSubmitStatus fsa_cmd_cache_flush(const FsaMemoryRange *range,
                                           FsaCompletionToken *completion) {
  if (range == nullptr || completion == nullptr) {
    if (completion != nullptr) *completion = 0;
    return kFsaCommandSubmitInvalidArgument;
  }
  return send_cache_operation_cmd(kCacheFlushPacketHeader, range->address,
                                  range->size, completion);
}

FsaCommandSubmitStatus fsa_cmd_cache_invalidate(
    const FsaMemoryRange *range, FsaCompletionToken *completion) {
  if (range == nullptr || completion == nullptr) {
    if (completion != nullptr) *completion = 0;
    return kFsaCommandSubmitInvalidArgument;
  }
  return send_cache_operation_cmd(kCacheInvalidatePacketHeader, range->address,
                                  range->size, completion);
}

FsaCommandSubmitStatus fsa_cmd_memory_copy(const FsaMemoryCopyInfo *info,
                                           FsaCompletionToken *completion) {
  if (info == nullptr || completion == nullptr ||
      info->struct_size != sizeof(FsaMemoryCopyInfo) ||
      !valid_memory_domain(info->source.domain) ||
      !valid_memory_domain(info->destination.domain) ||
      info->source.range.size != info->destination.range.size) {
    if (completion != nullptr) *completion = 0;
    return kFsaCommandSubmitInvalidArgument;
  }
  return send_memory_copy_cmd(
      info->source.domain, info->source.range.address, info->destination.domain,
      info->destination.range.address, info->source.range.size, completion);
}

FsaCommandSubmitStatus fsa_cmd_barrier(FsaCompletionToken *completion) {
  return submit_command(completion, [](Packet &packet) {
    packet.header_only_packet.header = kBarrierPacketHeader;
  });
}

FsaCompletionPollStatus fsa_poll_completion(FsaCompletionToken token,
                                            FsaCompletionResult *result) {
  if (firmware_session_failed()) {
    return kFsaCompletionPollTransportError;
  }

  MemoryCopyCaptureMetadata capture;
  size_t slot_index = 0;
  {
    std::lock_guard<std::mutex> lock(command_mtx);
    const auto found = find_completion_slot(token);
    if (!found.has_value()) {
      if (std::find(stale_completion_tokens.begin(),
                    stale_completion_tokens.end(),
                    token) == stale_completion_tokens.end()) {
        return kFsaCompletionPollInvalidToken;
      }
      if (result != nullptr) *result = FSA_COMPLETION_RESULT_FIRMWARE_REBOOT;
      recorder.CompletionSlotEvent(
          FSA_COMPLETION_POOL_BASE + fsa_completion_token_slot_index(token) *
                                         FSA_COMPLETION_SLOT_BYTES,
          fsa_completion_token_alloc_tag(token),
          FSA_COMPLETION_RESULT_FIRMWARE_REBOOT);
      return kFsaCompletionPollTerminal;
    }
    slot_index = *found;
    capture = completion_slots[slot_index].capture;
  }

  FirmwareSnapshot firmware;
  if (!read_firmware_snapshot(&firmware)) {
    return kFsaCompletionPollTransportError;
  }

  if (firmware_rebooted(firmware, token)) {
    if (result != nullptr) *result = FSA_COMPLETION_RESULT_FIRMWARE_REBOOT;
    recorder.CompletionSlotEvent(
        FSA_COMPLETION_POOL_BASE + slot_index * FSA_COMPLETION_SLOT_BYTES,
        fsa_completion_token_alloc_tag(token),
        FSA_COMPLETION_RESULT_FIRMWARE_REBOOT);
    return kFsaCompletionPollTerminal;
  }

  const uintptr_t slot_address =
      FSA_COMPLETION_POOL_BASE + slot_index * FSA_COMPLETION_SLOT_BYTES;
  FsaCompletionSlot slot = 0;
  if (copy_from_scratchpad(slot_address, &slot, sizeof(slot)) != 0 ||
      transport_failed.load(std::memory_order_acquire)) {
    return kFsaCompletionPollTransportError;
  }
  if (fsa_completion_slot_alloc_tag(slot) !=
      fsa_completion_token_alloc_tag(token)) {
    return kFsaCompletionPollInvalidToken;
  }

  const FsaCompletionResult slot_result = fsa_completion_slot_result(slot);
  if (result != nullptr) *result = slot_result;
  if (slot_result == FSA_COMPLETION_RESULT_PENDING) {
    return kFsaCompletionPollPending;
  }

  maybe_record_d2h_capture(capture, slot_result);
  recorder.CompletionSlotEvent(
      slot_address, fsa_completion_token_alloc_tag(token), slot_result);
  return kFsaCompletionPollTerminal;
}

FsaCompletionWaitStatus fsa_wait_completion(FsaCompletionToken token,
                                            uint64_t timeout_ms,
                                            FsaCompletionResult *result) {
  uint64_t elapsed_ms = 0;
  while (true) {
    const FsaCompletionPollStatus status = fsa_poll_completion(token, result);
    if (status == kFsaCompletionPollTerminal) {
      const FsaCompletionReleaseStatus release_status =
          fsa_release_completion(token);
      if (release_status == kFsaCompletionReleaseAccepted) {
        return kFsaCompletionWaitSuccess;
      }
      if (release_status == kFsaCompletionReleaseTransportError) {
        return kFsaCompletionWaitTransportError;
      }
      return kFsaCompletionWaitInvalidToken;
    }
    if (status == kFsaCompletionPollInvalidToken) {
      return kFsaCompletionWaitInvalidToken;
    }
    if (status == kFsaCompletionPollTransportError) {
      return kFsaCompletionWaitTransportError;
    }
    usleep(1000);
    if (timeout_ms > 0 && ++elapsed_ms >= timeout_ms) {
      return kFsaCompletionWaitTimeout;
    }
  }
}

FsaCompletionReleaseStatus fsa_release_completion(FsaCompletionToken token) {
  if (firmware_session_failed()) {
    return kFsaCompletionReleaseTransportError;
  }
  {
    std::lock_guard<std::mutex> lock(command_mtx);
    const auto stale = std::find(stale_completion_tokens.begin(),
                                 stale_completion_tokens.end(), token);
    if (stale != stale_completion_tokens.end()) {
      stale_completion_tokens.erase(stale);
      return kFsaCompletionReleaseAccepted;
    }
    const auto found = find_completion_slot(token);
    if (!found.has_value()) return kFsaCompletionReleaseInvalidToken;
  }

  FirmwareSnapshot firmware;
  if (!read_firmware_snapshot(&firmware)) {
    return kFsaCompletionReleaseTransportError;
  }
  if (firmware_rebooted(firmware, token)) {
    std::lock_guard<std::mutex> lock(command_mtx);
    const auto found = find_completion_slot(token);
    if (found.has_value()) {
      completion_slots[*found] = CompletionSlotMetadata{};
    }
    return kFsaCompletionReleaseAccepted;
  }

  const size_t slot_index = fsa_completion_token_slot_index(token);
  const uintptr_t slot_address =
      FSA_COMPLETION_POOL_BASE + slot_index * FSA_COMPLETION_SLOT_BYTES;
  FsaCompletionSlot slot = 0;
  if (copy_from_scratchpad(slot_address, &slot, sizeof(slot)) != 0 ||
      transport_failed.load(std::memory_order_acquire)) {
    return kFsaCompletionReleaseTransportError;
  }
  if (fsa_completion_slot_alloc_tag(slot) !=
      fsa_completion_token_alloc_tag(token)) {
    return kFsaCompletionReleaseInvalidToken;
  }
  if (fsa_completion_slot_result(slot) == FSA_COMPLETION_RESULT_PENDING) {
    return kFsaCompletionReleasePending;
  }

  std::lock_guard<std::mutex> lock(command_mtx);
  if (firmware_reboot_failed.load(std::memory_order_acquire)) {
    return kFsaCompletionReleaseTransportError;
  }
  const auto found = find_completion_slot(token);
  if (!found.has_value()) return kFsaCompletionReleaseInvalidToken;
  completion_slots[*found] = CompletionSlotMetadata{};
  return kFsaCompletionReleaseAccepted;
}

static FirmwareReadyResult wait_for_firmware_ready(uint64_t timeout) {
  if (transport_failed.load(std::memory_order_acquire))
    return kFirmwareReadyResultTransportError;
  uint64_t elapsed_ms = 0;
  while (true) {
    uint64_t status = kFirmwareStatusReset;
    uint64_t abi = 0;
    uint64_t generation = 0;
    uint64_t fault = kFirmwareFaultNone;
    if (real_mmio(FSA_CP_OFF_FW_STATUS, 0, &status) != 0 ||
        real_mmio(FSA_CP_OFF_FW_ABI_VERSION, 0, &abi) != 0 ||
        real_mmio(FSA_CP_OFF_FW_BOOT_GENERATION, 0, &generation) != 0 ||
        real_mmio(FSA_CP_OFF_FW_FAULT_CODE, 0, &fault) != 0)
      return kFirmwareReadyResultTransportError;
    if (transport_failed.load(std::memory_order_acquire))
      return kFirmwareReadyResultTransportError;
    if (status == kFirmwareStatusFault) return kFirmwareReadyResultFault;
    if (status == kFirmwareStatusReady) {
      if (abi != FSA_COMMAND_ABI_VERSION_V3 || fault != kFirmwareFaultNone)
        return kFirmwareReadyResultFault;
      /* Ignore the previous READY handshake while reset is in progress. */
      if (generation != 0 && generation != boot_generation_before_reset) {
        runtime_boot_generation = generation;
        boot_generation_before_reset = generation;
        return kFirmwareReadyResultSuccess;
      }
    }
    usleep(1000);
    if (timeout > 0 && ++elapsed_ms >= timeout)
      return kFirmwareReadyResultTimeout;
  }
}

int fsa_probe() { return fsa_real_probe(); }

int fsa_hal_init(FsaDeviceDescription *description) {
  if (description == nullptr) return -1;
  if (fsa_hal_is_available()) return -1;

  FsaDeviceDescription local_description = {};
  try {
    const auto *snapshot = formosa::real::configuration_snapshot();
    if (snapshot == nullptr ||
        !derive_device_description(*snapshot, local_description)) {
      fprintf(stderr, "Invalid Formosa configuration snapshot\n");
      return -1;
    }

    rollback_initialization();
    active_configuration = snapshot;
    recorder.InitIfNeeded();
    firmware_ready.store(false, std::memory_order_release);
    transport_failed.store(false, std::memory_order_release);
    firmware_reboot_failed.store(false, std::memory_order_release);

    const int init_status = fsa_real_init();
    mark_transport_failure(init_status);
    if (init_status != 0) {
      rollback_initialization();
      return init_status;
    }

    if (real_mmio(FSA_CP_OFF_FW_BOOT_GENERATION, 0,
                  &boot_generation_before_reset) != 0) {
      rollback_initialization();
      return -1;
    }

    uint64_t dev_cmd_ring_size = 0;
    if (real_mmio(FSA_CP_OFF_CMD_RING_SIZE, 0, &dev_cmd_ring_size) != 0 ||
        dev_cmd_ring_size == 0) {
      rollback_initialization();
      return -1;
    }
    host_cmd_ring.resize(dev_cmd_ring_size);
    host_rd_ptr = 0;
    host_wr_ptr = 0;
    next_alloc_generation = FSA_COMPLETION_ALLOC_GENERATION_INITIAL;
    completion_slots.assign(FSA_COMPLETION_POOL_ENTRIES,
                            CompletionSlotMetadata{});
    stale_completion_tokens.clear();
    runtime_boot_generation = 0;

    if (real_mmio(FSA_CP_OFF_CMD_RING_BASE, 0, &dev_cmd_ring_base) != 0) {
      rollback_initialization();
      return -1;
    }
    recorder.ManifestU64("cmd_ring_size", dev_cmd_ring_size);
    recorder.ManifestU64("cmd_ring_base", dev_cmd_ring_base);
    recorder.ManifestU64("completion_pool_base",
                         (uint64_t)FSA_COMPLETION_POOL_BASE);
    recorder.ManifestU64("global_mem_base", snapshot->global_mem_base);
    recorder.ManifestU64("global_mem_alloc_base",
                         snapshot->global_mem_alloc_base);
    recorder.ManifestU64("global_mem_size", snapshot->global_mem_size);
    recorder.ManifestU64("fsa_mmio_base", snapshot->fsa_mmio_base);
    recorder.ManifestU64("cache_block_size", snapshot->cache_line_size);

    const uint64_t alloc_size = local_description.max_allocation_size;
    if (falloc_init(snapshot->global_mem_alloc_base, alloc_size,
                    snapshot->cache_line_size, 0) != 0 ||
        falloc_noncache_init(snapshot->global_mem_noncache_alloc_base,
                             snapshot->global_mem_noncache_alloc_size,
                             snapshot->cache_line_size, 0) != 0) {
      rollback_initialization();
      return -1;
    }
    noncache_alloc_ready = true;

    uint64_t elapsed_ms = 0;
    if (ensure_rom_reset_state(5000, &elapsed_ms) != 0 ||
        upload_firmware(gpufw_elf, gpufw_elf_len) != 0 ||
        wait_for_firmware_ready(5000) != kFirmwareReadyResultSuccess) {
      rollback_initialization();
      return -1;
    }
    firmware_ready.store(true, std::memory_order_release);
    *description = local_description;
    return 0;
  } catch (...) {
    rollback_initialization();
    return -1;
  }
}

int fsa_hal_reset(uint64_t timeout) {
  std::lock_guard<std::mutex> lock(command_mtx);
  if (active_configuration == nullptr ||
      firmware_reboot_failed.load(std::memory_order_acquire))
    return -1;
  firmware_ready.store(false, std::memory_order_release);
  transport_failed.store(false, std::memory_order_release);
  boot_generation_before_reset = runtime_boot_generation;

  uint64_t elapsed_ms = 0;
  if (ensure_rom_reset_state(timeout, &elapsed_ms) != 0 ||
      upload_firmware(gpufw_elf, gpufw_elf_len) != 0) {
    firmware_reboot_failed.store(true, std::memory_order_release);
    return -1;
  }
  const uint64_t remaining =
      timeout == 0 ? 0 : (elapsed_ms >= timeout ? 1 : timeout - elapsed_ms);
  if (wait_for_firmware_ready(remaining) != kFirmwareReadyResultSuccess) {
    firmware_reboot_failed.store(true, std::memory_order_release);
    return -1;
  }
  for (const CompletionSlotMetadata &metadata : completion_slots) {
    if (metadata.in_use) stale_completion_tokens.push_back(metadata.token);
  }
  std::fill(completion_slots.begin(), completion_slots.end(),
            CompletionSlotMetadata{});
  host_rd_ptr = 0;
  host_wr_ptr = 0;
  next_alloc_generation = FSA_COMPLETION_ALLOC_GENERATION_INITIAL;
  firmware_ready.store(true, std::memory_order_release);
  return 0;
}

int fsa_hal_cleanup() {
  reset_runtime_state();
  fsa_real_cleanup();
  falloc_clean();
  falloc_noncache_clean();
  recorder.Close();
  return 0;
}

int fsa_hal_is_available() {
  return active_configuration != nullptr &&
         firmware_ready.load(std::memory_order_acquire) &&
         !firmware_session_failed();
}

int fsa_hal_check_occupancy(uint64_t group_size, uint64_t local_mem_per_group,
                            uint64_t *max_local_mem) {
  if (!fsa_hal_is_available()) return -1;
  const auto &config = *active_configuration;
  if (group_size == 0) {
    fprintf(stderr, "group_size must be > 0\n");
    return -1;
  }

  const uint64_t threads_per_core =
      config.threads_per_warp * config.warps_per_core;
  if (group_size > threads_per_core) {
    fprintf(stderr,
            "Cannot schedule kernel: group_size (%" PRIu64
            ") > threads_per_core (%" PRIu64 ")\n",
            group_size, threads_per_core);
    return -1;
  }

  const uint64_t warps_per_group =
      group_size / config.threads_per_warp +
      (group_size % config.threads_per_warp != 0 ? 1 : 0);
  const uint64_t groups_by_threads = config.warps_per_core / warps_per_group;
  if (groups_by_threads == 0) {
    fprintf(stderr, "No available slots due to warp/thread constraint\n");
    return -1;
  }

  uint64_t groups_by_local_mem = groups_by_threads;
  if (local_mem_per_group != 0) {
    if (local_mem_per_group > config.local_mem_size) {
      fprintf(stderr,
              "Cannot schedule kernel: local_mem_per_group (%" PRIu64
              ") > local_mem_size (%" PRIu64 ")\n",
              local_mem_per_group, config.local_mem_size);
      return -1;
    }
    groups_by_local_mem = config.local_mem_size / local_mem_per_group;
  }
  if (std::min(groups_by_threads, groups_by_local_mem) == 0) {
    fprintf(stderr, "Kernel cannot have any resident workgroups per core\n");
    return -1;
  }
  if (max_local_mem != nullptr)
    *max_local_mem = config.local_mem_size / groups_by_threads;
  return 0;
}
