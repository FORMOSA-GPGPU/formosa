// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <ilha/cache_command.h>
#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/output.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_master_slave_ifs.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ilha {

using lv::TlmSink;

class CacheController : public sc_module {
 public:
  using CacheCommand = ilha::CacheCommand;
  using BankCommandIf = tlm::tlm_master_if<CacheCommand, bool>;

  struct Param {
    bool verbose = false;
    int64_t num_banks = 0;
    LV_SCHEMA(CacheController, Param,
              LV_FIELD(verbose, "Whether or not to output debug info"),
              LV_FIELD(num_banks,
                       "Number of downstream maintenance ports. 0 degenerates "
                       "to a dummy MMIO component"))
  };

  sc_in<bool> clk;

  CacheController(const sc_module_name &name, const Param &param);

  void set_clock(std::shared_ptr<sc_clock> clock) {
    clk.bind(*clock);
    clock_ = clock;
  }
  sc_clock *clock() const { return clock_.get(); };

  const tlm_utils::simple_target_socket<TlmSink> *mmio_port() const {
    return &mmio_sink_.port;
  };

  // Bind the next downstream cache-maintenance request/response channel.
  void bind_bank(BankCommandIf *bank);

 private:
  void end_of_elaboration() override;
  static constexpr std::array<std::string_view, 3> kOpcodeNames = {
      "NOP",
      "Flush",
      "Invalidate",
  };
  enum class CommandOpcode : uint64_t {
    kNop = 0,
    kFlush = 1,
    kInvalidate = 2,
  };

  struct Csr {
    uint64_t start = 0;
    uint64_t addr = 0;
    uint64_t size = 0;
    uint64_t opcode = 0;
  } csr_;

  static bool IsAllowedCsrAccess(uint64_t addr, unsigned size,
                                 const unsigned char *data);

  void HandleMmioReq();
  void HandleCommands();
  void PrepareBankCommands();
  bool TryDispatchCommands();
  bool TryCollectCommandResponses();

  const bool verbose_;
  const int64_t num_banks_;
  uint8_t *csr_mem_;
  std::shared_ptr<sc_clock> clock_;
  sc_event cmd_start_event_;

  TlmSink mmio_sink_;
  sc_vector<sc_port<BankCommandIf>> banks_;
  std::vector<std::optional<CacheCommand>> bank_commands_;
  std::size_t next_bank_ = 0;
  bool cmd_prepared_ = false;
  bool cmd_dispatched_ = false;
};

CacheController::CacheController(const sc_module_name &name, const Param &param)
    : sc_module(name),
      verbose_(param.verbose),
      num_banks_(param.num_banks),
      csr_mem_(reinterpret_cast<uint8_t *>(&csr_)),
      mmio_sink_("mmio_port"),
      banks_("banks") {
  if (num_banks_ < 0) {
    lv::Fatal("CacheController num_banks must be >= 0, got {}", num_banks_);
  }
  if (num_banks_ > 0) {
    banks_.init(static_cast<std::size_t>(num_banks_));
    bank_commands_.resize(static_cast<std::size_t>(num_banks_));
  }

  SC_METHOD(HandleMmioReq);
  SC_METHOD(HandleCommands);
}

void CacheController::bind_bank(BankCommandIf *bank) {
  if (bank == nullptr) {
    lv::Fatal("CacheController: bank port is null");
  }
  if (next_bank_ >= banks_.size()) {
    lv::Fatal("CacheController: too many bank bindings (num_banks={})",
              num_banks_);
  }
  banks_[next_bank_].bind(*bank);
  ++next_bank_;
}

void CacheController::end_of_elaboration() {
  if (next_bank_ != banks_.size()) {
    lv::Fatal("CacheController: bound {} bank ports, expected {}", next_bank_,
              banks_.size());
  }
}

bool CacheController::IsAllowedCsrAccess(uint64_t addr, unsigned size,
                                         const unsigned char *data) {
  if (data == nullptr) {
    return false;
  }
  if (size != 1 && size != 2 && size != 4 && size != 8) {
    return false;
  }
  if ((addr & 7) != 0) {
    return false;
  }
  switch (addr) {
    case 0x00:
    case 0x08:
    case 0x10:
    case 0x18:
      return true;
    default:
      return false;
  }
}

