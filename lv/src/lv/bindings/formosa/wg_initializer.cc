// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "wg_initializer.h"

#include <fmt/core.h>
#include <liblv/binding.h>
#include <liblv/interfaces/warp_ctrl.h>
#include <sysc/datatypes/bit/sc_bv_base.h>
#include <systemc.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

bool CheckForErrorException(const sc_dt::sc_bv_base &mask,
                            const std::vector<uint64_t> &mcause,
                            const std::vector<int> &processing_wg_ids,
                            int wg_idx) {
  for (int i = 0; i < mask.length(); ++i) {
    if (mask[i].to_bool() && processing_wg_ids[i] == wg_idx &&
        mcause[i] != 11) {
      return true;  // Non-ecall exception
    }
  }
  return false;
}

int FindFirstExceptionWarp(const sc_dt::sc_bv_base &mask,
                           const std::vector<uint64_t> &mcause,
                           const std::vector<int> &processing_wg_ids,
                           int wg_idx, bool prefer_error) {
  int first = -1;
  for (int i = 0; i < mask.length(); ++i) {
    if (!mask[i].to_bool() || processing_wg_ids[i] != wg_idx) {
      continue;
    }
    if (first < 0) {
      first = i;
    }
    if (prefer_error && mcause[i] != 11) {
      return i;
    }
  }
  return first;
}

}  // namespace

