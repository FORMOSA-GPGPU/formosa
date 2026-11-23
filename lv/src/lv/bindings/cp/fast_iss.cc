// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/statistics.h>

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "command_processor.h"

namespace cp {

class FastISS : public sc_module {
 public:
  struct LocalRegion {
    uint64_t addr = 0;
    uint64_t size = 0;
    LV_SCHEMA(FastISS, LocalRegion,
              LV_FIELD(addr, "Private local region start"),
              LV_FIELD(size, "Private local region size in bytes"))
  };
  struct Param {
    sc_time instruction_cost = sc_time(2.5, SC_NS);
    sc_time quantum = sc_time(40, SC_NS);
    std::vector<LocalRegion> local_regions;
    LV_SCHEMA(FastISS, Param,
              LV_FIELD(instruction_cost,
                       "Fixed execution cost, excluding all memory latency"),
              LV_FIELD(quantum,
                       "Local-time threshold for synchronization at "
                       "cost-accounting points"),
              LV_FIELD(local_regions,
                       "Regions eligible for private DMI execution"))
  };

  using Target = TLM_MemIF::Target;
  FastISS(const sc_module_name &name, uint64_t mhart_id, const Param &param);
  ~FastISS() override;
  void set_clk(sc_clock *clock);
  sc_clock *get_clk() const { return clock_; }
  void set_target(Target *target);
  Target *get_target() const { return target_; }
  void set_pc(uint64_t pc) { core_.set_pc(pc); }
  sc_in<bool> *ext_int() { return &ext_interrupt_; }
  sc_in<bool> *sw_int() { return &sw_interrupt_; }
  sc_in<bool> *timer_int() { return &timer_interrupt_; }
  lv::stats::Group stats() const { return stats_; }

 private:
  class Memory;
  const Param param_;
  sc_in<bool> clk_{"clock"};
  sc_in<bool> ext_interrupt_{"ext_interrupt"};
  sc_in<bool> sw_interrupt_{"sw_interrupt"};
  sc_in<bool> timer_interrupt_{"timer_interrupt"};
  sc_clock *clock_ = nullptr;
  Target *target_ = nullptr;
  rv64::Core core_;
  std::unique_ptr<Memory> memory_;
  sc_time local_ = SC_ZERO_TIME;
  struct Stats : lv::stats::Group {
    Metric instructions, dmi_fetches, dmi_reads, dmi_writes;
    Metric at_fetches, at_reads, at_writes, synchronizations;
    explicit Stats(const char *name)
        : Group(name),
          LV_STAT(instructions,
                  "Fetched instructions charged fixed execution cost"),
          LV_STAT(dmi_fetches, "Instruction fetches using a DMI pointer"),
          LV_STAT(dmi_reads, "Data reads using a DMI pointer"),
          LV_STAT(dmi_writes, "Data writes using a DMI pointer"),
          LV_STAT(at_fetches,
                  "Instruction fetches using the AT transaction path"),
          LV_STAT(at_reads, "Data reads using the AT transaction path"),
          LV_STAT(at_writes, "Data writes using the AT transaction path"),
          LV_STAT(synchronizations,
                  "Local time settlements and delta synchronization calls") {}
  } stats_;

  void Run();
  void Advance(sc_time cost);
  void Synchronize();
  void ExternalInterrupt();
  void SoftwareInterrupt();
  void TimerInterrupt();
};

// Only this adapter knows the local-time policy. The base owns the existing
// payload pool and AT handshake; fallback calls its qualified scalar methods.
class FastISS::Memory : public TLM_MemIF {
 public:
  Memory(sc_module_name name, FastISS &owner)
      : TLM_MemIF(name), owner_(owner) {}

  uint32_t load_instr(uint64_t pc) override {
    auto bits = Load<uint32_t>(
        pc,
        [&] {
          return TLM_MemIF::load_instr(pc);
        },
        true);
    // Charge before executing: run_step may issue a shared/MMIO side effect.
    // This is a fixed execution/issue cost, never an inclusive average CPI.
    owner_.stats_.instructions++;
    owner_.Advance(owner_.param_.instruction_cost);
    rv64::Instruction instruction(bits);
    const auto op = instruction.decode();
    if (op == rv64::Opcode::FENCE || op == rv64::Opcode::FENCE_I ||
        op == rv64::Opcode::SFENCE_VMA)
      owner_.Synchronize();
    return bits;
  }

