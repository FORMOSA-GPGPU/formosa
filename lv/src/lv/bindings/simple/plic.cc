// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief Event-driven PLIC model and its simple.Plic Lua binding.
 */

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace simple {

/**
 * @brief Model level-sensitive sources, PLIC MMIO, and per-context IRQ outputs.
 *
 * Source changes and gateway completion trigger pending and IRQ evaluation in
 * delta cycles. MMIO services at most one request per positive clock edge and
 * sleeps while its FIFOs are not ready. Pending requests and gateway occupancy
 * are tracked separately: claim clears pending, while completion releases the
 * gateway.
 * Priorities and thresholds retain their low three bits. All state starts at
 * zero; there is no runtime reset or edge-triggered source support.
 *
 * @pre Bind the clock and every input/output port before sc_start(). The
 *      platform retains the module and connecting channels for the simulation
 *      lifetime; unused inputs need low channels and outputs need terminals.
 */
class Plic : public sc_module {
 public:
  /** @brief Implemented source and context counts; register strides stay fixed.
   */
  struct Param {
    uint32_t num_sources =
        32;  ///< Sources with IDs 1..num_sources; range 1..1023.
    uint32_t num_contexts = 1;  ///< Context count in 1..15872; IDs start at 0.

    /**
     * @brief Validate counts before allocating per-source and per-context
     * state.
     * @return An unchanged copy of the valid configuration.
     * @throws lv::fatal_error If either count is outside its valid range;
     *         the diagnostic records the field, value, and supported range.
     */
    Param Validate() const {
      if (num_sources == 0 || num_sources > 1023) {
        LV_FATAL("PLIC parameter num_sources={} is outside [1, 1023]",
                 num_sources);
      }
      if (num_contexts == 0 || num_contexts > kMaxContexts) {
        LV_FATAL("PLIC parameter num_contexts={} is outside [1, {}]",
                 num_contexts, kMaxContexts);
      }
      return *this;
    }

    // clang-format off
    LV_SCHEMA(Plic, Param,
              LV_FIELD(num_sources, "Number of level interrupt sources (1..1023)"),
              LV_FIELD(num_contexts, "Number of interrupt contexts (1..15872)"))
    // clang-format on
  };

  /**
   * @brief Construct an initially inactive PLIC with unbound source inputs.
   * @param name SystemC instance name.
   * @param param Number of implemented sources and contexts.
   * @throws lv::fatal_error If either count is outside its valid range.
   */
  Plic(const sc_module_name &name, const Param &param)
      : sc_module(name),
        param_(param.Validate()),
        enable_words_(param_.num_sources / 32 + 1),
        clock_i_("clock"),
        sink_("sink"),
        irq_("irq", param_.num_contexts),
        sources_("sources", param_.num_sources),
        priority_(param_.num_sources + 1, 0),
        pending_(param_.num_sources + 1, false),
        gateway_busy_(param_.num_sources + 1, false),
        context_irq_dirty_(param_.num_contexts, false),
        enables_(param_.num_contexts, std::vector<uint32_t>(enable_words_, 0)),
        threshold_(param_.num_contexts, 0) {
    for (auto &port : irq_) port.initialize(false);

    // Run once after wiring to accept sources that are initially high.
    SC_METHOD(ProcessSourcesAndIrq);
    sources_process_ = sc_get_current_process_handle();
    SC_METHOD(ProcessMmio);
  }

  /**
   * @brief Bind the clock that schedules MMIO processing.
   * @param clock Non-null clock owned by the surrounding platform.
   * @pre Call once during platform wiring, before sc_start().
   * @throws lv::fatal_error If the clock is null or already bound.
   */
  void set_clock(sc_clock *clock) {
    if (clock == nullptr) {
      LV_FATAL("{}: PLIC clock binding is null", name());
    }
    if (clock_ != nullptr) {
      LV_FATAL("{}: PLIC clock is already bound", name());
    }
    clock_ = clock;
    clock_i_.bind(*clock);
  }

  /** @return The bound clock, or nullptr before clock binding. */
  sc_clock *clock() const { return clock_; }

  /**
   * @return MMIO target socket accepting offsets relative to the PLIC base.
   * @note Debug transport returns zero bytes and has no register side effects.
   */
  auto port() { return &sink_.port; }

  /**
   * @return A Lua array of module-owned input ports, indexed by source ID.
   * @pre Bind every port to a platform-owned boolean channel before sc_start().
   * @note Source ID 1 maps to the first port; no source ID 0 port is exposed.
   *       The array and ports do not retain the module or its channels.
   */
  sol::as_table_t<std::vector<sc_in<bool> *>> sources() {
    std::vector<sc_in<bool> *> ports;
    for (auto &port : sources_) ports.push_back(&port);
    return ports;
  }

  /**
   * @return A Lua array of module-owned output ports, indexed by context ID
   * + 1.
   * @pre Bind every output to a platform-owned channel before sc_start().
   * @note Outputs initialize low. A channel is high only when an enabled
   *       pending source's priority strictly exceeds that context's threshold.
   *       The array and ports do not retain the module or its channels.
   */
  sol::as_table_t<std::vector<sc_out<bool> *>> irq() {
    std::vector<sc_out<bool> *> ports;
    for (auto &port : irq_) ports.push_back(&port);
    return ports;
  }

 private:
  // Register strides stay fixed even when fewer sources/contexts are present.
  static constexpr uint64_t kPendingBase = 0x1000;
  static constexpr uint64_t kEnableBase = 0x2000;
  static constexpr uint64_t kContextBase = 0x200000;
  static constexpr uint64_t kBitmapBytes = 0x80;
  static constexpr uint64_t kContextStride = 0x1000;
  static constexpr uint64_t kWindowBytes = 0x4000000;
  static constexpr uint32_t kClaimCompleteOffset = 4;
  static constexpr uint32_t kMaxContexts = 15872;
  static constexpr uint32_t kPriorityMask = 7;

  /**
   * @brief Test the source's bit in a context's enable bitmap.
   * @param context Valid zero-based context ID.
   * @param source Valid source ID.
   * @return Whether this context enables the source.
   */
  bool Enabled(uint32_t context, uint32_t source) const {
    return (enables_[context][source / 32] & (uint32_t{1} << (source % 32))) !=
           0;
  }

  /** @brief Schedule IRQ refresh for a context-local register change. */
  void MarkContextIrqDirty(uint32_t context) {
    context_irq_dirty_[context] = true;
    irq_dirty_ = true;
  }

  /** @brief Refresh all contexts after shared priority or pending changes. */
  void MarkAllIrqsDirty() {
    all_contexts_dirty_ = true;
    irq_dirty_ = true;
  }

  /**
   * @brief Select an enabled pending source without changing its state.
   * @param context Valid zero-based context ID.
   * @return The highest-priority source, breaking ties by lowest ID, or 0 if
   *         no enabled, nonzero-priority source is pending.
   * @note Thresholds affect IRQ notification only and are not consulted here.
   */
  uint32_t BestSource(uint32_t context) const {
    uint32_t best = 0;
    uint32_t best_priority = 0;

    // Strict comparison preserves the lowest ID on ties and excludes priority
    // 0.
    for (uint32_t id = 1; id <= param_.num_sources; ++id) {
      if (pending_[id] && priority_[id] > best_priority &&
          Enabled(context, id)) {
        best = id;
        best_priority = priority_[id];
      }
    }
    return best;
  }

  /**
   * @brief Atomically select a source and clear only its pending bit.
   * @param context Valid zero-based context ID.
   * @return The claimed source ID, or 0 when there is no candidate.
   * @note The gateway stays busy until completion. Context thresholds do not
   *       limit claim, so polling is possible without an IRQ notification.
   */
  uint32_t Claim(uint32_t context) {
    const uint32_t id = BestSource(context);
    if (id != 0) {
      pending_[id] = false;
      MarkAllIrqsDirty();
    }
    return id;
  }

  /**
   * @brief Release a source gateway when the completing context enables it.
   * @param context Valid zero-based context ID.
   * @param id Completion value; zero, unimplemented, or disabled IDs are
   * ignored.
   * @note No claim ownership is tracked. Pending is unchanged, and a held-high
   *       source can request again when the source method handles completion.
   */
  void Complete(uint32_t context, uint32_t id) {
    if (id != 0 && id <= param_.num_sources && Enabled(context, id)) {
      gateway_busy_[id] = false;
      // A held-high source has no new edge to wake its gateway.
      state_changed_.notify(SC_ZERO_TIME);
    }
  }

  /**
   * @brief Build a bitmap mask containing only implemented nonzero source IDs.
   * @param word Zero-based 32-bit bitmap word index.
   * @return Valid source bits; source 0 and unimplemented source bits are zero.
   */
  uint32_t SourceMask(uint32_t word) const {
    const uint32_t first = word * 32;
    uint32_t mask = 0;
    for (uint32_t bit = 0; bit < 32; ++bit) {
      if (first + bit != 0 && first + bit <= param_.num_sources) {
        mask |= uint32_t{1} << bit;
      }
    }
    return mask;
  }

  /**
   * @brief Decode and access one PLIC register, including claim/complete
   * effects.
   * @param address Aligned byte offset relative to the externally mapped base.
   * @param write True for a write; false for a read.
   * @param[in,out] value Input word for a write or output word for a read.
   * @return True for a decoded register; false for a reserved address or an
   *         unimplemented context, leaving PLIC state unchanged on failure.
   * @pre ProcessRequest() has validated the transaction's access format.
   * @note Unimplemented source fields read zero and ignore writes. Pending
   *       writes are ignored; priority and threshold writes retain low 3 bits.
   */
  bool Access(uint64_t address, bool write, uint32_t *value) {
    if (address < kPendingBase) {
      const uint32_t id = address / 4;
      if (id == 0 || id > param_.num_sources) {
        if (!write) *value = 0;
      } else if (write) {
        const uint32_t priority = *value & kPriorityMask;
        if (priority_[id] != priority) {
          priority_[id] = priority;
          MarkAllIrqsDirty();
        }
      } else {
        *value = priority_[id];
      }
      return true;
    }

    if (address < kPendingBase + kBitmapBytes) {
      if (!write) {
        const uint32_t first = (address - kPendingBase) / 4 * 32;
        *value = 0;
        for (uint32_t bit = 0; bit < 32; ++bit) {
          const uint32_t id = first + bit;
          if (id <= param_.num_sources && pending_[id]) {
            *value |= uint32_t{1} << bit;
          }
        }
      }
      return true;  // Pending is read-only; writes are ignored.
    }

    if (address >= kEnableBase &&
        address < kEnableBase + kMaxContexts * kBitmapBytes) {
      const uint32_t context = (address - kEnableBase) / kBitmapBytes;
      const uint32_t word = ((address - kEnableBase) % kBitmapBytes) / 4;
      if (context >= param_.num_contexts) return false;

      if (word >= enable_words_) {
        if (!write) *value = 0;
      } else if (write) {
        const uint32_t enables = *value & SourceMask(word);
        if (enables_[context][word] != enables) {
          enables_[context][word] = enables;
          MarkContextIrqDirty(context);
        }
      } else {
        *value = enables_[context][word];
      }
      return true;
    }

    if (address >= kContextBase && address < kWindowBytes) {
      const uint32_t context = (address - kContextBase) / kContextStride;
      const uint32_t offset = (address - kContextBase) % kContextStride;
      if (context >= param_.num_contexts) return false;

      if (offset == 0) {
        if (write) {
          const uint32_t threshold = *value & kPriorityMask;
          if (threshold_[context] != threshold) {
            threshold_[context] = threshold;
            MarkContextIrqDirty(context);
          }
        } else {
          *value = threshold_[context];
        }
        return true;
      }
      if (offset == kClaimCompleteOffset) {
        if (write) {
          Complete(context, *value);
        } else {
          *value = Claim(context);
        }
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Validate and execute one little-endian 32-bit MMIO transaction.
   * @param[in,out] trans Payload receiving read data and a TLM response status.
   * @pre The caller has reserved response FIFO space.
   * @note Only aligned full-word reads/writes are accepted. Byte enables, when
   *       supplied, must enable all four bytes. Streaming width may be zero
   *       (unspecified) or at least four. Rejected requests have no register
   *       side effects, and successful accesses do not grant DMI.
   */
  void ProcessRequest(tlm::tlm_generic_payload *trans) {
    const uint64_t address = trans->get_address();
    auto *data = trans->get_data_ptr();

    // Validate the entire transaction before a register access can claim or
    // complete a source. A rejected transaction must leave PLIC state intact.
    if (!trans->is_read() && !trans->is_write()) {
      trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
      return;
    }
    if (address % 4 != 0) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return;
    }
    if (trans->get_data_length() != 4 || data == nullptr) {
      trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return;
    }

    // Existing CPU transactions also use zero for an unspecified stream width.
    const auto streaming_width = trans->get_streaming_width();
    if (streaming_width != 0 && streaming_width < 4) {
      trans->set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
      return;
    }
    if (const auto *byte_enables = trans->get_byte_enable_ptr()) {
      const auto byte_enable_length = trans->get_byte_enable_length();
      for (uint32_t i = 0; i < 4; ++i) {
        if (byte_enable_length == 0 ||
            byte_enables[i % byte_enable_length] != TLM_BYTE_ENABLED) {
          trans->set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
          return;
        }
      }
    }

    // Pack MMIO words explicitly so host endianness does not affect the model.
    uint32_t value = 0;
    if (trans->is_write()) {
      for (uint32_t i = 0; i < 4; ++i) value |= uint32_t{data[i]} << (8 * i);
    }
    if (!Access(address, trans->is_write(), &value)) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return;
    }
    if (trans->is_read()) {
      for (uint32_t i = 0; i < 4; ++i) data[i] = value >> (8 * i);
    }

    trans->set_dmi_allowed(false);
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
  }

  void end_of_elaboration() override {
    // Wiring is complete; register every event that can change pending or IRQ.
    sensitive << sources_process_ << state_changed_;
    for (auto &port : sources_) sensitive << port.value_changed_event();
  }

  /**
   * @brief Accept asserted sources and update IRQs after relevant state
   * changes.
   *
   * An idle gateway accepts a high source by setting pending and busy;
   * deassertion cannot retract it. MMIO notifies this method after claim,
   * register changes, and completion. Only this method writes IRQ outputs.
   * Local register changes refresh only their context; shared priority and
   * pending changes refresh all contexts.
   */
  void ProcessSourcesAndIrq() {
    for (uint32_t id = 1; id <= param_.num_sources; ++id) {
      if (!gateway_busy_[id] && !pending_[id] && sources_[id - 1].read()) {
        pending_[id] = true;
        gateway_busy_[id] = true;
        MarkAllIrqsDirty();
      }
    }

    // Completion affects IRQs only when a source is accepted again. Local
    // register changes leave other contexts' arbitration results valid.
    if (irq_dirty_) {
      for (uint32_t context = 0; context < param_.num_contexts; ++context) {
        if (!all_contexts_dirty_ && !context_irq_dirty_[context]) continue;

        irq_[context].write(priority_[BestSource(context)] >
                            threshold_[context]);
        context_irq_dirty_[context] = false;
      }
      all_contexts_dirty_ = false;
      irq_dirty_ = false;
    }
  }

  /**
   * @brief Service one MMIO request on a clock edge when both FIFOs are ready.
   * @note Reserve response space before consuming a request with side effects.
   *       Waiting for response space does not suspend source or IRQ handling.
   */
  void ProcessMmio() {
    if (sink_.req_port->num_available() == 0 ||
        sink_.resp_port->num_free() == 0) {
      next_trigger(sink_.req_port->data_written_event() |
                   sink_.resp_port->data_read_event());
      return;
    }

    if (clock_i_.posedge()) {
      tlm::tlm_generic_payload *trans = nullptr;
      sink_.req_port->nb_read(trans);
      trans->acquire();
      ProcessRequest(trans);
      if (irq_dirty_) state_changed_.notify(SC_ZERO_TIME);
      sink_.resp_port->nb_write(trans);
      trans->release();
    }
    next_trigger(clock_i_.posedge_event());
  }

  const Param param_;
  const uint32_t enable_words_;  ///< Bitmap words covering IDs 0..num_sources.

  sc_clock *clock_ = nullptr;
  sc_in<bool> clock_i_;
  lv::TlmSink sink_;
  /// Module-owned ports; the platform owns and retains connecting channels.
  sc_vector<sc_out<bool>> irq_;
  /// Ports are zero-based; source ID id maps to sources_[id - 1].
  sc_vector<sc_in<bool>> sources_;
  sc_event state_changed_;
  sc_process_handle sources_process_;

  /// Shared priorities indexed by source IDs 1..num_sources.
  /// Slot 0 stays zero as the "no interrupt" arbitration sentinel.
  std::vector<uint32_t> priority_;
  /// Requests waiting for claim, shared by contexts.
  /// Indexed by source IDs 1..num_sources; reserved slot 0 stays false.
  std::vector<bool> pending_;
  /// Accepted requests waiting for completion.
  /// Indexed by source IDs 1..num_sources; reserved slot 0 stays false.
  std::vector<bool> gateway_busy_;
  /// Any outstanding IRQ refresh; also wakes source/IRQ evaluation after MMIO.
  bool irq_dirty_ = false;
  /// Shared state changes invalidate every context's arbitration result.
  bool all_contexts_dirty_ = false;
  /// Context-local register changes waiting for IRQ refresh.
  std::vector<bool> context_irq_dirty_;

  /// Per-context source enable bitmaps.
  std::vector<std::vector<uint32_t>> enables_;
  /// Per-context IRQ notification thresholds.
  std::vector<uint32_t> threshold_;
};

/// @cond LUA_BINDINGS
LV_BINDING(simple, Plic)
    .constructor(
        [](const char *name, const Plic::Param &param) {
          return std::make_shared<Plic>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a level-triggered PLIC"))
    .property("clock", &Plic::clock, &Plic::set_clock, lv::doc("SystemC clock"))
    .property("port", &Plic::port,
              lv::doc("PLIC MMIO target; debug unsupported"))
    .property("sources", &Plic::sources,
              lv::doc("Source input ports; Lua index = source ID; "
                      "bind every port before sc_start"))
    .property("irq", &Plic::irq,
              lv::doc("Context IRQ output ports; Lua index = context ID + 1"));
/// @endcond

}  // namespace simple