namespace lv {
namespace formosa {

WGInitializer::WGInitializer(const sc_module_name &name, const Param &param)
    : sc_module(name),
      kWgResidentLimit(param.wg_resident_limit),
      kThreadsPerWarp(param.threads_per_warp),
      kWarpsPerCore(param.warps_per_core),
      sink_(
          "sink",
          [this](tlm::tlm_generic_payload &trans) {
            return ProcessRequest(&trans);
          },
          param.sink_fifo_size),
      core_(nullptr),
      dispatch_fifo_("dispatch_fifo", param.fifo_size),
      warp_cmd_fifo_("warp_cmd_fifo", param.fifo_size),
      dequeue_fifo_("dequeue_fifo", param.fifo_size),
      processing_wg_info_(param.wg_resident_limit),
      processing_wg_ids_(param.warps_per_core, -1),
      cwm_(static_cast<int>(kWarpsPerCore)),
      release_cwm_(static_cast<int>(kWarpsPerCore)),
      completed_warps_scratch_(param.wg_resident_limit, 0),
      pf_packets_(),
      on_wg_dispatch_(param.on_wg_dispatch),
      on_wg_retire_(param.on_wg_retire) {
  // Initialize the hardware info CSR
  csr_.wg_resident_limit = param.wg_resident_limit;
  wg_scratch_.reserve(param.wg_resident_limit);
  retire_infos_scratch_.reserve(param.wg_resident_limit);

  SC_THREAD(ProcessRequestThread);
  SC_THREAD(EnqueueWG);
  SC_THREAD(DequeueWG);
  SC_THREAD(ProcessThread);

  pf_packets_.reserve(param.wg_resident_limit);
  for (uint32_t i = 0; i < param.wg_resident_limit; ++i) {
    std::string track_name = fmt::format("WG slot {}", i);
    pf_packets_.push_back(LV_NEW_MODULE_TRACK(track_name));
    pf_packets_[i].set_enabled(param.enable_trace);
  }
}

WGInitializer::~WGInitializer() = default;

void WGInitializer::set_core(WarpCtrl *core) {
  core_ = core;
  if (core_ != nullptr) {
    core_->warp_cmd.bind(warp_cmd_fifo_);
  }
}

void WGInitializer::TraceWGSlotActiveBegin(int slot_idx) {
  [[maybe_unused]] const auto &info = processing_wg_info_[slot_idx];
  LV_TRACE_BEGIN(pf_packets_[slot_idx], "WGInitializer", "WG slot active",
                 LV_TRACE_ARG("slot", slot_idx),
                 LV_TRACE_ARG("kernel_pc", info.kernel_pc),
                 LV_TRACE_ARG("info_ptr", info.info_ptr),
                 LV_TRACE_ARG("group_size", info.group_size),
                 LV_TRACE_ARG("warp_count", info.warp_count));
}

void WGInitializer::TraceWGSlotInstant(int slot_idx, std::string_view name) {
  [[maybe_unused]] const auto &info = processing_wg_info_[slot_idx];
  LV_TRACE_INSTANT(pf_packets_[slot_idx], "WGInitializer", name,
                   LV_TRACE_ARG("slot", slot_idx),
                   LV_TRACE_ARG("kernel_pc", info.kernel_pc),
                   LV_TRACE_ARG("info_ptr", info.info_ptr),
                   LV_TRACE_ARG("group_size", info.group_size),
                   LV_TRACE_ARG("warp_count", info.warp_count),
                   LV_TRACE_ARG("activated_warps", info.activated_warps),
                   LV_TRACE_ARG("completed_warps", info.completed_warps));
}

void WGInitializer::ProcessRequestThread() {
  while (true) {
    // blocking call to get a request
    tlm::tlm_generic_payload *trans = sink_.req_port->read();
    ProcessRequest(trans);
    sink_.resp_port->write(trans);
  }
}

unsigned int WGInitializer::ProcessRequest(tlm::tlm_generic_payload *trans) {
  tlm::tlm_command cmd = trans->get_command();
  sc_dt::uint64 addr = trans->get_address();
  unsigned char *ptr = trans->get_data_ptr();
  unsigned int len = trans->get_data_length();

  if (addr > offsetof(CSR, wg_resident_limit) || addr % 8 != 0) {
    SC_REPORT_ERROR("TLM-2", "Illegal address received by WGInitializer");
    trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    return 0;
  }

  if (len != 8) {
    SC_REPORT_ERROR("TLM-2", "Illegal length received by WGInitializer");
    trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    return 0;
  }

  if (cmd == tlm::TLM_READ_COMMAND) {
    uint8_t *csr_mem = reinterpret_cast<uint8_t *>(&csr_);
    std::memcpy(ptr, &csr_mem[addr], len);
  } else if (cmd == tlm::TLM_WRITE_COMMAND &&
             addr <= offsetof(CSR, deq_valid)) {
    uint8_t *csr_mem = reinterpret_cast<uint8_t *>(&csr_);
    uint64_t old_enqueue_valid = csr_.enq_valid;
    uint64_t old_dequeue_valid = csr_.deq_valid;
    std::memcpy(&csr_mem[addr], ptr, len);
    if (old_enqueue_valid == 0 && csr_.enq_valid != 0) {
      enqueue_event_.notify(SC_ZERO_TIME);
    }
    if (old_dequeue_valid != 0 && csr_.deq_valid == 0) {
      dequeue_event_.notify(SC_ZERO_TIME);
    }
  } else {
    SC_REPORT_ERROR("TLM-2", "Illegal transaction command received by memory");
  }
  trans->set_response_status(tlm::TLM_OK_RESPONSE);
  return len;
}

void WGInitializer::EnqueueWG() {
  while (true) {
    wait(enqueue_event_);

    WGDispatchInfo info;
    info.kernel_pc = csr_.enq_kernel_pc;
    info.info_ptr = csr_.enq_info_ptr;
    info.group_size = csr_.enq_wg_size;

    dispatch_fifo_.write(info);
    // Clear only after the enqueue is fully accepted.
    csr_.enq_valid = 0;
  }
}

void WGInitializer::DequeueWG() {
  while (true) {
    csr_.deq_valid = 0;
    WGDequeueInfo info = dequeue_fifo_.read();
    csr_.deq_status = info.status;
    csr_.deq_kernel_pc = info.kernel_pc;
    csr_.deq_info_ptr = info.info_ptr;
    csr_.deq_mcause = info.mcause;
    csr_.deq_mepc = info.mepc;
    csr_.deq_mtval = info.mtval;
    csr_.deq_valid = 1;
    wait(dequeue_event_);
  }
}

void WGInitializer::ProcessThread() {
  while (true) {
    wait(core_->idle_mask_changed_event() | core_->active_mask_changed_event() |
         core_->barrier_mask_changed_event() |
         core_->exception_mask_changed_event() |
         dispatch_fifo_.data_written_event());
    HandleExceptionWarps();
    HandleBarrierWarps();
    DispatchRemainingWarps();
    DispatchNewWG();
  }
}

void WGInitializer::EmitWarpCtrlCommand(const WarpCtrlCommand &cmd) {
  warp_cmd_fifo_.put(cmd);
}

void WGInitializer::HandleExceptionWarps() {
  const sc_dt::sc_bv_base &active_mask = core_->active_mask();
  const sc_dt::sc_bv_base &barrier_mask = core_->barrier_mask();
  const sc_dt::sc_bv_base &exception_mask = core_->exception_mask();
  const auto &mcause = core_->mcause();
  const auto &mepc = core_->mepc();
  const auto &mtval = core_->mtval();

  if (exception_mask == 0) {
    return;
  }

  std::fill(completed_warps_scratch_.begin(), completed_warps_scratch_.end(),
            0);
  wg_scratch_.clear();
  retire_infos_scratch_.clear();

  cwm_ = 0;
  release_cwm_ = 0;
  for (int wg_idx = 0; wg_idx < static_cast<int>(processing_wg_info_.size());
       ++wg_idx) {
    if (!processing_wg_info_[wg_idx].busy) {
      continue;
    }

    int first_warp_index = FindFirstExceptionWarp(exception_mask, mcause,
                                                  processing_wg_ids_, wg_idx,
                                                  /*prefer_error=*/false);
    if (first_warp_index < 0) {
      continue;
    }

    bool has_error_exception = CheckForErrorException(
        exception_mask, mcause, processing_wg_ids_, wg_idx);
    if (has_error_exception) {
      wg_scratch_.push_back(wg_idx);
      for (int i = 0; i < cwm_.length(); ++i) {
        if ((active_mask[i].to_bool() || barrier_mask[i].to_bool()) &&
            processing_wg_ids_[i] == wg_idx) {
          cwm_[i] = 1;
        }
      }
    }

    for (int i = 0; i < exception_mask.length(); ++i) {
      if (exception_mask[i].to_bool() && processing_wg_ids_[i] == wg_idx) {
        release_cwm_[i] = 1;
        completed_warps_scratch_[wg_idx]++;
      }
    }

    uint64_t next_completed_warps =
        processing_wg_info_[wg_idx].completed_warps +
        completed_warps_scratch_[wg_idx];
    if (!has_error_exception &&
        processing_wg_info_[wg_idx].warp_count != next_completed_warps) {
      continue;
    }

    int report_warp_index =
        FindFirstExceptionWarp(exception_mask, mcause, processing_wg_ids_,
                               wg_idx, has_error_exception);
    WGDequeueInfo dequeue_info;
    dequeue_info.status =
        has_error_exception ? WGStatus::kWGException : WGStatus::kWGOkay;
    dequeue_info.kernel_pc = processing_wg_info_[wg_idx].kernel_pc;
    dequeue_info.info_ptr = processing_wg_info_[wg_idx].info_ptr;
    dequeue_info.mcause = mcause[report_warp_index];
    dequeue_info.mepc = mepc[report_warp_index];
    dequeue_info.mtval = mtval[report_warp_index];
    retire_infos_scratch_.emplace_back(wg_idx, dequeue_info);
  }

  if (cwm_ != 0) {
    EmitWarpCtrlCommand(WarpCtrlCommand::Abort(cwm_));
    for (int wg_idx : wg_scratch_) {
      TraceWGSlotInstant(wg_idx, "Abort warps on exception");
    }
  }

  if (release_cwm_ != 0) {
    EmitWarpCtrlCommand(WarpCtrlCommand::Release(release_cwm_));
  }

  for (int i = 0; i < cwm_.length(); ++i) {
    if (cwm_[i].to_bool() || release_cwm_[i].to_bool()) {
      processing_wg_ids_[i] = -1;
    }
  }

  for (int wg_idx = 0;
       wg_idx < static_cast<int>(completed_warps_scratch_.size()); ++wg_idx) {
    processing_wg_info_[wg_idx].completed_warps +=
        completed_warps_scratch_[wg_idx];
  }

  for (const auto &[wg_idx, dequeue_info] : retire_infos_scratch_) {
    dequeue_fifo_.write(dequeue_info);
    TraceWGSlotInstant(wg_idx, dequeue_info.status == WGStatus::kWGException
                                   ? "WG exception complete"
                                   : "WG complete");
    LV_TRACE_END(pf_packets_[wg_idx]);
    processing_wg_info_[wg_idx].busy = false;
    processing_wg_count_--;
    processing_wg_order_.erase(std::remove(processing_wg_order_.begin(),
                                           processing_wg_order_.end(), wg_idx),
                               processing_wg_order_.end());
    if (on_wg_retire_) {
      (*on_wg_retire_)(dequeue_info.kernel_pc, dequeue_info.info_ptr,
                       dequeue_info.mcause, dequeue_info.mepc,
                       dequeue_info.mtval);
    }
  }
}

void WGInitializer::HandleBarrierWarps() {
  const sc_dt::sc_bv_base &barrier_mask = core_->barrier_mask();

  if (barrier_mask == 0) {
    return;
  }

  cwm_ = 0;
  wg_scratch_.clear();
  for (int wg_idx = 0; wg_idx < static_cast<int>(processing_wg_info_.size());
       ++wg_idx) {
    if (!processing_wg_info_[wg_idx].busy) {
      continue;
    }

    // The number of warps reaching this WG's barrier.
    uint32_t barrier_count = 0;
    for (int i = 0; i < barrier_mask.length(); ++i) {
      if (barrier_mask[i].to_bool()) {
        barrier_count += (processing_wg_ids_[i] == wg_idx);
      }
    }

    if (barrier_count != processing_wg_info_[wg_idx].warp_count) {
      continue;
    }

    for (int i = 0; i < cwm_.length(); ++i) {
      if (barrier_mask[i].to_bool() && processing_wg_ids_[i] == wg_idx) {
        cwm_[i] = 1;
      }
    }
    wg_scratch_.push_back(wg_idx);
  }

  if (cwm_ == 0) {
    return;
  }

  EmitWarpCtrlCommand(WarpCtrlCommand::Resume(cwm_));
  for (int wg_idx : wg_scratch_) {
    TraceWGSlotInstant(wg_idx, "Barrier resume");
  }
}

void WGInitializer::DispatchRemainingWarps() {
  const sc_dt::sc_bv_base &idle_mask = core_->idle_mask();

  if (processing_wg_order_.empty() || idle_mask == 0) {
    // No processing WG or no idle warp, nothing to dispatch
    return;
  }

  // Find the first processing WG in the order that has remaining warps to
  // dispatch
  auto it = std::find_if(processing_wg_order_.begin(),
                         processing_wg_order_.end(), [this](int wg_idx) {
                           return processing_wg_info_[wg_idx].activated_warps <
                                  processing_wg_info_[wg_idx].warp_count;
                         });

  if (it == processing_wg_order_.end()) {
    // No processing WG has remaining warps to dispatch
    return;
  }

  int current_wg_idx = *it;
  int remaining_warps = processing_wg_info_[current_wg_idx].warp_count -
                        processing_wg_info_[current_wg_idx].activated_warps;

  cwm_ = 0;
  int warps_to_activate = 0;
  for (int i = 0; i < idle_mask.length() && warps_to_activate < remaining_warps;
       ++i) {
    if (idle_mask[i].to_bool() && processing_wg_ids_[i] < 0) {
      cwm_[i] = 1;
      warps_to_activate++;
    }
  }
  if (warps_to_activate == 0) {
    return;
  }

  auto &info = processing_wg_info_[current_wg_idx];
  EmitWarpCtrlCommand(WarpCtrlCommand::Activate(
      cwm_, info.kernel_pc, info.info_ptr, info.activated_warps));

  processing_wg_info_[current_wg_idx].activated_warps += warps_to_activate;
  for (int i = 0; i < cwm_.length(); ++i) {
    if (cwm_[i].to_bool()) {
      processing_wg_ids_[i] = current_wg_idx;
    }
  }
  TraceWGSlotInstant(current_wg_idx, "Activate remaining warps");
}

void WGInitializer::DispatchNewWG() {
  const sc_dt::sc_bv_base &idle_mask = core_->idle_mask();

  if (dispatch_fifo_.num_available() > 0 &&
      processing_wg_count_ < kWgResidentLimit && idle_mask != 0) {
    bool has_available_idle_warp = false;
    for (int i = 0; i < idle_mask.length(); ++i) {
      if (idle_mask[i].to_bool() && processing_wg_ids_[i] < 0) {
        has_available_idle_warp = true;
        break;
      }
    }
    if (!has_available_idle_warp) {
      return;
    }

    int vacant_entry = FindVacantProcessingWG();
    if (vacant_entry < 0) {
      return;
    }

    WGDispatchInfo info;
    dispatch_fifo_.nb_read(info);

    int warp_count = (info.group_size + kThreadsPerWarp - 1) / kThreadsPerWarp;
    assert(static_cast<uint32_t>(warp_count) <= kWarpsPerCore);

    // Assume that we are going to activate all idle warps.
    cwm_ = 0;
    int warps_to_activate = 0;
    for (int i = 0; i < idle_mask.length() && warps_to_activate < warp_count;
         ++i) {
      if (idle_mask[i].to_bool() && processing_wg_ids_[i] < 0) {
        cwm_[i] = 1;
        warps_to_activate++;
      }
    }
    if (warps_to_activate == 0) {
      return;
    }

    // Activate warps
    EmitWarpCtrlCommand(
        WarpCtrlCommand::Activate(cwm_, info.kernel_pc, info.info_ptr, 0));

    ProcessingWGInfo wg_info;
    wg_info.busy = 1;
    wg_info.kernel_pc = info.kernel_pc;
    wg_info.info_ptr = info.info_ptr;
    wg_info.group_size = info.group_size;
    wg_info.warp_count = warp_count;
    wg_info.activated_warps = warps_to_activate;
    wg_info.completed_warps = 0;
    processing_wg_info_[vacant_entry] = wg_info;
    processing_wg_count_++;
    for (int i = 0; i < cwm_.length(); ++i) {
      if (cwm_[i].to_bool()) {
        processing_wg_ids_[i] = vacant_entry;
      }
    }
    processing_wg_order_.push_back(vacant_entry);
    TraceWGSlotActiveBegin(vacant_entry);
    TraceWGSlotInstant(vacant_entry, "Dispatch new WG");
    if (on_wg_dispatch_) {
      (*on_wg_dispatch_)(wg_info.kernel_pc, wg_info.info_ptr,
                         wg_info.warp_count);
    }
  }
}

using namespace std::literals::string_view_literals;  // NOLINT

LV_BINDING(formosa, WGInitializer)
    .constructor(
        [](const char *name, const WGInitializer::Param &param) {
          return std::make_shared<WGInitializer>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a work-group initializer"))
    .property("port", &WGInitializer::port,
              lv::doc("Work-group control MMIO port"))
    .property("warp_ctrl_target", &WGInitializer::core,
              &WGInitializer::set_core, lv::doc("Warp-control target"));

}  // namespace formosa
}  // namespace lv
