/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <sol/sol.hpp>

// Lua exposes these sockets as sc.Socket. Push the common TLM interface used
// by connector setters, rather than unregistered concrete socket userdata.
// sol2's ADL extension also avoids treating SystemC ports as Lua containers.
namespace tlm_utils {

template <typename Module, unsigned int Width, typename Types>
int sol_lua_push(lua_State *state,
                 const simple_target_socket<Module, Width, Types> *socket) {
  using Base =
      tlm::tlm_base_target_socket_b<Width, tlm::tlm_fw_transport_if<Types>,
                                    tlm::tlm_bw_transport_if<Types>>;
  return sol::stack::push(state, static_cast<const Base *>(socket));
}

template <typename Module, unsigned int Width, typename Types>
int sol_lua_push(lua_State *state,
                 const simple_target_socket<Module, Width, Types> &socket) {
  return sol_lua_push(state, &socket);
}

template <typename Module, unsigned int Width, typename Types, unsigned int N,
          sc_core::sc_port_policy Policy>
int sol_lua_push(lua_State *state,
                 const multi_passthrough_target_socket<Module, Width, Types, N,
                                                       Policy> *socket) {
  using Base =
      tlm::tlm_base_target_socket_b<Width, tlm::tlm_fw_transport_if<Types>,
                                    tlm::tlm_bw_transport_if<Types>>;
  return sol::stack::push(state, static_cast<const Base *>(socket));
}

template <typename Module, unsigned int Width, typename Types, unsigned int N,
          sc_core::sc_port_policy Policy>
int sol_lua_push(lua_State *state,
                 const multi_passthrough_target_socket<Module, Width, Types, N,
                                                       Policy> &socket) {
  return sol_lua_push(state, &socket);
}

}  // namespace tlm_utils
