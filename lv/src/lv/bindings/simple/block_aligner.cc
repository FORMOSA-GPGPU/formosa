// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/mm/pool.h>
#include <sysc/kernel/sc_module.h>
#include <sysc/kernel/sc_thread_process.h>
#include <sysc/utils/sc_report.h>
#include <tlm.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>
#include <tlm_utils/simple_target_socket.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <systemc>
#include <unordered_map>
#include <utility>

namespace simple {
using lv::TlmSink;
using lv::TlmSource;
class BlockAligner : public sc_module {
 public:
  explicit BlockAligner(const sc_module_name &name, unsigned int align_size)
      : sc_module(name),
        from_("from_", nullptr),
        to_("to_"),
        align_size_(align_size),
        addr_align_mask_(~(align_size - 1)) {
    if (align_size_ < 2) SC_REPORT_ERROR("BlockAligner", "no need to align");
    if (!is_pow_of_2(align_size_))
      SC_REPORT_ERROR("BlockAligner", "align_size_ must be pow of 2");

    SC_THREAD(handle_raw_to_align_fw);
    SC_THREAD(handle_align_to_raw_bw);
  }

  // For lua binding
  using Target =
      tlm_utils::simple_initiator_socket<BlockAligner>::base_target_socket_type;
  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;

  Source *from_port() const;
  void set_to_target(Target *t);
  Target *to_target() const;

 private:
  // tlm transaction + unique ptr for byte_enable + data_ptr
  struct trans_payloads {
    tlm::tlm_generic_payload *tlm_trans;
    std::unique_ptr<unsigned char[]> data_ptr;
    std::unique_ptr<unsigned char[]> byte_enable_ptr;
  };

