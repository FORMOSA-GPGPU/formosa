// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_tag.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>

#include "utils/delay_queue.h"

namespace simtix {

struct DelayQueueTestingItem {
  uint64_t id = 0;
  // SystemC timestamp (in cycles) recorded when the item is pulled into the
  // DUT.
  uint64_t accept_cycle = std::numeric_limits<uint64_t>::max();
};

// Producer side of the testbench. The DUT pulls items from here, so this
// implements tlm_get_if. The moment an item is handed to the DUT (get), its id
// and the current SystemC timestamp are stamped onto the item itself and the
// input throughput is verified.
class DelayQueueProducer : public sc_module,
                           public tlm::tlm_get_if<DelayQueueTestingItem> {
 public:
  DelayQueueProducer(const sc_module_name name, uint64_t ticks_per_output)
      : sc_module(name),
        ticks_per_output_(ticks_per_output),
        req_fifo_("req_fifo", 2),
        last_accept_cycle_(0) {
    sc_spawn(sc_bind(&DelayQueueProducer::Produce, this));
  }

  void set_clock(std::shared_ptr<sc_clock> clock) { clock_ = std::move(clock); }
  void set_target_count(uint64_t count) { target_count_ = count; }
  uint64_t minimum_accept_gap() const { return minimum_accept_gap_; }

  DelayQueueTestingItem get(
      tlm::tlm_tag<DelayQueueTestingItem> *t = 0) override {
    DelayQueueTestingItem item = req_fifo_.get(t);
    Stamp(item);
    return item;
  }

  bool nb_get(DelayQueueTestingItem &t) override {
    if (req_fifo_.nb_get(t)) {
      Stamp(t);
      return true;
    }
    return false;
  }

  bool nb_can_get(tlm::tlm_tag<DelayQueueTestingItem> *t = 0) const override {
    return req_fifo_.nb_can_get(t);
  }

  const sc_event &ok_to_get(
      tlm::tlm_tag<DelayQueueTestingItem> *t = 0) const override {
    return req_fifo_.ok_to_get(t);
  }

 private:
  uint64_t CurrentCycle() const {
    if (!clock_) {
      LV_FATAL("clock is not set.");
    }
    return static_cast<uint64_t>(sc_time_stamp().value() /
                                 clock_->period().value());
  }

  // Record id + SystemC timestamp onto the item and verify input throughput.
  void Stamp(DelayQueueTestingItem &item) {
    const uint64_t accept_cycle = CurrentCycle();
    if (has_last_accept_) {
      const uint64_t gap = accept_cycle - last_accept_cycle_;
      if (gap < ticks_per_output_) {
        LV_FATAL(
            "Input throughput violation for id={}: last_accept={}, accept={}, "
            "required gap>={}",
            item.id, last_accept_cycle_, accept_cycle, ticks_per_output_);
      }
      minimum_accept_gap_ = std::min(minimum_accept_gap_, gap);
    }
    has_last_accept_ = true;
    last_accept_cycle_ = accept_cycle;
    item.accept_cycle = accept_cycle;
  }

  uint64_t PhaseTarget(bool stall) const {
    return stall ? static_cast<uint64_t>((rand() % 10) + 5)
                 : static_cast<uint64_t>(ticks_per_output_ + 1 + (rand() % 5));
  }

  void Produce() {
    uint64_t next_id = 0;
    bool stall = false;
    uint64_t sequence_num = 0;
    uint64_t phase_target = PhaseTarget(stall);

    while (next_id < target_count_) {
      wait(clock_->posedge_event());

      if (!stall && next_id < target_count_) {
        DelayQueueTestingItem item{.id = next_id};
        if (req_fifo_.nb_put(item)) {
          next_id++;
        }
      }

      sequence_num++;
      if (sequence_num >= phase_target) {
        sequence_num = 0;
        stall = !stall;
        phase_target = PhaseTarget(stall);
      }
    }
  }

  const uint64_t ticks_per_output_;
  tlm::tlm_fifo<DelayQueueTestingItem> req_fifo_;
  std::shared_ptr<sc_clock> clock_;
  uint64_t target_count_ = 0;
  uint64_t last_accept_cycle_ = 0;
  uint64_t minimum_accept_gap_ = std::numeric_limits<uint64_t>::max();
  bool has_last_accept_ = false;
};

// Consumer side of the testbench. The DUT pushes items here, so this implements
// tlm_put_if. On put it records the current SystemC timestamp and forwards the
// item (with its accept timestamp) to the tester for latency/order/throughput
// judgment.
class DelayQueueConsumer : public sc_module,
                           public tlm::tlm_put_if<DelayQueueTestingItem> {
 public:
  using CheckFn =
      std::function<void(const DelayQueueTestingItem &, uint64_t output_cycle)>;

  DelayQueueConsumer(const sc_module_name name)
      : sc_module(name), resp_fifo_("resp_fifo", 2) {
    sc_spawn(sc_bind(&DelayQueueConsumer::Consume, this));
  }

  void set_clock(std::shared_ptr<sc_clock> clock) { clock_ = std::move(clock); }
  void set_check(CheckFn check) { check_ = std::move(check); }

  void put(const DelayQueueTestingItem &t) override {
    resp_fifo_.put(t);
    check_(t, CurrentCycle());
  }

  bool nb_put(const DelayQueueTestingItem &t) override {
    if (resp_fifo_.nb_put(t)) {
      check_(t, CurrentCycle());
      return true;
    }
    return false;
  }

  bool nb_can_put(tlm::tlm_tag<DelayQueueTestingItem> *t = 0) const override {
    return resp_fifo_.nb_can_put(t);
  }

  const sc_event &ok_to_put(
      tlm::tlm_tag<DelayQueueTestingItem> *t = 0) const override {
    return resp_fifo_.ok_to_put(t);
  }

 private:
  uint64_t CurrentCycle() const {
    if (!clock_) {
      LV_FATAL("clock is not set.");
    }
    return static_cast<uint64_t>(sc_time_stamp().value() /
                                 clock_->period().value());
  }

  uint64_t PhaseTarget(bool stall) const {
    return stall ? static_cast<uint64_t>((rand() % 10) + 5)
                 : static_cast<uint64_t>((rand() % 5) + 1);
  }

  void Consume() {
    bool stall = false;
    uint64_t sequence_num = 0;
    uint64_t phase_target = PhaseTarget(stall);

    while (true) {
      wait(clock_->posedge_event());

      if (!stall && resp_fifo_.nb_can_get()) {
        DelayQueueTestingItem item;
        resp_fifo_.nb_get(item);
      }

      sequence_num++;
      if (sequence_num >= phase_target) {
        sequence_num = 0;
        stall = !stall;
        phase_target = PhaseTarget(stall);
      }
    }
  }

  tlm::tlm_fifo<DelayQueueTestingItem> resp_fifo_;
  std::shared_ptr<sc_clock> clock_;
  CheckFn check_;
};

// DelayQueue pulls from the producer (req side) and pushes to the consumer
// (resp side). The producer stamps accept cycles; the tester verifies latency
// plus input/output ordering and throughput at the output boundary.
class DelayQueueTester : public sc_module {
 public:
  sc_in<bool> clk;

