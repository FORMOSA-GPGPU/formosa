/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>
#include <liblv/log.h>
#include <systemc.h>

#include <string>

namespace simtix::konata {

template <class T>
uint64_t GetUniqueId(const T *);

template <class T>
uint32_t GetThreadId(const T *);

template <class T>
class KonataTracer : public sc_module {
 public:
  sc_in<bool> SC_NAMED(clock);

  KonataTracer(const sc_module_name &name, std::string path)
      : sc_module(name), tracer_(std::move(path)), update_proxy_(this) {
    LV_FTRACER_PRINTLN(tracer_, "Kanata\t004");
    LV_FTRACER_PRINTLN(tracer_, "C=\t-1");
    SC_METHOD(Tick);
    sensitive << clock.pos();
  }

  void Declare(const T *instr) {
    LV_FTRACER_PRINTLN(tracer_, "I\t{}\t0\t{}", GetUniqueId(instr),
                       GetThreadId(instr));
  }

  void StartStage(const T *instr, uint32_t lane_id,
                  std::string_view stage_name) {
    LV_FTRACER_PRINTLN(tracer_, "S\t{}\t{}\t{}", GetUniqueId(instr), lane_id,
                       stage_name);
  }

  void AddComment(const T *instr, std::string_view comment) {
    LV_FTRACER_PRINTLN(tracer_, "L\t{}\t1\t{}", GetUniqueId(instr), comment);
  }

  void AddMnemonic(const T *instr, std::string_view mnemonic) {
    LV_FTRACER_PRINTLN(tracer_, "L\t{}\t0\t{}", GetUniqueId(instr), mnemonic);
  }

  void Retire(const T *instr) {
    LV_FTRACER_PRINTLN(tracer_, "R\t{}\t{}\t0", GetUniqueId(instr),
                       GetUniqueId(instr));
  }

  void ArchitecturalFlush(const T *instr, std::string_view label) {
    AddComment(instr, fmt::format("flush: {}\\n", label));
    LV_FTRACER_PRINTLN(tracer_, "R\t{}\t{}\t1", GetUniqueId(instr),
                       GetUniqueId(instr));
  }

  void SpeculativeDiscard(const T *instr, std::string_view cause) {
    AddComment(instr, fmt::format("{}\\n", cause));
    LV_FTRACER_PRINTLN(tracer_, "R\t{}\t{}\t1", GetUniqueId(instr),
                       GetUniqueId(instr));
  }

  void Flush(const T *instr, std::string_view reason = {}) {
    ArchitecturalFlush(instr, reason);
  }

  void Wake(const T *consumer, uint64_t producer_id) {
    LV_FTRACER_PRINTLN(tracer_, "W\t{}\t{}\t0", GetUniqueId(consumer),
                       producer_id);
  }

  void Wake(const T *consumer, const T *producer) {
    Wake(consumer, GetUniqueId(producer));
  }

 private:
  class UpdateProxy : public sc_prim_channel {
   public:
    UpdateProxy(KonataTracer *parent) : parent_(parent) {}
    void TriggerUpdate() { request_update(); }

   protected:
    void update() override { LV_FTRACER_PRINTLN(parent_->tracer_, "C\t1"); }

   private:
    KonataTracer *parent_;
  };

  void Tick() { update_proxy_.TriggerUpdate(); }

  lv::log::FileTracer tracer_;
  UpdateProxy update_proxy_;

  friend class UpdateProxy;
};

}  // namespace simtix::konata