  lv::TlmSink from_;
  lv::TlmSource to_;
  Target *to_target_;
  std::unordered_map<tlm::tlm_generic_payload *, trans_payloads>
      aligned_to_raw_trans_;
  const uint64_t align_size_;
  const uint64_t addr_align_mask_;
  bool is_pow_of_2(const uint64_t &);
  void check_raw_trans(const tlm::tlm_generic_payload &);
  void handle_raw_to_align_fw();
  void handle_align_to_raw_bw();
};

inline bool BlockAligner::is_pow_of_2(const uint64_t &val) {
  return (val > 0) && ((val & (val - 1)) == 0);
}

inline void BlockAligner::check_raw_trans(
    const tlm::tlm_generic_payload &raw_trans) {
  auto raw_addr = raw_trans.get_address();
  auto aligned_addr = raw_addr & addr_align_mask_;
  auto raw_strb_len = raw_trans.get_byte_enable_length();
  auto raw_data_len = raw_trans.get_data_length();
  uint32_t offset = raw_addr - aligned_addr;
  auto raw_strb_ptr = raw_trans.get_byte_enable_ptr();
  auto raw_data_ptr = raw_trans.get_data_ptr();

  if (raw_data_len == 0) SC_REPORT_ERROR("BlockAligner", "data_len must > 0");

  if (raw_data_ptr == 0) SC_REPORT_ERROR("BlockAligner", "data_ptr is nullptr");

  if (raw_strb_len) {
    if (!raw_strb_ptr)
      SC_REPORT_ERROR("BlockAligner",
                      "strb length must be 0 when strb ptr is null");

    if (!is_pow_of_2(raw_strb_len))
      SC_REPORT_ERROR("BlockAligner", "raw_strb_len must be power of two");
  }

  if (!is_pow_of_2(raw_data_len))
    SC_REPORT_ERROR("BlockAligner", "raw_data_len must be power of two");

  if (offset + raw_strb_len > align_size_ ||
      offset + raw_data_len > align_size_) {
    SC_REPORT_ERROR("BlockAligner", "Currently not handling across line req");
  }

  if (raw_strb_len && raw_strb_len != raw_data_len)
    SC_REPORT_ERROR("BlockAligner", "raw_strb_len should equal raw_data_len");
}

void BlockAligner::handle_raw_to_align_fw() {
  while (true) {
    auto *raw_trans = from_.req_port->read();
    raw_trans->acquire();
    auto raw_addr = raw_trans->get_address();
    auto aligned_addr = raw_addr & addr_align_mask_;
    auto raw_strb_ptr = raw_trans->get_byte_enable_ptr();
    auto raw_strb_len = raw_trans->get_byte_enable_length();
    auto raw_data_ptr = raw_trans->get_data_ptr();
    auto raw_data_len = raw_trans->get_data_length();
    check_raw_trans(*raw_trans);

    bool is_write = raw_trans->is_write();
    uint32_t offset = raw_addr - aligned_addr;
    std::unique_ptr<unsigned char[]> aligned_strb_(
        new unsigned char[align_size_]);
    std::unique_ptr<unsigned char[]> aligned_data_ptr_(
        new unsigned char[align_size_]);
    memset(aligned_strb_.get(), 0, align_size_);
    memset(aligned_data_ptr_.get(), 0, align_size_);

    if (raw_strb_ptr && raw_strb_len) {
      memcpy(aligned_strb_.get() + offset, raw_strb_ptr, raw_strb_len);
    } else {
      // byte_enable ptr is nullptr, write at most data_len bytes
      memset(aligned_strb_.get() + offset, TLM_BYTE_ENABLED, raw_data_len);
    }

    if (raw_data_ptr && is_write) {
      memcpy(aligned_data_ptr_.get() + offset, raw_data_ptr, raw_data_len);
    }

    auto *aligned_trans = lv::mm::Pool::Allocate();
    aligned_trans->set_command(raw_trans->get_command());
    aligned_trans->set_address(aligned_addr);
    aligned_trans->set_byte_enable_length(is_write ? align_size_ : 0);
    aligned_trans->set_byte_enable_ptr(is_write ? aligned_strb_.get()
                                                : nullptr);
    aligned_trans->set_data_length(align_size_);
    aligned_trans->set_data_ptr(aligned_data_ptr_.get());
    aligned_trans->set_streaming_width(align_size_);
    aligned_trans->acquire();
    aligned_to_raw_trans_[aligned_trans] = {
        raw_trans, std::move(aligned_data_ptr_), std::move(aligned_strb_)};
    to_.req_port->write(aligned_trans);
  }
}

void BlockAligner::handle_align_to_raw_bw() {
  while (true) {
    auto *aligned_trans = to_.resp_port->read();
    auto it = aligned_to_raw_trans_.find(aligned_trans);
    if (it == aligned_to_raw_trans_.end() || it->second.tlm_trans == nullptr) {
      std::cerr << "Addr: " << aligned_trans->get_address() << " with cmd "
                << (aligned_trans->is_write() ? "write" : "read")
                << " has no corresponding raw trans for aligned_trans\n";
      SC_REPORT_ERROR("BlockAligner",
                      "No corresponding raw trans for aligned_trans");
    }
    auto *raw_trans = it->second.tlm_trans;
    raw_trans->set_response_status(aligned_trans->get_response_status());
    auto raw_addr = raw_trans->get_address();
    auto aligned_addr = raw_addr & addr_align_mask_;
    uint32_t offset = raw_addr - aligned_addr;

    if (raw_trans->is_read()) {
      memcpy(raw_trans->get_data_ptr(), aligned_trans->get_data_ptr() + offset,
             raw_trans->get_data_length());
    }

    aligned_trans->set_data_ptr(nullptr);
    aligned_trans->set_byte_enable_ptr(nullptr);
    aligned_trans->release();
    aligned_to_raw_trans_.erase(aligned_trans);
    from_.resp_port->write(raw_trans);
    raw_trans->release();
  }
}

// Interface for lua bind

BlockAligner::Source *BlockAligner::from_port() const { return &from_.port; }

BlockAligner::Target *BlockAligner::to_target() const { return to_target_; }

void BlockAligner::set_to_target(Target *t) {
  to_target_ = t;
  to_.set_target(t);
}

LV_BINDING(simple, BlockAligner)
    .constructor(
        [](const char *name, uint32_t align_size) {
          return std::make_shared<BlockAligner>(name, align_size);
        },
        lv::params("name", "align_size"), lv::doc("Create a TLM block aligner"))
    .property("from", &BlockAligner::from_port,
              lv::doc("Unaligned request port"))
    .property("to", &BlockAligner::to_target, &BlockAligner::set_to_target,
              lv::doc("Aligned memory target"));

}  // namespace simple