  DelayQueueTester(const sc_module_name name, uint64_t latency,
                   uint64_t ticks_per_output)
      : sc_module(name),
        latency_(latency),
        ticks_per_output_(ticks_per_output),
        producer_("producer", ticks_per_output),
        consumer_("consumer"),
        delay_queue_("delay_queue", latency, ticks_per_output),
        minimum_delay_(std::numeric_limits<uint64_t>::max()),
        minimum_output_gap_(std::numeric_limits<uint64_t>::max()) {
    consumer_.set_check(
        [this](const DelayQueueTestingItem &item, uint64_t output_cycle) {
          CheckOutput(item, output_cycle);
        });
  }

  void test(uint64_t seed, uint64_t count) {
    if (count == 0) {
      LV_FATAL("count must be positive.");
    }
    if (!clock_) {
      LV_FATAL("clock must be set before running test.");
    }
    srand(seed);
    target_count_ = count;
    producer_.set_target_count(count);

    // Allow generous headroom for stall phases in the producer/consumer.
    const uint64_t max_cycles =
        count * (latency_ + ticks_per_output_) * 100 + latency_ * 100;
    sc_start(clock_->period() * static_cast<double>(max_cycles));

    if (output_count_ < target_count_) {
      LV_FATAL("Simulation timeout: output_count={}, target_count={}",
               output_count_, target_count_);
    }
  }

