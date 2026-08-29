// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/lsu/coalescing.h"

#include <liblv/binding.h>

#include "cores/exec_flag.h"

namespace simtix::pipelined {

void CoalescingLsu::SetupAtomicTrans(uint32_t len, AtomicExtension::Op op) {
  assert(packet_ != nullptr);
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet_->tmask[i] == 1) {
      auto &lane_trans = trans_[i];
      lane_trans.clear_extension<AtomicExtension>();
      lane_trans.set_command(tlm::TLM_READ_COMMAND);
      lane_trans.set_address(packet_->addr_buf[i]);
      lane_trans.set_data_length(len);
      lane_trans.set_data_ptr(
          reinterpret_cast<unsigned char *>(&packet_->data_buf[i]));
      lane_trans.set_byte_enable_ptr(nullptr);
      lane_trans.set_byte_enable_length(0);
      lane_trans.set_streaming_width(len);
      lane_trans.set_dmi_allowed(false);
      lane_trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
      lane_trans.set_extension(exts_[i].get());
      exts_[i]->ip = packet_->wpc;
    }
  }

  SetupAtomicExtensions(packet_->tmask, atomic_exts_, trans_, op);
}

void CoalescingLsu::SetupLineTrans(tlm::tlm_command command) {
  assert(packet_ != nullptr);
  for (int i = 0; i < line_reqs_.size(); ++i) {
    auto &line_trans = trans_[i];
    line_trans.clear_extension<AtomicExtension>();
    line_trans.set_command(command);
    line_trans.set_address(line_reqs_[i]);
    line_trans.set_data_length(cache_block_size_);
    line_trans.set_data_ptr(
        reinterpret_cast<unsigned char *>(GetLineBuffer(i)));
    line_trans.set_byte_enable_ptr(nullptr);
    line_trans.set_byte_enable_length(0);
    line_trans.set_streaming_width(cache_block_size_);
    line_trans.set_dmi_allowed(false);
    line_trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    line_trans.set_extension(exts_[i].get());
    exts_[i]->ip = packet_->wpc;

    // For store, we need to set byte enables
    if (command == tlm::TLM_WRITE_COMMAND) {
      line_trans.set_byte_enable_ptr(GetStrbBuffer(i));
      line_trans.set_byte_enable_length(cache_block_size_);
    }
  }
}

bool CoalescingLsu::StackAddressRemapper(uint64_t *addr) {
  uint64_t original_addr = *addr;
  bool is_stack =
      stack_remap_table_ != nullptr
          ? stack_remap_table_->Matches(original_addr)
          : original_addr >= stack_start_ && original_addr <= stack_end_;

  if (enable_stack_remap_ && is_stack) {
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
     *
     */

    // Extract fields from the original address
    uint64_t local_stack_offset = original_addr & MASK_STACK;
    uint64_t tid = (original_addr >> BIT_WIDTH_STACK) & MASK_TID;
    uint64_t base_addr = original_addr & MASK_BASE;

    // Split local stack offset into byte offset and stack index
    uint64_t byte_offset = local_stack_offset & MASK_GRANULARITY;
    uint64_t stack_index = local_stack_offset >> BIT_GRANULARITY;

    // Remapping address
    uint64_t new_index_part = stack_index << (BIT_WIDTH_TID + BIT_GRANULARITY);
    uint64_t new_tid_part = tid << BIT_GRANULARITY;
    *addr = base_addr | new_index_part | new_tid_part | byte_offset;
  }

  return is_stack;
}

void CoalescingLsu::Coalesce() {
  line_reqs_.clear();
  std::fill(strb_buf_.begin(), strb_buf_.end(), 0);
  std::fill(line_buf_.begin(), line_buf_.end(), 0);

  bool is_store = HasFlag(packet_->flag, ExecFlag::STORE);
  bool is_load = HasFlag(packet_->flag, ExecFlag::LOAD);
  uint32_t mem_size = GetMemSize(packet_->flag);

  // Coalesce memory accesses to the same cache line and prepare line buffers.
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet_->tmask[i] != 1) continue;

    bool is_stack_access = StackAddressRemapper(&packet_->addr_buf[i]);
    uint64_t line_addr = ToLineAddr(packet_->addr_buf[i]);
    uint64_t line_offset = ToLineOffset(packet_->addr_buf[i]);
    assert(line_offset + mem_size <= cache_block_size_ &&
           "CoalescingLsu does not support accesses crossing cache lines");
    auto it = std::find(line_reqs_.begin(), line_reqs_.end(), line_addr);

    // A warp issues multiple accesses,
    // threads should all be stack accesses or all be non-stack accesses,
    if (packet_->is_stack_access) {
      assert(is_stack_access &&
             "Inconsistent stack access within the same warp");
    }

    // Mark whether this access is a stack access for later use
    packet_->is_stack_access |= is_stack_access;
    (is_stack_access ? stats_.stack_lane_reqs : stats_.non_stack_lane_reqs)++;

    uint32_t line_id;
    if (it == line_reqs_.end()) {
      line_id = line_reqs_.size();
      line_reqs_.push_back(line_addr);

      // Add request count for statistics only when creating a new line request
      if (is_store) {
        (is_stack_access ? stats_.stack_writes_line_reqs
                         : stats_.non_stack_writes_line_reqs)++;
      } else if (is_load) {
        (is_stack_access ? stats_.stack_reads_line_reqs
                         : stats_.non_stack_reads_line_reqs)++;
      }

    } else {
      line_id = std::distance(line_reqs_.begin(), it);
    }

    // Record the mapping from lane to line id for LOAD scattering
    lane_to_line_[i] = line_id;

    // Merge store data to line buffer and set byte enables in strb buffer
    if (is_store) {
      uint8_t *line_buf = GetLineBuffer(line_id);
      uint8_t *strb_buf = GetStrbBuffer(line_id);

      std::memcpy(line_buf + line_offset, &packet_->data_buf[i], mem_size);
      std::memset(strb_buf + line_offset, 0xFF, mem_size);
    }
  }
}

