// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <libcomm/libcomm.h>
#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/mm/thread_safe_pool.h>
#include <liblv/schema.h>
#include <systemc.h>

#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ipc {

struct Agent;

namespace {

struct Param {
  std::string socket_path = "/tmp/ipc.socket";
  uint32_t timeout_ms = 100;
  bool debug = false;
  bool ignore_terminate = false;
  std::optional<sol::function> probe_hook;

  // clang-format off
  LV_SCHEMA(Agent, Param,
            LV_FIELD(socket_path, "IPC socket path"),
            LV_FIELD(timeout_ms, "Socket timeout in milliseconds"),
            LV_FIELD(debug, "Enable debug-transport mode"),
            LV_FIELD(ignore_terminate, "Ack Terminate without pausing the simulation"),
            LV_FIELD(probe_hook, "Callback invoked on probe requests"))
  // clang-format on
};

}  // namespace

SC_MODULE(Agent) {
 public:
  SC_CTOR(Agent, std::string_view socket_path, uint32_t timeout_ms, bool debug,
          std::function<void()> probe_hook, bool ignore_terminate)
      : timeout_({.tv_sec = timeout_ms / 1000,
                  .tv_usec = (timeout_ms % 1000) * 1000}),
        accept_thread_(libcomm::Serve(
            socket_path.data(), &timeout_,
            [this](std::unique_ptr<libcomm::Transceiver> transceiver) {
              // On acception, replace the current transceiver directly.
              set_transceiver(std::move(transceiver));
            })),
        transceiver_(nullptr),
        sink_("sink",
              [this](auto &payload) -> uint32_t {
                // Only handle debug transport when in debug mode.
                if (!debug_)
                  return 0;
                else
                  return HandleDbgTransport(payload);
              }),
        source_("source"),
        probe_hook_(probe_hook),
        debug_(debug),
        ignore_terminate_(ignore_terminate) {
    SC_THREAD(HandleReqFw);
    SC_THREAD(HandleRespFw);
    SC_THREAD(HandleRespBw);
    SC_THREAD(HandleReqBw);
    SC_THREAD(HandleProbe);
    SC_THREAD(HandleTerminate);
  }

  ~Agent() { accept_thread_->detach(); }

  void HandleReqFw() {
    for (;;) {
      while (req_q_fw_.size() == 0) {
        wait(req_q_fw_.push_event());
      }

      tlm::tlm_generic_payload *payload = req_q_fw_.pop();
      if (debug_) {
        source_.PutRequestDbg(*payload);
        SendResponse(payload);
      } else {
        source_.req_port->write(payload);
      }
    }
  }

  void HandleRespBw() {
    for (;;) {
      auto *payload = source_.resp_port->read();
      SendResponse(payload);
    }
  }

  void HandleReqBw() {
    for (;;) {
      auto *payload = sink_.req_port->read();
      payload->acquire();
      SendRequest(payload);
    }
  }

  void HandleRespFw() {
    for (;;) {
      while (resp_q_fw_.size() == 0) {
        wait(resp_q_fw_.push_event());
      }

      tlm::tlm_generic_payload *payload = resp_q_fw_.pop();
      sink_.resp_port->write(payload);
      payload->release();
    }
  }

  void HandleProbe() {
    for (;;) {
      wait(probe_event_);
      probe_hook_();
      transceiver_->Send(libcomm::Msg::Respond(probe_msg_));
    }
  }

  void HandleTerminate() {
    for (;;) {
      wait(terminate_event_);
      transceiver_->Send(libcomm::Msg::Respond(terminate_msg_));
      if (!ignore_terminate_) sc_pause();
    }
  }

  // Lua bindings
  using Target =
      tlm_utils::simple_initiator_socket<Agent>::base_target_socket_type;
  void set_target(Target * t) { source_.set_target(t); }

  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;
  Source *port() const { return &sink_.port; }

 private:
  uint32_t HandleDbgTransport(tlm::tlm_generic_payload & payload) {
    SendRequest(&payload);

    // Block until the request is processed through libcomm.
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock);
    return payload.get_data_length();
  }

  void SendRequest(tlm::tlm_generic_payload * payload) {
    libcomm::Cmd cmd = libcomm::Cmd::Get;

    switch (payload->get_command()) {
      case tlm::TLM_READ_COMMAND:
        if (payload->get_address() == 0 && payload->get_data_length() == 0) {
          cmd = libcomm::Cmd::Probe;
        } else {
          cmd = libcomm::Cmd::Get;
        }
        break;
      case tlm::TLM_WRITE_COMMAND:
        if (payload->get_address() == 0 && payload->get_data_length() == 0) {
          cmd = libcomm::Cmd::Terminate;
        } else {
          cmd = libcomm::Cmd::Put;
        }
        break;
      default:
        // Unknown command, do nothing and release the payload.
        payload->release();
        return;
    }

    std::lock_guard<std::mutex> lock(id_payload_map_mutex_);
    transceiver_->Send(libcomm::Msg::Build(cmd)
                           .id(++id_)
                           .addr(payload->get_address())
                           .size(payload->get_data_length())
                           .data(payload->get_data_ptr()));

    auto [it, ok] = id_payload_map_.insert({id_, payload});
    if (!ok) {
      // internal error
      assert(0);
    }
  }

  void SendResponse(tlm::tlm_generic_payload * payload) {
    std::lock_guard<std::mutex> lock(payload_meta_map_mutex_);
    auto it = payload_meta_map_.find(payload);
    if (it == payload_meta_map_.end()) {
      // internal error
      assert(0);
    }
    auto &[key, value] = *it;
    auto &[msg, data] = value;

    transceiver_->Send(
        libcomm::Msg::Respond(msg)
            .data(data.data())
            .status(FromTlmStatus(payload->get_response_status())));

    payload_meta_map_.erase(payload);
    payload->release();
  }

  void set_transceiver(std::unique_ptr<libcomm::Transceiver> transceiver) {
    transceiver_ = std::move(transceiver);
    transceiver_->RegisterSyncHandler([this](auto *self, libcomm::Msg msg) {
      if (msg.is_request()) {
        if (msg.cmd() == libcomm::Cmd::Probe) {
          probe_msg_ = msg;
          probe_event_.notify(SC_ZERO_TIME);
          return;
        }

        if (msg.cmd() == libcomm::Cmd::Terminate) {
          terminate_msg_ = msg;
          terminate_event_.notify(SC_ZERO_TIME);
          return;
        }

        std::lock_guard<std::mutex> lock(payload_meta_map_mutex_);
        auto *payload = lv::mm::ThreadSafePool::Allocate();
        auto [it, ok] = payload_meta_map_.insert(
            {payload, {msg, std::vector<uint8_t>(msg.size(), 0)}});

        if (!ok) {
          // internal error
          assert(0);
        }

        auto &[key, value] = *it;
        auto &[msg, buf] = value;

        payload->acquire();
        payload->set_address(msg.addr());
        payload->set_data_length(msg.size());
        payload->set_data_ptr(buf.data());
        payload->set_byte_enable_ptr(nullptr);
        payload->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        if (msg.cmd() == libcomm::Cmd::Get) {
          payload->set_read();
        }

        if (msg.cmd() == libcomm::Cmd::Put) {
          payload->set_write();
          std::memcpy(buf.data(), msg.data(), msg.size());
        }

        req_q_fw_.push(payload);
      }

      if (msg.is_response()) {
        std::lock_guard<std::mutex> lock(id_payload_map_mutex_);

        auto it = id_payload_map_.find(msg.id());
        if (it == id_payload_map_.end()) {
          // internal error
          assert(0);
        }
        auto [key, payload] = *it;
        payload->set_response_status(ToTlmStatus(msg.status()));

        if (msg.has_data()) {
          std::memcpy(payload->get_data_ptr(), msg.data(), msg.size());
        }
        id_payload_map_.erase(msg.id());

        if (debug_) {
          cv_.notify_one();
        } else {
          resp_q_fw_.push(payload);
        }
      }
    });
  }

  static libcomm::Status FromTlmStatus(tlm::tlm_response_status status) {
    switch (status) {
      case tlm::TLM_OK_RESPONSE:
        return libcomm::Status::Okay;
      case tlm::TLM_COMMAND_ERROR_RESPONSE:
        return libcomm::Status::CmdErr;
      case tlm::TLM_ADDRESS_ERROR_RESPONSE:
        return libcomm::Status::AddrErr;
      default:
        // warning
      case tlm::TLM_GENERIC_ERROR_RESPONSE:
        return libcomm::Status::GenericErr;
    }
  }

  static tlm::tlm_response_status ToTlmStatus(libcomm::Status status) {
    switch (status) {
      case libcomm::Status::Okay:
        return tlm::TLM_OK_RESPONSE;
      case libcomm::Status::CmdErr:
        return tlm::TLM_COMMAND_ERROR_RESPONSE;
      case libcomm::Status::AddrErr:
        return tlm::TLM_ADDRESS_ERROR_RESPONSE;
      default:
        // warning
      case libcomm::Status::GenericErr:
        return tlm::TLM_GENERIC_ERROR_RESPONSE;
    }
  }

  timeval timeout_;

  std::unique_ptr<std::thread> accept_thread_;
  std::unique_ptr<libcomm::Transceiver> transceiver_;

  std::mutex m_;
  std::condition_variable cv_;

  class thread_safe_event : sc_prim_channel {
   public:
    void notify(sc_time delay = SC_ZERO_TIME) {
      delay_ = delay;
      async_request_update();
    }
    operator const sc_event &(void) const { return event_; }

   protected:
    virtual void update(void) { event_.notify(delay_); }

   private:
    sc_event event_;
    sc_time delay_;
  };

  libcomm::Msg probe_msg_{libcomm::Msg::Build(libcomm::Cmd::Probe)};
  thread_safe_event probe_event_;
  libcomm::Msg terminate_msg_{libcomm::Msg::Build(libcomm::Cmd::Terminate)};
  thread_safe_event terminate_event_;

  // Thread-safe std::queue of tlm_generic_payload, with push event.
  class PayloadQueue {
   public:
    using T = tlm::tlm_generic_payload *;

    void push(const T &payload) {
      std::lock_guard<std::mutex> lock(m_);
      q_.push(payload);
      push_event_.notify();
    }

    T pop() {
      assert(!q_.empty());
      std::lock_guard<std::mutex> lock(m_);
      T tmp = q_.front();
      q_.pop();
      return tmp;
    }

    size_t size() const {
      std::lock_guard<std::mutex> lock(m_);
      return q_.size();
    }

    const sc_event &push_event() const { return push_event_; }

   private:
    thread_safe_event push_event_;
    mutable std::mutex m_;
    std::queue<tlm::tlm_generic_payload *> q_;
  };

  PayloadQueue req_q_fw_;
  PayloadQueue resp_q_fw_;

  using Meta = std::pair<libcomm::Msg, std::vector<uint8_t>>;
  std::unordered_map<tlm::tlm_generic_payload *, Meta> payload_meta_map_;
  std::mutex payload_meta_map_mutex_;

  uint32_t id_ = 0;
  std::unordered_map<uint32_t, tlm::tlm_generic_payload *> id_payload_map_;
  std::mutex id_payload_map_mutex_;

  lv::TlmSink sink_;
  lv::TlmSource source_;

  std::function<void()> probe_hook_;

  bool debug_;
  bool ignore_terminate_;
};

LV_BINDING(ipc, Agent)
    .constructor(
        [](const char *name, const Param &param) {
          return std::make_shared<Agent>(
              name, param.socket_path, param.timeout_ms, param.debug,
              [probe_hook = param.probe_hook]() {
                if (probe_hook.has_value()) {
                  (*probe_hook)();
                }
              },
              param.ignore_terminate);
        },
        lv::params("name", "param"), lv::doc("Create an IPC bridge agent"))
    .property("target", &Agent::set_target, lv::doc("Outgoing memory target"))
    .property("port", &Agent::port, lv::doc("Incoming IPC TLM port"));

}  // namespace ipc
