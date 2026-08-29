/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/ip_extension.h>
#include <liblv/mm/static.h>
#include <liblv/output.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "cores/pipelined/lsu/base.h"
#include "cores/pipelined/lsu/stack_remap_table.h"

namespace simtix::pipelined {

namespace {

bool IsPowerOfTwo(uint64_t n) { return n != 0 && (n & (n - 1)) == 0; }

void CheckPowerOfTwo(const char *param_name, uint64_t value) {
  if (!IsPowerOfTwo(value)) {
    LV_FATAL("CoalescingLsu parameter '{}' must be a power of two, got {}",
             param_name, value);
  }
}

uint32_t log2(uint64_t n) {
  uint32_t res = 0;
  while (n >>= 1) res++;
  return res;
}

}  // namespace

class CoalescingLsu : public Lsu {
 public:
  struct Param {
    uint64_t cache_block_size = 64;
    bool enable_stack_remap = true;
    uint32_t granularity = 8;
    uint32_t stack_group_size = 4;
    uint64_t stack_start = 0x81000000;
    uint64_t stack_end = 0x81FFFFFF;
    uint64_t stack_size_per_thread = 1024;  // 1KB stack per thread

    // clang-format off
    LV_SCHEMA(CoalescingLsu, Param,
              LV_FIELD(cache_block_size, "Cache block size in bytes"),
              LV_FIELD(enable_stack_remap, "Whether to enable Stack remap or not"),
              LV_FIELD(granularity, "Granularity for stack remap (in bytes)"),
              LV_FIELD(stack_group_size, "Number of threads sharing the same stack region"),
              LV_FIELD(stack_start, "Start address of the stack region"),
              LV_FIELD(stack_end, "End address of the stack region"),
              LV_FIELD(stack_size_per_thread, "Stack size allocated per thread"))
    // clang-format on
  };

  CoalescingLsu(const sc_module_name &name, const ArchParam &param,
                const Param &pp)
      : Lsu(name),
        cache_block_size_(pp.cache_block_size),
        num_lanes_(param.num_lanes),
        trans_(param.num_lanes),
        lane_to_line_(param.num_lanes),
        line_buf_(param.num_lanes * cache_block_size_, 0),
        strb_buf_(param.num_lanes * cache_block_size_, 0),
        enable_stack_remap_(pp.enable_stack_remap),
        granularity_(pp.granularity),
        stack_group_size_(pp.stack_group_size),
        stack_start_(pp.stack_start),
        stack_end_(pp.stack_end),
        stack_size_per_thread_(pp.stack_size_per_thread),
        stats_(name) {
    CheckPowerOfTwo("cache_block_size", cache_block_size_);
    if (enable_stack_remap_) {
      CheckPowerOfTwo("granularity", granularity_);
      CheckPowerOfTwo("stack_group_size", stack_group_size_);
      CheckPowerOfTwo("stack_size_per_thread", stack_size_per_thread_);
    }

    exts_.reserve(num_lanes_);
    atomic_exts_.reserve(num_lanes_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      trans_[i].set_mm(lv::mm::Static);
      exts_.emplace_back(new lv::IpExtension);
      atomic_exts_.emplace_back(new AtomicExtension);
    }

    if (enable_stack_remap_) {
      BIT_GRANULARITY = log2(granularity_);
      MASK_GRANULARITY = (1ULL << BIT_GRANULARITY) - 1;

      BIT_WIDTH_STACK = log2(stack_size_per_thread_);
      MASK_STACK = (1ULL << BIT_WIDTH_STACK) - 1;

      BIT_WIDTH_TID = log2(stack_group_size_);
      MASK_TID = (1ULL << BIT_WIDTH_TID) - 1;
      MASK_BASE = ~((MASK_TID << BIT_WIDTH_STACK) | MASK_STACK);
    }

    SC_THREAD(HandleProc);
    SC_THREAD(IssueMemReq);
    SC_THREAD(CollectMemResp);
  }

  ~CoalescingLsu() override { ClearAtomicExtensions(trans_); }

  lv::stats::Group *stats() const override { return &stats_; }
  StackRemapTable *stack_remap_table() const { return stack_remap_table_; }
  void set_stack_remap_table(StackRemapTable *table) {
    stack_remap_table_ = table;
  }

 protected:
  // Hardware config
  const uint64_t cache_block_size_;
  // Core config
  const uint32_t num_lanes_;

  void SetupAtomicTrans(uint32_t len, AtomicExtension::Op op);
  void SetupLineTrans(tlm::tlm_command command);
  void Coalesce();
  void ScatterLoadData();
  bool StackAddressRemapper(uint64_t *addr);

  uint8_t *GetLineBuffer(uint32_t line_id) {
    return &line_buf_[line_id * cache_block_size_];
  }