  int64_t load_double(uint64_t a) override {
    return Load<int64_t>(a, [&] {
      return TLM_MemIF::load_double(a);
    });
  }
  int64_t load_word(uint64_t a) override {
    return Load<int32_t>(a, [&] {
      return TLM_MemIF::load_word(a);
    });
  }
  int64_t load_half(uint64_t a) override {
    return Load<int16_t>(a, [&] {
      return TLM_MemIF::load_half(a);
    });
  }
  int64_t load_byte(uint64_t a) override {
    return Load<int8_t>(a, [&] {
      return TLM_MemIF::load_byte(a);
    });
  }
  uint64_t load_uword(uint64_t a) override {
    return Load<uint32_t>(a, [&] {
      return TLM_MemIF::load_uword(a);
    });
  }
  uint64_t load_uhalf(uint64_t a) override {
    return Load<uint16_t>(a, [&] {
      return TLM_MemIF::load_uhalf(a);
    });
  }
  uint64_t load_ubyte(uint64_t a) override {
    return Load<uint8_t>(a, [&] {
      return TLM_MemIF::load_ubyte(a);
    });
  }
  void store_double(uint64_t a, uint64_t v) override {
    Store(a, v, [&] {
      TLM_MemIF::store_double(a, v);
    });
  }
  void store_word(uint64_t a, uint32_t v) override {
    Store(a, v, [&] {
      TLM_MemIF::store_word(a, v);
    });
  }
  void store_half(uint64_t a, uint16_t v) override {
    Store(a, v, [&] {
      TLM_MemIF::store_half(a, v);
    });
  }
  void store_byte(uint64_t a, uint8_t v) override {
    Store(a, v, [&] {
      TLM_MemIF::store_byte(a, v);
    });
  }

 private:
  FastISS &owner_;
  // A bounded cache is sufficient for reuse, not for correctness: evicted
  // grants are queried again. Never allocate on instruction or memory paths.
  std::array<std::optional<tlm::tlm_dmi>, 4> grants_;
  size_t next_grant_ = 0;
  // The pinned TLM payload constructor allocates its extension vector. Build
  // this query once, not on a recurring DMI miss or invalidation path.
  tlm::tlm_generic_payload query_;