void CacheController::HandleMmioReq() {
  bool ready = mmio_sink_.req_port->num_available() > 0 &&
               mmio_sink_.resp_port->num_free() > 0;

  if (!ready) {
    next_trigger(mmio_sink_.req_port->data_written_event() |
                 mmio_sink_.resp_port->data_read_event());
    return;
  }

  if (clk->posedge()) {
    auto *trans = mmio_sink_.req_port->read();
    auto addr = trans->get_address();
    auto *data = trans->get_data_ptr();
    auto size = trans->get_data_length();

    if (!IsAllowedCsrAccess(addr, size, data)) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      mmio_sink_.resp_port->write(trans);
    } else {
      if (trans->is_write()) {
        if (addr == 0) {
          if (csr_.start == 0 && data[0] == 1) {
            cmd_start_event_.notify();
            csr_.start = 1;
          }
        } else {
          std::memcpy(&csr_mem_[addr], data, size);
        }
      } else if (trans->is_read()) {
        std::memcpy(data, &csr_mem_[addr], size);
      }

      trans->set_response_status(tlm::TLM_OK_RESPONSE);
      mmio_sink_.resp_port->write(trans);
    }
  }

  next_trigger(clk->posedge_event());
}

void CacheController::PrepareBankCommands() {
  if (csr_.opcode != static_cast<uint64_t>(CommandOpcode::kFlush) &&
      csr_.opcode != static_cast<uint64_t>(CommandOpcode::kInvalidate)) {
    std::fill(bank_commands_.begin(), bank_commands_.end(), std::nullopt);
    return;
  }

  std::fill(bank_commands_.begin(), bank_commands_.end(),
            CacheCommand{csr_.addr, csr_.size, csr_.opcode});
}

bool CacheController::TryDispatchCommands() {
  for (std::size_t bank = 0; bank < bank_commands_.size(); ++bank) {
    if (bank_commands_[bank] && !banks_[bank]->nb_can_put()) {
      return false;
    }
  }

  for (std::size_t bank = 0; bank < bank_commands_.size(); ++bank) {
    if (bank_commands_[bank] && !banks_[bank]->nb_put(*bank_commands_[bank])) {
      lv::Fatal("CacheController bank {} rejected an available command", bank);
    }
  }
  return true;
}

bool CacheController::TryCollectCommandResponses() {
  for (std::size_t bank = 0; bank < bank_commands_.size(); ++bank) {
    if (bank_commands_[bank] && !banks_[bank]->nb_can_get()) {
      return false;
    }
  }

  for (std::size_t bank = 0; bank < bank_commands_.size(); ++bank) {
    if (!bank_commands_[bank]) {
      continue;
    }
    bool response = false;
    if (!banks_[bank]->nb_get(response) || !response) {
      lv::Fatal("CacheController bank {} rejected a maintenance command", bank);
    }
  }
  return true;
}

void CacheController::HandleCommands() {
  if (csr_.start) {
    // Only process the request at posedge of the clock
    if (!clk->posedge()) {
      next_trigger(clk.posedge_event());
      return;
    }

    if (!cmd_prepared_) {
      if (verbose_) {
        if (csr_.opcode > 2) {
          LV_WARNING("Invalid command opcode ({}) on {:#x}:{:#x}", csr_.opcode,
                     csr_.addr, csr_.size);
        } else {
          LV_INFO("{} on {:#x}:{:#x}", kOpcodeNames[csr_.opcode], csr_.addr,
                  csr_.size);
        }
      }

      if (num_banks_ > 0) {
        PrepareBankCommands();
      }
      cmd_prepared_ = true;
    }

    if (num_banks_ > 0 && !cmd_dispatched_) {
      if (!TryDispatchCommands()) {
        next_trigger(clk.posedge_event());
        return;
      }
      cmd_dispatched_ = true;
    }

    if (num_banks_ > 0 && !TryCollectCommandResponses()) {
      next_trigger(clk.posedge_event());
      return;
    }

    csr_.start = 0;
    cmd_prepared_ = false;
    cmd_dispatched_ = false;
  }

  // Wait for another request
  next_trigger(cmd_start_event_);
}

LV_BINDING(ilha, CacheController)
    .constructor(
        [](const char *name, const CacheController::Param &param) {
          return std::make_shared<CacheController>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a cache controller"))
    .property("clock", &CacheController::clock, &CacheController::set_clock,
              lv::doc("SystemC clock"))
    .property("mmio_port", &CacheController::mmio_port,
              lv::doc("MMIO target port"))
    .property("bank", &CacheController::bind_bank,
              lv::doc("Bind the next downstream maintenance port"));

}  // namespace ilha