  uint8_t *GetStrbBuffer(uint32_t line_id) {
    return &strb_buf_[line_id * cache_block_size_];
  }

  uint64_t ToLineOffset(uint64_t addr) {
    return addr & (cache_block_size_ - 1);
  }

  uint64_t ToLineAddr(uint64_t addr) { return addr & ~(cache_block_size_ - 1); }

  template <typename RetT, typename CastT>
  void SignExtensionImpl() {
    assert(packet_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (packet_->tmask[i] == 1) {
        packet_->data_buf[i] = static_cast<RetT>(
            *reinterpret_cast<CastT *>(&packet_->data_buf[i]));
      }
    }
  }

  template <typename T, typename... Rest>
  void DispatchSignExtension(uint32_t size, bool is_signed) {
    if (sizeof(T) == size) {
      if (is_signed) {
        SignExtensionImpl<int64_t, std::make_signed_t<T>>();
      } else {
        SignExtensionImpl<uint64_t, std::make_unsigned_t<T>>();
      }
    } else if constexpr (sizeof...(Rest) > 0) {
      DispatchSignExtension<Rest...>(size, is_signed);
    }
  }

  void SignExtension(uint32_t size, bool is_signed) {
    DispatchSignExtension<int8_t, int16_t, int32_t, int64_t>(size, is_signed);
  }

  void HandleProc();
  void IssueMemReq();
  void CollectMemResp();

  sc_event start_issuing_mem_req_;
  sc_event start_collecting_mem_resp_;
  sc_event done_issuing_mem_req_;
  sc_event done_collecting_mem_resp_;

  Packet *packet_ = nullptr;

  std::vector<std::unique_ptr<lv::IpExtension>> exts_;
  std::vector<std::unique_ptr<AtomicExtension>> atomic_exts_;
  std::vector<tlm::tlm_generic_payload> trans_;

  std::vector<uint64_t> line_reqs_;
  std::vector<uint32_t> lane_to_line_;
  std::vector<uint8_t> line_buf_;
  std::vector<uint8_t> strb_buf_;

  const bool enable_stack_remap_;
  const uint32_t granularity_;
  const uint32_t stack_group_size_;
  const uint64_t stack_start_;
  const uint64_t stack_end_;
  const uint64_t stack_size_per_thread_;
  StackRemapTable *stack_remap_table_ = nullptr;

  uint32_t BIT_GRANULARITY = 0;
  uint64_t MASK_GRANULARITY = 0;

  uint32_t BIT_WIDTH_STACK = 0;
  uint64_t MASK_STACK = 0;

  uint32_t BIT_WIDTH_TID = 0;
  uint64_t MASK_TID = 0;

  uint64_t MASK_BASE = 0;

  struct Stats : lv::stats::Group {
    Metric stack_reads_line_reqs;
    Metric stack_writes_line_reqs;
    Formula<Integer> total_stack_line_request;
    Metric stack_lane_reqs;

    Metric non_stack_reads_line_reqs;
    Metric non_stack_writes_line_reqs;
    Formula<Integer> total_non_stack_line_request;
    Metric non_stack_lane_reqs;
    Formula<Integer> total_line_request;
    Formula<Integer> total_lane_request;
    Formula<Real> memory_coalescing_efficiency;

    Stats(const char *name)
        : Group(name),
          LV_STAT(stack_reads_line_reqs,
                  "Number of line requests for stack reads"),
          LV_STAT(stack_writes_line_reqs,
                  "Number of line requests for stack writes"),
          LV_STAT(total_stack_line_request,
                  "Total number of line requests for stack accesses"),
          LV_STAT(stack_lane_reqs, "Number of active stack memory lanes"),
          LV_STAT(non_stack_reads_line_reqs,
                  "Number of line requests for non-stack reads"),
          LV_STAT(non_stack_writes_line_reqs,
                  "Number of line requests for non-stack writes"),
          LV_STAT(total_non_stack_line_request,
                  "Total number of line requests for non-stack accesses"),
          LV_STAT(non_stack_lane_reqs,
                  "Number of active non-stack memory lanes"),
          LV_STAT(total_line_request,
                  "Total number of coalesced line requests"),
          LV_STAT(total_lane_request, "Total number of active memory lanes"),
          LV_STAT(memory_coalescing_efficiency,
                  "Active memory lanes per coalesced line request") {
      total_stack_line_request = stack_reads_line_reqs + stack_writes_line_reqs;
      total_non_stack_line_request =
          non_stack_reads_line_reqs + non_stack_writes_line_reqs;
      total_line_request =
          total_stack_line_request + total_non_stack_line_request;
      total_lane_request = stack_lane_reqs + non_stack_lane_reqs;
      memory_coalescing_efficiency = total_lane_request / total_line_request;
    }
  } mutable stats_;
};

}  // namespace simtix::pipelined
