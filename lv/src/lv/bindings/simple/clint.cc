// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/output.h>
#include <sysc/kernel/sc_time.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cstdint>
#include <memory>
#include <sol/raii.hpp>
#include <vector>

namespace simple {

using lv::TlmSink;
using lv::Warning;

class Clint : public sc_module {
 public:
  using IntSignal = sc_signal<bool>;

  explicit Clint(const sc_module_name &name, size_t num_cores = 1)
      : sc_module(name),
        num_cores_(num_cores),
        timer_irq_("timer_irq", num_cores),
        msip_irq_("msip_irq", num_cores),
        clock_i_("clock"),
        msip_(num_cores, 0),
        mtimecmp_(num_cores, std::numeric_limits<uint64_t>::max()),
        mtime_(0),
        sink_(
            "sink",
            [this](tlm::tlm_generic_payload &trans) {
              return ProcessRequest(&trans);
            },
            16) {
    for (uint64_t i = 0; i < num_cores; ++i) {
      timer_irq_ptrs_.emplace_back(&timer_irq_[i]);
      msip_irq_ptrs_.emplace_back(&msip_irq_[i]);

      // Initialize timer irq and msip irq signals to false
      timer_irq_[i].write(false);
      msip_irq_[i].write(false);
    }

    SC_METHOD(MTimeMethod);
    sensitive << clock_i_.pos();
    dont_initialize();

    SC_METHOD(RequestMethod);
  }

  ~Clint() override = default;

  void set_clock(sc_clock *clock) {
    clock_ = clock;
    clock_i_.bind(*clock_);
  }

  sc_clock *clock() const { return clock_; }

  auto port() const { return &sink_.port; }

  sol::as_table_t<std::vector<IntSignal *>> timer_irq() {
    return sol::as_table(timer_irq_ptrs_);
  }

  sol::as_table_t<std::vector<IntSignal *>> msip_irq() {
    return sol::as_table(msip_irq_ptrs_);
  }

 private:
  size_t num_cores_;
  // signal holder
  sc_vector<IntSignal> timer_irq_;
  sc_vector<IntSignal> msip_irq_;
  // vector of signal pointers
  std::vector<IntSignal *> timer_irq_ptrs_;
  std::vector<IntSignal *> msip_irq_ptrs_;

  sc_event msip_modify_event_;

  sc_clock *clock_;
  sc_in<bool> clock_i_;

  // registers
  std::vector<uint32_t> msip_;      // Machine Software Interrupt Pending
  std::vector<uint64_t> mtimecmp_;  // Machine Timer Compare
  uint64_t mtime_;

  TlmSink sink_;

  /**
   * Update mtime and set the mtime irq per cycle
   */
  void MTimeMethod() {
    for (size_t i = 0; i < num_cores_; ++i) {
      timer_irq_[i].write((mtime_ >= mtimecmp_[i]));
    }
    mtime_ += 1;
  }

