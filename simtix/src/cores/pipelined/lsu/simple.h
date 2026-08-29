/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/ip_extension.h>
#include <liblv/mm/static.h>

#include <memory>
#include <type_traits>
#include <vector>

#include "cores/pipelined/lsu/base.h"

namespace simtix::pipelined {

class SimpleLsu : public Lsu {
 public:
  SimpleLsu(const sc_module_name &name, const ArchParam &param)
      : Lsu(name), num_lanes_(param.num_lanes), trans_(param.num_lanes) {
    exts_.reserve(num_lanes_);
    atomic_exts_.reserve(num_lanes_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      trans_[i].set_mm(lv::mm::Static);
      exts_.emplace_back(new lv::IpExtension);
      atomic_exts_.emplace_back(new AtomicExtension);
    }
    SC_THREAD(HandleProc);
    SC_THREAD(IssueMemReq);
    SC_THREAD(CollectMemResp);
  }

  ~SimpleLsu() override { ClearAtomicExtensions(trans_); }

 protected:
  void SetupTrans(tlm::tlm_command command, uint32_t len);
  void SetupAtomicTrans(uint32_t len, AtomicExtension::Op op);

  template <typename RetT, typename CastT>
  void SignExtensionImpl() {
    assert(packet_ != nullptr);
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
  const uint32_t num_lanes_;
  std::vector<std::unique_ptr<lv::IpExtension>> exts_;
  std::vector<std::unique_ptr<AtomicExtension>> atomic_exts_;
  std::vector<tlm::tlm_generic_payload> trans_;
};

}  // namespace simtix::pipelined