  bool IsLocal(uint64_t address, size_t size) const {
    for (const auto &region : owner_.param_.local_regions) {
      if (address >= region.addr && address - region.addr < region.size &&
          size <= region.size - (address - region.addr))
        return true;
    }
    return false;
  }
  static bool Covers(const tlm::tlm_dmi &grant, uint64_t address, size_t size,
                     bool write) {
    return grant.get_dmi_ptr() && address >= grant.get_start_address() &&
           address <= grant.get_end_address() &&
           size - 1 <= grant.get_end_address() - address &&
           (write ? grant.is_write_allowed() : grant.is_read_allowed());
  }
  const tlm::tlm_dmi *Grant(uint64_t address, size_t size, bool write) {
    // Availability of a pointer does not authorize run-ahead into shared state.
    if (!IsLocal(address, size)) return nullptr;
    for (const auto &grant : grants_) {
      if (grant && Covers(*grant, address, size, write)) return &*grant;
    }
    query_.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
    query_.set_address(address);
    query_.set_dmi_allowed(false);
    tlm::tlm_dmi grant;
    const bool granted = mem_port_->get_direct_mem_ptr(query_, grant);
    if (granted && Covers(grant, address, size, write)) {
      auto &slot = grants_[next_grant_];
      slot = grant;
      next_grant_ = (next_grant_ + 1) % grants_.size();
      return &*slot;
    }
    return nullptr;
  }
  template <class T, class F>
  T Load(uint64_t address, F fallback, bool fetch = false) {
    const auto *grant = Grant(address, sizeof(T), false);
    if (grant) {
      if (fetch)
        owner_.stats_.dmi_fetches++;
      else
        owner_.stats_.dmi_reads++;
      T value;
      std::memcpy(&value,
                  grant->get_dmi_ptr() + address - grant->get_start_address(),
                  sizeof(T));
      owner_.Advance(grant->get_read_latency());
      return value;
    }
    owner_.Synchronize();
    // The AT path advances global time itself. Do not add that elapsed time to
    // local_ a second time.
    if (fetch)
      owner_.stats_.at_fetches++;
    else
      owner_.stats_.at_reads++;
    return static_cast<T>(fallback());
  }
  template <class T, class F>
  void Store(uint64_t address, T value, F fallback) {
    const auto *grant = Grant(address, sizeof(T), true);
    if (grant) {
      owner_.stats_.dmi_writes++;
      std::memcpy(grant->get_dmi_ptr() + address - grant->get_start_address(),
                  &value, sizeof(T));
      owner_.Advance(grant->get_write_latency());
      return;
    }
    owner_.Synchronize();
    owner_.stats_.at_writes++;
    fallback();
  }
  void InvalidateDirectMemPtr(sc_dt::uint64 start, sc_dt::uint64 end) override {
    for (auto &grant : grants_) {
      if (grant && start <= grant->get_end_address() &&
          end >= grant->get_start_address())
        grant.reset();
    }
  }
};

FastISS::FastISS(const sc_module_name &name, uint64_t mhart_id,
                 const Param &param)
    : sc_module(name),
      param_(param),
      core_(mhart_id, false),
      memory_(std::make_unique<Memory>("memory", *this)),
      stats_(name) {
  if (param.quantum <= SC_ZERO_TIME || param.instruction_cost <= SC_ZERO_TIME) {
    throw std::invalid_argument(
        "FastISS quantum and instruction_cost must be positive");
  }
  for (const auto &region : param.local_regions) {
    if (!region.size ||
        region.size - 1 > std::numeric_limits<uint64_t>::max() - region.addr) {
      throw std::invalid_argument("FastISS local region is empty or overflows");
    }
  }
  core_.set_instr_mem(memory_.get());
  core_.set_data_mem(memory_.get());
  SC_THREAD(Run);
  SC_METHOD(ExternalInterrupt);
  sensitive << ext_interrupt_;
  SC_METHOD(SoftwareInterrupt);
  sensitive << sw_interrupt_;
  SC_METHOD(TimerInterrupt);
  sensitive << timer_interrupt_;
}

FastISS::~FastISS() = default;

void FastISS::Advance(sc_time cost) {
  local_ += cost;
  // Yield inside the current step: Core samples pending interrupts at its end.
  // Waiting only after run_step would defer sampling by another instruction.
  if (local_ >= param_.quantum) Synchronize();
}

void FastISS::Synchronize() {
  stats_.synchronizations++;
  const auto delay = std::exchange(local_, SC_ZERO_TIME);
  if (delay != SC_ZERO_TIME) wait(delay);
  // Let signal updates and same-timestamp interrupt methods become visible.
  wait(SC_ZERO_TIME);
}

void FastISS::Run() {
  wait(clk_.posedge_event());
  while (true) {
    // Firmware uses ebreak outside a debugger: keep the legacy run_step loop.
    core_.run_step();
  }
}
void FastISS::ExternalInterrupt() {
  core_.ext_interrupt(ext_interrupt_.read());
}
void FastISS::SoftwareInterrupt() { core_.sw_interrupt(sw_interrupt_.read()); }
void FastISS::TimerInterrupt() {
  core_.timer_interrupt(timer_interrupt_.read());
}
void FastISS::set_clk(sc_clock *clock) {
  clock_ = clock;
  clk_.bind(*clock);
}
void FastISS::set_target(Target *target) {
  target_ = target;
  memory_->bind_mem_port(target);
}

LV_BINDING(cp, FastISS)
    .constructor(
        [](const char *name, uint64_t mhart_id, const FastISS::Param &param) {
          return std::make_shared<FastISS>(name, mhart_id, param);
        },
        lv::params("name", "mhart_id", "param"),
        lv::doc("Create a locally timed RISC-V command processor"))
    .property("clock", &FastISS::get_clk, &FastISS::set_clk,
              lv::doc("SystemC clock"))
    .property("target", &FastISS::get_target, &FastISS::set_target,
              lv::doc("Memory target"))
    .method("set_pc", &FastISS::set_pc, lv::params("pc"),
            lv::doc("Set initial program counter"))
    .property("ext_int", &FastISS::ext_int, lv::doc("External interrupt input"))
    .property("sw_int", &FastISS::sw_int, lv::doc("Software interrupt input"))
    .property("timer_int", &FastISS::timer_int,
              lv::doc("Timer interrupt input"))
    .property("stats", &FastISS::stats,
              lv::doc("Initiator path and synchronization statistics"));

}  // namespace cp
