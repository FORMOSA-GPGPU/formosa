/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace tlm {

template <unsigned int BusWidth, typename FwIf, typename BwIf>
class tlm_base_initiator_socket_b;
template <unsigned int BusWidth, typename FwIf, typename BwIf, int N,
          sc_core::sc_port_policy Pol>
class tlm_base_initiator_socket;

template <unsigned int BusWidth, typename FwIf, typename BwIf>
class tlm_base_target_socket_b;
template <unsigned int BusWidth, typename FwIf, typename BwIf, int N,
          sc_core::sc_port_policy Pol>
class tlm_base_target_socket;

template <unsigned int BusWidth, typename Types, int N,
          sc_core::sc_port_policy Pol>
class tlm_initiator_socket;
template <unsigned int BusWidth, typename Types, int N,
          sc_core::sc_port_policy Pol>
class tlm_target_socket;

}  // namespace tlm

namespace tlm_utils {

template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
class simple_initiator_socket_b;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_initiator_socket;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_initiator_socket_optional;
template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
class simple_initiator_socket_tagged_b;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_initiator_socket_tagged;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_initiator_socket_tagged_optional;

template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
class simple_target_socket_b;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_target_socket;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_target_socket_optional;
template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
class simple_target_socket_tagged_b;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_target_socket_tagged;
template <typename Module, unsigned int BusWidth, typename Types>
class simple_target_socket_tagged_optional;

template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N, sc_core::sc_port_policy Pol>
class multi_passthrough_initiator_socket;
template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N>
class multi_passthrough_initiator_socket_optional;

template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N, sc_core::sc_port_policy Pol>
class multi_passthrough_target_socket;
template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N>
class multi_passthrough_target_socket_optional;

}  // namespace tlm_utils

namespace lv {

inline std::string lua_socket_type_name() { return "sc.Socket"; }

template <unsigned int BusWidth, typename FwIf, typename BwIf, int N,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<
    tlm::tlm_base_initiator_socket<BusWidth, FwIf, BwIf, N, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <unsigned int BusWidth, typename FwIf, typename BwIf>
struct lua_type_alias<tlm::tlm_base_initiator_socket_b<BusWidth, FwIf, BwIf>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <unsigned int BusWidth, typename FwIf, typename BwIf, int N,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<
    tlm::tlm_base_target_socket<BusWidth, FwIf, BwIf, N, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <unsigned int BusWidth, typename FwIf, typename BwIf>
struct lua_type_alias<tlm::tlm_base_target_socket_b<BusWidth, FwIf, BwIf>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <unsigned int BusWidth, typename Types, int N,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<tlm::tlm_initiator_socket<BusWidth, Types, N, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <unsigned int BusWidth, typename Types, int N,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<tlm::tlm_target_socket<BusWidth, Types, N, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<
    tlm_utils::simple_initiator_socket_b<Module, BusWidth, Types, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_initiator_socket<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_initiator_socket_optional<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<
    tlm_utils::simple_initiator_socket_tagged_b<Module, BusWidth, Types, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_initiator_socket_tagged<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<tlm_utils::simple_initiator_socket_tagged_optional<
    Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<
    tlm_utils::simple_target_socket_b<Module, BusWidth, Types, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_target_socket<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_target_socket_optional<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          sc_core::sc_port_policy Pol>
struct lua_type_alias<
    tlm_utils::simple_target_socket_tagged_b<Module, BusWidth, Types, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_target_socket_tagged<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types>
struct lua_type_alias<
    tlm_utils::simple_target_socket_tagged_optional<Module, BusWidth, Types>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N, sc_core::sc_port_policy Pol>
struct lua_type_alias<tlm_utils::multi_passthrough_initiator_socket<
    Module, BusWidth, Types, N, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N>
struct lua_type_alias<tlm_utils::multi_passthrough_initiator_socket_optional<
    Module, BusWidth, Types, N>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N, sc_core::sc_port_policy Pol>
struct lua_type_alias<tlm_utils::multi_passthrough_target_socket<
    Module, BusWidth, Types, N, Pol>> {
  static std::string name() { return lua_socket_type_name(); }
};

template <typename Module, unsigned int BusWidth, typename Types,
          unsigned int N>
struct lua_type_alias<tlm_utils::multi_passthrough_target_socket_optional<
    Module, BusWidth, Types, N>> {
  static std::string name() { return lua_socket_type_name(); }
};

}  // namespace lv
