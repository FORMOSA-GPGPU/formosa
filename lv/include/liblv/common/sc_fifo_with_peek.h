/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>

#include <string>
#include <typeinfo>

namespace lv {

template <typename T>
class sc_fifo_with_peek : public sc_core::sc_fifo<T>,
                          public virtual tlm::tlm_peek_if<T> {
 public:
  explicit sc_fifo_with_peek(int size = 16) : sc_core::sc_fifo<T>(size) {}

  explicit sc_fifo_with_peek(const char *name, int size = 16)
      : sc_core::sc_fifo<T>(name, size) {}

  void register_port(sc_core::sc_port_base &port,
                     const char *if_typename) override {
    const std::string interface_name(if_typename);
    if (interface_name == typeid(tlm::tlm_blocking_peek_if<T>).name() ||
        interface_name == typeid(tlm::tlm_nonblocking_peek_if<T>).name() ||
        interface_name == typeid(tlm::tlm_peek_if<T>).name()) {
      return;
    }

    sc_core::sc_fifo<T>::register_port(port, if_typename);
  }

  T peek(tlm::tlm_tag<T> * = nullptr) const override {
    while (this->num_available() == 0) {
      sc_core::wait(this->data_written_event());
    }

    return this->m_buf[this->m_ri];
  }

  void peek(T &value) const override {
    value = peek(static_cast<tlm::tlm_tag<T> *>(nullptr));
  }

  bool nb_peek(T &value) const override {
    if (this->num_available() == 0) {
      return false;
    }

    value = this->m_buf[this->m_ri];
    return true;
  }

  bool nb_can_peek(tlm::tlm_tag<T> * = nullptr) const override {
    return this->num_available() > 0;
  }

  const sc_core::sc_event &ok_to_peek(
      tlm::tlm_tag<T> * = nullptr) const override {
    return this->data_written_event();
  }
};

}  // namespace lv