  void RequestMethod() {
    bool is_ready =
        sink_.req_port->num_available() > 0 && sink_.resp_port->num_free() > 0;
    // Wait until there is a request and we can put a response
    if (!is_ready) {
      next_trigger(sink_.req_port->data_written_event() |
                   sink_.resp_port->data_read_event());
      return;
    }

    // Only process on positive clock edge
    if (clock_->posedge()) {
      // Process one request per cycle
      tlm::tlm_generic_payload *trans;
      sink_.req_port->nb_read(trans);
      trans->acquire();
      ProcessRequest(trans);
      sink_.resp_port->nb_write(trans);
      trans->release();
    }

    next_trigger(clock_->posedge_event());
  }

  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans) {
    auto cmd = trans->get_command();
    auto addr = trans->get_address();
    auto ptr = trans->get_data_ptr();
    auto len = trans->get_data_length();

    if (addr < 0x4000) {  // Access msip
      // check alignment
      if (addr % 4 != 0) {
        Warning("{}: msip read not aligned to 4 bytes at address 0x{:x}",
                name(), addr);
        trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return 0;
      }
      if (len != 4) {
        Warning("{}: msip read with invalid length {} at address 0x{:x}",
                name(), len, addr);
        trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return 0;
      }
      // Check core ID
      unsigned int core_id = addr / 4;
      if (core_id >= num_cores_) {
        Warning("{}: msip read for invalid core id {} at address 0x{:x}",
                name(), core_id, addr);
        trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return 0;
      }
      // Access msip
      if (cmd == tlm::TLM_READ_COMMAND) {
        memcpy(ptr, &msip_[core_id], len);
      } else if (cmd == tlm::TLM_WRITE_COMMAND) {
        uint32_t value = *reinterpret_cast<uint32_t *>(ptr);
        if (value > 1) {
          Warning("{}: msip write with invalid value {} at address 0x{:x}",
                  name(), value, addr);
        }
        msip_[core_id] = value & 1;  // Only allow 0 or 1

        msip_irq_[core_id].write(msip_[core_id] & 1);
      } else {
        SC_REPORT_ERROR("TLM-2",
                        "Illegal transaction command received by CLINT");
      }
    } else if (addr < 0xC000) {  // Access mtime or mtimecmp
      // check alignment
      if (addr % 4 != 0) {
        Warning(
            "{}: mtime registers read not aligned to 4 bytes at address "
            "0x{:x}\n",
            name(), addr);
        trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return 0;
      } else if (len != 8 && len != 4) {
        Warning(
            "{}: mtime registers read with invalid length {} at address "
            "0x{:x}\n",
            name(), len, addr);
        trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return 0;
      }

      if (addr >= 0xBFF8) {
        // Access mtime with 32-bit or 64-bit alignment
        size_t word_offset = (addr - 0xBFF8) / 4;
        uint32_t *word_ptr = reinterpret_cast<uint32_t *>(&mtime_);
        if (cmd == tlm::TLM_READ_COMMAND) {
          memcpy(ptr, &word_ptr[word_offset], len);
        } else if (cmd == tlm::TLM_WRITE_COMMAND) {
          memcpy(&word_ptr[word_offset], ptr, len);
        } else {
          SC_REPORT_ERROR("TLM-2",
                          "Illegal transaction command received by CLINT");
        }
        return len;
      }

      // Access mtimecmp
      // Check core ID
      unsigned int core_id = (addr - 0x4000) / 8;
      if (core_id >= num_cores_) {
        Warning("{}: mtimecmp read for invalid core id {} at address 0x{:x}",
                name(), core_id, addr);
        trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return 0;
      }
      // Access mtimecmp
      size_t word_offset = (addr - 0x4000 - core_id * 8) / 4;
      uint32_t *word_ptr = reinterpret_cast<uint32_t *>(&mtimecmp_[core_id]);
      if (cmd == tlm::TLM_READ_COMMAND) {
        memcpy(ptr, &word_ptr[word_offset], len);
      } else if (cmd == tlm::TLM_WRITE_COMMAND) {
        memcpy(&word_ptr[word_offset], ptr, len);
      } else {
        SC_REPORT_ERROR("TLM-2",
                        "Illegal transaction command received by CLINT");
      }
    } else {
      Warning("{}: CLINT access out of range at address 0x{:x}", name(), addr);
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return 0;
    }
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    return len;
  }
};

LV_BINDING(simple, Clint)
    .constructor(
        [](const char *name, const size_t num_cores) {
          return std::make_shared<Clint>(name, num_cores);
        },
        lv::params("name", "num_cores"),
        lv::doc("Create a core-local interrupt controller"))
    .property("port", &Clint::port, lv::doc("CLINT request port"))
    .property("clock", &Clint::clock, &Clint::set_clock,
              lv::doc("SystemC clock"))
    .property("timer_irq", &Clint::timer_irq,
              lv::doc("Timer interrupt signals"))
    .property("msip_irq", &Clint::msip_irq,
              lv::doc("Software interrupt signals"));

}  // namespace simple
