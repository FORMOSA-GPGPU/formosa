// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0
#include "cores/pipelined/lsu/coalescing_outstanding.h"

#include <liblv/binding.h>

#include <cstring>

#include "cores/exec_flag.h"

namespace simtix::pipelined {
void CoalescingOutstandingLsu::SetupAtomicTrans(InflightSlot &slot,
                                                uint32_t length,
                                                AtomicExtension::Op op) {
  slot.total_reqs = 0;
  for (uint32_t lane = 0; lane < num_lanes_; ++lane) {
    if (slot.packet->tmask[lane] != 1) continue;
    auto &lane_trans = slot.trans[lane];
    lane_trans.clear_extension<AtomicExtension>();
    lane_trans.set_command(tlm::TLM_READ_COMMAND);
    lane_trans.set_address(slot.packet->addr_buf[lane]);
    lane_trans.set_data_length(length);
    lane_trans.set_data_ptr(
        reinterpret_cast<unsigned char *>(&slot.packet->data_buf[lane]));
    lane_trans.set_byte_enable_ptr(nullptr);
    lane_trans.set_byte_enable_length(0);
    lane_trans.set_streaming_width(length);
    lane_trans.set_dmi_allowed(false);
    lane_trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    lane_trans.set_extension(slot.ip_exts[lane].get());
    slot.ip_exts[lane]->ip = slot.packet->wpc;
    lane_trans.set_extension(slot.lsu_exts[lane].get());
    slot.lsu_exts[lane]->slot_id = slot.slot_id;
    slot.lsu_exts[lane]->req_id = lane;
    ++slot.total_reqs;
  }
  SetupAtomicExtensions(slot.packet->tmask, slot.atomic_exts, slot.trans, op);
}

void CoalescingOutstandingLsu::SetupLineTrans(InflightSlot &slot,
                                              tlm::tlm_command command) {
  for (uint32_t req_id = 0; req_id < slot.total_reqs; ++req_id) {
    auto &line_trans = slot.trans[req_id];
    line_trans.clear_extension<AtomicExtension>();
    line_trans.set_command(command);
    line_trans.set_address(slot.line_reqs[req_id]);
    line_trans.set_data_length(cache_block_size_);
    line_trans.set_data_ptr(GetLineBuffer(slot, req_id));
    line_trans.set_byte_enable_ptr(nullptr);
    line_trans.set_byte_enable_length(0);
    line_trans.set_streaming_width(cache_block_size_);
    line_trans.set_dmi_allowed(false);
    line_trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    line_trans.set_extension(slot.ip_exts[req_id].get());
    slot.ip_exts[req_id]->ip = slot.packet->wpc;
    line_trans.set_extension(slot.lsu_exts[req_id].get());
    slot.lsu_exts[req_id]->slot_id = slot.slot_id;
    slot.lsu_exts[req_id]->req_id = req_id;

    // For store, we need to set byte enables
    if (command == tlm::TLM_WRITE_COMMAND) {
      line_trans.set_byte_enable_ptr(GetStrbBuffer(slot, req_id));
      line_trans.set_byte_enable_length(cache_block_size_);
    }
  }
}

bool CoalescingOutstandingLsu::StackAddressRemapper(uint64_t *addr) {
  uint64_t original_addr = *addr;
  bool is_stack_access =
      stack_remap_table_
          ? stack_remap_table_->Matches(original_addr)
          : original_addr >= stack_start_ && original_addr <= stack_end_;
  if (enable_stack_remap_ && is_stack_access) {
    /**
     * Address Format (Before Remapping):
     *
     *  |       Base Address      |   TID   |  Local Stack Offset  |
     *  | 63        ...        12 | 11   10 | 9                  0 |
     *
     * Address Format (After Remapping):
     *
     *  Local Stack Offset is split into two parts based on data granularity.
     *    --> Part1: Granularity-aligned byte offset (low bits)
     *    --> Part2: Remaining stack index bits above the byte offset
     *
     *    For example, with 8-byte granularity:
     *      --> part1: bits [2:0] (3 bits for byte offset within 8 bytes)
     *      --> part2: bits [9:3] (7 bits for stack index)
     *
     *  Then move the stack index part above the TID bits,
     *  resulting in the following format:
     *
     *  | Base Address | Stack index | TID | Granularity-aligned byte offset |
     *  | 63        12 | ..       .. |     | ..                           .. |
     */

    // Extract fields from the original address.
    uint64_t local_stack_offset = original_addr & MASK_STACK;
    uint64_t tid = (original_addr >> BIT_WIDTH_STACK) & MASK_TID;
    uint64_t base_addr = original_addr & MASK_BASE;

    // Split the local stack offset and remap the stack index above TID.
    uint64_t byte_offset = local_stack_offset & MASK_GRANULARITY;
    uint64_t stack_index = local_stack_offset >> BIT_GRANULARITY;

    // Remapping address
    uint64_t new_index_part = stack_index << (BIT_WIDTH_TID + BIT_GRANULARITY);
    uint64_t new_tid_part = tid << BIT_GRANULARITY;
    *addr = base_addr | new_index_part | new_tid_part | byte_offset;
  }
  return is_stack_access;
}

void CoalescingOutstandingLsu::Coalesce(InflightSlot &slot) {
  Packet *packet = slot.packet;
  slot.line_reqs.clear();
  std::fill(slot.strb_buf.begin(), slot.strb_buf.end(), 0);
  std::fill(slot.line_buf.begin(), slot.line_buf.end(), 0);
  bool is_store = HasFlag(packet->flag, ExecFlag::STORE);
  bool is_load = HasFlag(packet->flag, ExecFlag::LOAD);
  uint32_t mem_size = GetMemSize(packet->flag);
  packet->is_stack_access = false;
  bool has_active_lane = false;

  // Coalesce memory accesses to the same cache line and prepare line buffers.
  for (uint32_t lane = 0; lane < num_lanes_; ++lane) {
    if (packet->tmask[lane] != 1) continue;
    bool is_stack_access = StackAddressRemapper(&packet->addr_buf[lane]);
    uint64_t line_addr = ToLineAddr(packet->addr_buf[lane]);
    uint64_t line_offset = ToLineOffset(packet->addr_buf[lane]);
    if (line_offset + mem_size > cache_block_size_) {
      LV_FATAL(
          "CoalescingOutstandingLsu does not support accesses crossing cache "
          "lines: lane={}, address={:#x}, size={}, cache_block_size={}",
          lane, packet->addr_buf[lane], mem_size, cache_block_size_);
    }
    auto line_iter =
        std::find(slot.line_reqs.begin(), slot.line_reqs.end(), line_addr);
    // A warp issues multiple accesses,
    // threads should all be stack accesses or all be non-stack accesses.
    if (has_active_lane) {
      assert(packet->is_stack_access == is_stack_access &&
             "Inconsistent stack access within the same warp");
    } else {
      packet->is_stack_access = is_stack_access;
      has_active_lane = true;
    }

    // Mark whether this access is a stack access for later use.
    (is_stack_access ? stats_.stack_lane_reqs : stats_.non_stack_lane_reqs)++;
    uint32_t line_id;
    if (line_iter == slot.line_reqs.end()) {
      line_id = slot.line_reqs.size();
      slot.line_reqs.push_back(line_addr);

      // Add request count for statistics only when creating a new line request.
      if (is_store)
        (is_stack_access ? stats_.stack_writes_line_reqs
                         : stats_.non_stack_writes_line_reqs)++;
      else if (is_load)
        (is_stack_access ? stats_.stack_reads_line_reqs
                         : stats_.non_stack_reads_line_reqs)++;
    } else
      line_id = std::distance(slot.line_reqs.begin(), line_iter);
    // Record the mapping from lane to line id for LOAD scattering.
    slot.lane_to_line[lane] = line_id;

    // Merge store data to line buffer and set byte enables in strb buffer.
    if (is_store) {
      std::memcpy(GetLineBuffer(slot, line_id) + line_offset,
                  &packet->data_buf[lane], mem_size);
      std::memset(GetStrbBuffer(slot, line_id) + line_offset, 0xff, mem_size);
    }
  }
  slot.total_reqs = slot.line_reqs.size();
}

void CoalescingOutstandingLsu::ScatterLoadData(InflightSlot &slot) {
  uint32_t mem_size = GetMemSize(slot.packet->flag);

  for (uint32_t lane = 0; lane < num_lanes_; ++lane) {
    if (slot.packet->tmask[lane] != 1) continue;
    uint32_t line_id = slot.lane_to_line[lane];
    uint64_t line_offset = ToLineOffset(slot.packet->addr_buf[lane]);

    std::memcpy(&slot.packet->data_buf[lane],
                GetLineBuffer(slot, line_id) + line_offset, mem_size);
    ExtendLane(slot.packet, lane);
  }
}

void CoalescingOutstandingLsu::ExtendLane(Packet *packet, uint32_t lane) {
  uint64_t value = packet->data_buf[lane];
  uint32_t mem_size = GetMemSize(packet->flag);
  if (IsSigned(packet->flag)) {
    if (mem_size == 1)
      value = static_cast<int8_t>(value);
    else if (mem_size == 2)
      value = static_cast<int16_t>(value);
    else if (mem_size == 4)
      value = static_cast<int32_t>(value);
  } else if (mem_size < 8)
    value &= (uint64_t{1} << (mem_size * 8)) - 1;
  packet->data_buf[lane] = value;
}

void CoalescingOutstandingLsu::FreeSlot(uint32_t slot_id) {
  auto &slot = inflight_slots_[slot_id];
  slot.valid = false;
  slot.packet = nullptr;
  slot.total_reqs = slot.received_reqs = 0;
  slot.line_reqs.clear();
  --in_flight_count_;
  free_slots_.push(slot_id);
}

void CoalescingOutstandingLsu::IssueMemReq() {
  for (;;) {
    while (free_slots_.empty() || barrier_state_ != BarrierState::kNone) {
      wait(slot_available_event_);
    }

    Packet *packet = lsu_req->get();

    // tmask all 0
    if (!packet->tmask.or_reduce()) {
      lsu_resp->put(packet);
      continue;
    }

    // atomic operations should wait all entries in inflight_slots responed
    bool is_atomic = HasFlag(packet->flag, ExecFlag::ATOMIC);
    if (is_atomic) {
      barrier_state_ = BarrierState::kDraining;
      while (in_flight_count_ > 0) wait(slot_available_event_);
      barrier_state_ = BarrierState::kAtomicInFlight;
    }

    uint32_t slot_id = free_slots_.front();
    free_slots_.pop();
    auto &slot = inflight_slots_[slot_id];
    slot.valid = true;
    slot.packet = packet;
    slot.received_reqs = 0;
    ++in_flight_count_;

    if (is_atomic) {
      SetupAtomicTrans(slot, GetMemSize(packet->flag),
                       DecodeAtomicOp(packet->flag));
      for (uint32_t lane = 0; lane < num_lanes_; ++lane)
        if (packet->tmask[lane] == 1)
          dmem_port_.req_port->write(&slot.trans[lane]);
    } else {
      Coalesce(slot);
      SetupLineTrans(slot, HasFlag(packet->flag, ExecFlag::STORE)
                               ? tlm::TLM_WRITE_COMMAND
                               : tlm::TLM_READ_COMMAND);
      for (uint32_t req_id = 0; req_id < slot.total_reqs; ++req_id)
        dmem_port_.req_port->write(&slot.trans[req_id]);
    }
  }
}

void CoalescingOutstandingLsu::CollectMemResp() {
  for (;;) {
    auto *resp = dmem_port_.resp_port->read();
    if (!resp->is_response_ok()) {
      LV_FATAL(
          "CoalescingOutstandingLsu received a failed memory response: "
          "address={:#x}, size={}, status={} ({})",
          resp->get_address(), resp->get_data_length(),
          static_cast<int>(resp->get_response_status()),
          resp->get_response_string());
    }

    auto *lsu_extension = resp->get_extension<LsuTransExtension>();
    assert(lsu_extension && lsu_extension->slot_id < inflight_slots_.size());

    auto &slot = inflight_slots_[lsu_extension->slot_id];
    assert(slot.valid && lsu_extension->req_id < num_lanes_);

    Packet *packet = slot.packet;
    bool is_atomic = HasFlag(packet->flag, ExecFlag::ATOMIC);
    if (is_atomic) {
      assert(packet->tmask[lsu_extension->req_id] == 1);
    } else {
      assert(lsu_extension->req_id < slot.total_reqs);
    }
    assert(slot.received_reqs < slot.total_reqs);

    if (++slot.received_reqs == slot.total_reqs) {
      if (is_atomic) {
        for (uint32_t lane = 0; lane < num_lanes_; ++lane)
          if (packet->tmask[lane] == 1) ExtendLane(packet, lane);
      } else if (HasFlag(packet->flag, ExecFlag::LOAD)) {
        // All line responses are now in the slot buffers; scatter once.
        ScatterLoadData(slot);
      }
      uint32_t slot_id = slot.slot_id;
      FreeSlot(slot_id);
      lsu_resp->put(packet);
      if (is_atomic) barrier_state_ = BarrierState::kNone;
      slot_available_event_.notify();
    }
  }
}

LV_BINDING_WITH_BASES(simtix, CoalescingOutstandingLsu, Lsu)
    .constructor(
        [](const char *name, const ArchParam &param,
           const CoalescingOutstandingLsu::Param &lsu_param) {
          return std::make_shared<CoalescingOutstandingLsu>(name, param,
                                                            lsu_param);
        },
        lv::params("name", "param", "lsu_param"),
        lv::doc("Create a coalescing LSU"))
    .property("stack_remap_table", &CoalescingOutstandingLsu::stack_remap_table,
              &CoalescingOutstandingLsu::set_stack_remap_table,
              lv::doc("Firmware-programmable stack remapping table"));
}  // namespace simtix::pipelined