  void set_clock(std::shared_ptr<sc_clock> clock) {
    clk.bind(*clock);
    clock_ = clock;
    producer_.set_clock(clock);
    consumer_.set_clock(clock);
  }

  void before_end_of_elaboration() override {
    delay_queue_.req_q_.bind(producer_);
    delay_queue_.resp_q_.bind(consumer_);
    delay_queue_.clock.bind(clk);
  }

 private:
  // Judge the item once the DUT has output it: compare the current timestamp
  // against the accept timestamp recorded by the producer.
  void CheckOutput(const DelayQueueTestingItem &item, uint64_t output_cycle) {
    if (item.accept_cycle == std::numeric_limits<uint64_t>::max()) {
      LV_FATAL("Missing accept cycle for id={}", item.id);
    }
    if (output_cycle < item.accept_cycle + latency_) {
      LV_FATAL(
          "Latency violation for id={}: accept={}, output={}, required>={}",
          item.id, item.accept_cycle, output_cycle,
          item.accept_cycle + latency_);
    }
    if (item.id != next_output_id_) {
      LV_FATAL("Queue order violation: expected id={}, got id={}",
               next_output_id_, item.id);
    }
    next_output_id_++;

    if (has_last_output_) {
      const uint64_t gap = output_cycle - last_output_cycle_;
      if (gap < ticks_per_output_) {
        LV_FATAL(
            "Output throughput violation for id={}: last_output={}, output={}, "
            "required gap>={}",
            item.id, last_output_cycle_, output_cycle, ticks_per_output_);
      }
      minimum_output_gap_ = std::min(minimum_output_gap_, gap);
    }
    has_last_output_ = true;
    last_output_cycle_ = output_cycle;

    if (output_cycle - item.accept_cycle < minimum_delay_) {
      minimum_delay_ = output_cycle - item.accept_cycle;
    }

    output_count_++;
    if (output_count_ >= target_count_) {
      if (minimum_delay_ != latency_) {
        LV_FATAL("Minimum observed latency violation: minimum={}, expected={}",
                 minimum_delay_, latency_);
      }
      if (target_count_ > 1 &&
          producer_.minimum_accept_gap() != ticks_per_output_) {
        LV_FATAL(
            "Input throughput was not reached: minimum gap={}, expected={}",
            producer_.minimum_accept_gap(), ticks_per_output_);
      }
      if (target_count_ > 1 && minimum_output_gap_ != ticks_per_output_) {
        LV_FATAL(
            "Output throughput was not reached: minimum gap={}, expected={}",
            minimum_output_gap_, ticks_per_output_);
      }
      sc_pause();
    }
  }

  const uint64_t latency_;
  const uint64_t ticks_per_output_;

  DelayQueueProducer producer_;
  DelayQueueConsumer consumer_;
  DelayQueue<DelayQueueTestingItem> delay_queue_;

  std::shared_ptr<sc_clock> clock_;
  uint64_t target_count_ = 0;
  uint64_t next_output_id_ = 0;
  uint64_t last_output_cycle_ = 0;
  uint64_t output_count_ = 0;
  uint64_t minimum_delay_;
  uint64_t minimum_output_gap_;
  bool has_last_output_ = false;
};

LV_BINDING(simtix, DelayQueueTester)
    .constructor(
        [](const char *name, uint64_t latency, uint64_t ticks_per_output) {
          return std::make_shared<DelayQueueTester>(name, latency,
                                                    ticks_per_output);
        },
        lv::params("name", "latency", "ticks_per_output"),
        lv::doc("Create a DelayQueue tester"))
    .method("test", &DelayQueueTester::test, lv::params("seed", "count"),
            lv::doc("Run the delay queue test"))
    .property("clock", &DelayQueueTester::set_clock, lv::doc("SystemC clock"));

}  // namespace simtix