void CoalescingLsu::ScatterLoadData() {
  uint32_t mem_size = GetMemSize(packet_->flag);
  bool is_signed = IsSigned(packet_->flag);

  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet_->tmask[i] != 1) continue;

    uint32_t line_id = lane_to_line_[i];
    uint64_t line_offset = ToLineOffset(packet_->addr_buf[i]);
    assert(line_offset + mem_size <= cache_block_size_ &&
           "CoalescingLsu does not support accesses crossing cache lines");
    uint8_t *line_buf = GetLineBuffer(line_id);

    std::memcpy(&packet_->data_buf[i], line_buf + line_offset, mem_size);
  }
}

void CoalescingLsu::HandleProc() {
  for (;;) {
    packet_ = lsu_req->get();
    auto flag = packet_->flag;

    if (HasFlag(flag, ExecFlag::ATOMIC)) {
      SetupAtomicTrans(GetMemSize(flag), simtix::DecodeAtomicOp(flag));
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
      SignExtension(GetMemSize(flag), IsSigned(flag));
    }

    if (HasFlag(flag, ExecFlag::LOAD)) {
      Coalesce();
      SetupLineTrans(tlm::TLM_READ_COMMAND);
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
      ScatterLoadData();
      SignExtension(GetMemSize(flag), IsSigned(flag));
    }

    if (HasFlag(flag, ExecFlag::STORE)) {
      Coalesce();
      SetupLineTrans(tlm::tlm_command::TLM_WRITE_COMMAND);
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
    }

    lsu_resp->put(packet_);
  }
}

void CoalescingLsu::IssueMemReq() {
  for (;;) {
    wait(start_issuing_mem_req_);

    if (HasFlag(packet_->flag, ExecFlag::ATOMIC)) {
      for (uint32_t i = 0; i < num_lanes_; ++i) {
        if (packet_->tmask[i] == 1) {
          dmem_port_.req_port->write(&trans_[i]);
        }
      }
    }

    if (HasFlag(packet_->flag, ExecFlag::LOAD) ||
        HasFlag(packet_->flag, ExecFlag::STORE)) {
      for (size_t i = 0; i < line_reqs_.size(); ++i) {
        dmem_port_.req_port->write(&trans_[i]);
      }
    }

    done_issuing_mem_req_.notify();
  }
}

void CoalescingLsu::CollectMemResp() {
  for (;;) {
    wait(start_collecting_mem_resp_);

    if (HasFlag(packet_->flag, ExecFlag::ATOMIC)) {
      for (uint32_t i = 0; i < num_lanes_; ++i) {
        if (packet_->tmask[i] == 1) {
          auto *resp = dmem_port_.resp_port->read();
          assert(resp->is_response_ok());
        }
      }
    }

    if (HasFlag(packet_->flag, ExecFlag::LOAD) ||
        HasFlag(packet_->flag, ExecFlag::STORE)) {
      for (size_t i = 0; i < line_reqs_.size(); ++i) {
        auto *resp = dmem_port_.resp_port->read();
        assert(resp->is_response_ok());
      }
    }

    done_collecting_mem_resp_.notify();
  }
}

LV_BINDING_WITH_BASES(simtix, CoalescingLsu, Lsu)
    .constructor(
        [](const char *name, const ArchParam &param,
           const CoalescingLsu::Param &lsu_param) {
          return std::make_shared<CoalescingLsu>(name, param, lsu_param);
        },
        lv::params("name", "param", "lsu_param"),
        lv::doc("Create a coalescing LSU"))
    .property("stack_remap_table", &CoalescingLsu::stack_remap_table,
              &CoalescingLsu::set_stack_remap_table,
              lv::doc("Firmware-programmable stack remapping table"));

}  // namespace simtix::pipelined
