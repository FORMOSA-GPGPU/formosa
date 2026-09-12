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
#include <queue>
#include <vector>

#include "cores/pipelined/lsu/base.h"
#include "cores/pipelined/lsu/stack_remap_table.h"

namespace simtix::pipelined {

struct LsuTransExtension : tlm::tlm_extension<LsuTransExtension> {
  uint32_t slot_id = 0;
  uint32_t req_id = 0;

  tlm_extension_base *clone() const override {
    return new LsuTransExtension(*this);
  }
  void copy_from(const tlm_extension_base &other) override {
    const auto &extension = static_cast<const LsuTransExtension &>(other);
    slot_id = extension.slot_id;
    req_id = extension.req_id;
  }

  // Don't delete this
  virtual void free() override {}
};

namespace {

bool IsPowerOfTwo(uint64_t n) { return n != 0 && (n & (n - 1)) == 0; }

void CheckPowerOfTwo(const char *param_name, uint64_t value) {
  if (!IsPowerOfTwo(value)) {
    LV_FATAL(
        "CoalescingOutstandingLsu parameter '{}' must be a power of two, got "
        "{}",
        param_name, value);
  }
}

uint32_t log2(uint64_t n) {
  uint32_t res = 0;
  while (n >>= 1) res++;
  return res;
}

}  // namespace

class CoalescingOutstandingLsu : public Lsu {
 public:
  struct Param {
    uint32_t num_inflight_slots = 4;
    uint64_t cache_block_size = 64;
    bool enable_stack_remap = true;
    uint32_t granularity = 8;
    uint32_t stack_group_size = 4;
    uint64_t stack_start = 0x81000000;
    uint64_t stack_end = 0x81FFFFFF;
    uint64_t stack_size_per_thread = 1024;  // 1KB stack per thread

    // clang-format off
    LV_SCHEMA(CoalescingOutstandingLsu, Param,
              LV_FIELD(num_inflight_slots, "Maximum number of in-flight LSU instructions"),
              LV_FIELD(cache_block_size, "Cache block size in bytes"),
              LV_FIELD(enable_stack_remap, "Whether to enable Stack remap or not"),
              LV_FIELD(granularity, "Granularity for stack remap (in bytes)"),
              LV_FIELD(stack_group_size, "Number of threads sharing the same stack region"),
              LV_FIELD(stack_start, "Start address of the stack region"),
              LV_FIELD(stack_end, "End address of the stack region"),
              LV_FIELD(stack_size_per_thread, "Stack size allocated per thread"))
    // clang-format on
  };

  CoalescingOutstandingLsu(const sc_module_name &name, const ArchParam &param,
                           const Param &lsu_param)
      : Lsu(name),
        cache_block_size_(lsu_param.cache_block_size),
        num_lanes_(param.num_lanes),
        inflight_slots_(lsu_param.num_inflight_slots),
        enable_stack_remap_(lsu_param.enable_stack_remap),
        granularity_(lsu_param.granularity),
        stack_group_size_(lsu_param.stack_group_size),
        stack_start_(lsu_param.stack_start),
        stack_end_(lsu_param.stack_end),
        stack_size_per_thread_(lsu_param.stack_size_per_thread),
        stats_(name) {
    CheckPowerOfTwo("cache_block_size", cache_block_size_);
    if (inflight_slots_.empty()) {
      LV_FATAL(
          "CoalescingOutstandingLsu parameter 'num_inflight_slots' must be "
          "nonzero");
    }
    if (enable_stack_remap_) {
      CheckPowerOfTwo("granularity", granularity_);
      CheckPowerOfTwo("stack_group_size", stack_group_size_);
      CheckPowerOfTwo("stack_size_per_thread", stack_size_per_thread_);
    }

    // initialize inflight_slots_
    for (uint32_t slot_id = 0; slot_id < inflight_slots_.size(); ++slot_id) {
      auto &slot = inflight_slots_[slot_id];
      slot.slot_id = slot_id;
      slot.trans = std::vector<tlm::tlm_generic_payload>(num_lanes_);
      slot.lane_to_line.resize(num_lanes_);
      slot.line_buf.resize(num_lanes_ * cache_block_size_);
      slot.strb_buf.resize(num_lanes_ * cache_block_size_);
      for (uint32_t lane = 0; lane < num_lanes_; ++lane) {
        slot.trans[lane].set_mm(lv::mm::Static);
        slot.ip_exts.emplace_back(new lv::IpExtension);
        slot.atomic_exts.emplace_back(new AtomicExtension);
        slot.lsu_exts.emplace_back(new LsuTransExtension);
      }
      free_slots_.push(slot_id);
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

    SC_THREAD(IssueMemReq);
    SC_THREAD(CollectMemResp);
  }

  ~CoalescingOutstandingLsu() override {
    for (auto &slot : inflight_slots_) {
      ClearAtomicExtensions(slot.trans);
      for (auto &payload : slot.trans) {
        payload.clear_extension<lv::IpExtension>();
        payload.clear_extension<LsuTransExtension>();
      }
    }
  }

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

  struct InflightSlot {
    bool valid = false;
    uint32_t slot_id = 0;
    Packet *packet = nullptr;
    uint32_t total_reqs = 0;
    uint32_t received_reqs = 0;
    std::vector<uint64_t> line_reqs;
    std::vector<uint32_t> lane_to_line;
    std::vector<uint8_t> line_buf;
    std::vector<uint8_t> strb_buf;
    std::vector<std::unique_ptr<lv::IpExtension>> ip_exts;
    std::vector<std::unique_ptr<AtomicExtension>> atomic_exts;
    std::vector<std::unique_ptr<LsuTransExtension>> lsu_exts;
    // Declared last so payloads are destroyed before their extension owners.
    std::vector<tlm::tlm_generic_payload> trans;
  };

  enum class BarrierState { kNone, kDraining, kAtomicInFlight };

  void SetupAtomicTrans(InflightSlot &slot, uint32_t length,
                        AtomicExtension::Op op);
  void SetupLineTrans(InflightSlot &slot, tlm::tlm_command command);
  void Coalesce(InflightSlot &slot);
  void ScatterLoadData(InflightSlot &slot);
  void ExtendLane(Packet *packet, uint32_t lane);
  void FreeSlot(uint32_t slot_id);
  bool StackAddressRemapper(uint64_t *addr);

  uint8_t *GetLineBuffer(InflightSlot &slot, uint32_t line_id) {
    return &slot.line_buf[line_id * cache_block_size_];
  }

  uint8_t *GetStrbBuffer(InflightSlot &slot, uint32_t line_id) {
    return &slot.strb_buf[line_id * cache_block_size_];
  }

  uint64_t ToLineOffset(uint64_t addr) {
    return addr & (cache_block_size_ - 1);
  }

  uint64_t ToLineAddr(uint64_t addr) { return addr & ~(cache_block_size_ - 1); }

  void IssueMemReq();
  void CollectMemResp();

  sc_event slot_available_event_;
  std::vector<InflightSlot> inflight_slots_;
  std::queue<uint32_t> free_slots_;
  uint32_t in_flight_count_ = 0;
  BarrierState barrier_state_ = BarrierState::kNone;

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
