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
  bool synchronous = false;
  std::optional<sol::function> probe_hook;

  // clang-format off
  LV_SCHEMA(Agent, Param,
            LV_FIELD(socket_path, "IPC socket path"),
            LV_FIELD(timeout_ms, "Socket timeout in milliseconds"),
            LV_FIELD(debug, "Enable debug-transport mode"),
            LV_FIELD(ignore_terminate, "Ack Terminate without pausing the simulation"),
            LV_FIELD(synchronous, "Freeze simulation between external transactions after start()"),
            LV_FIELD(probe_hook, "Callback invoked on probe requests"))
  // clang-format on
};

}  // namespace

SC_MODULE(Agent) {
 public:
  SC_CTOR(Agent, std::string_view socket_path, uint32_t timeout_ms, bool debug,
          std::function<void()> probe_hook, bool ignore_terminate,
          bool synchronous)
      : timeout_(
            {.tv_sec = static_cast<time_t>(timeout_ms / 1000),
             .tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000)}),
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
        ignore_terminate_(ignore_terminate),
        synchronous_(synchronous) {
    if (synchronous_) {
      SC_THREAD(HandleSynchronous);
    } else {
      SC_THREAD(HandleReqFw);
      SC_THREAD(HandleRespFw);
      SC_THREAD(HandleRespBw);
      SC_THREAD(HandleWait);
    }
    SC_THREAD(HandleReqBw);
    if (!synchronous_) {
      SC_THREAD(HandleProbe);
      SC_THREAD(HandleTerminate);
    }
  }

  ~Agent() {
    accept_thread_->detach();
    // Join the receiver while its transaction queues and maps are still alive.
    std::lock_guard<std::mutex> lock(transceiver_mutex_);
    transceiver_.reset();
  }

  void Start() {
    if (started_) return;
    started_ = true;
    start_event_.notify(SC_ZERO_TIME);
  }

  // Block the SystemC kernel so host delays consume no simulated time;
  // an sc_event wait would let other simulation processes advance.
  void HandleSynchronous() {
    while (!started_) wait(start_event_);
    for (;;) {
      std::unique_lock<std::mutex> lock(incoming_mutex_);
      incoming_cv_.wait(lock, [&] {
        return !incoming_.empty();
      });
      auto incoming = std::move(incoming_.front());
      incoming_.pop();
      lock.unlock();
      const auto &msg = incoming.msg;
      if (msg.cmd() == libcomm::Cmd::Probe) {
        probe_hook_();
        Send(libcomm::Msg::Respond(msg), incoming.peer);
      } else if (msg.cmd() == libcomm::Cmd::Terminate) {
        Send(libcomm::Msg::Respond(msg), incoming.peer);
        if (!ignore_terminate_) {
          terminated_ = true;
          // The next sc_start() may drain device work without a host.
          sc_pause();
          wait(SC_ZERO_TIME);
          return;
        }
      } else if (msg.cmd() == libcomm::Cmd::Wait) {
        RespondWait(msg, incoming.peer);
      } else {
        auto *payload = req_q_fw_.pop();
        if (debug_) {
          source_.PutRequestDbg(*payload);
        } else {
          source_.req_port->write(payload);
          auto *response = source_.resp_port->read();
          assert(response == payload);
        }
        SendResponse(payload);
      }
    }
  }

  void RespondWait(const libcomm::Msg &msg,
                   std::weak_ptr<libcomm::Transceiver> peer) {
    if (msg.size() != 0) {
      Send(libcomm::Msg::Respond(msg).status(libcomm::Status::CmdErr), peer);
      return;
    }
    if (msg.addr() != 0) wait(sc_time(msg.addr(), SC_NS));
    const auto ns = static_cast<uint64_t>(sc_time_stamp() / sc_time(1, SC_NS));
    Send(libcomm::Msg::Respond(msg).addr(ns), peer);
  }

  void HandleWait() {
    for (;;) {
      std::unique_lock<std::mutex> lock(incoming_mutex_);
      if (incoming_.empty()) {
        lock.unlock();
        wait(wait_event_);
        continue;
      }
      auto incoming = incoming_.front();
      incoming_.pop();
      lock.unlock();
      RespondWait(incoming.msg, incoming.peer);
    }
  }

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
    while (synchronous_ && !started_) wait(start_event_);
    for (;;) {
      auto *payload = sink_.req_port->read();
      payload->acquire();
      SendRequest(payload);
      if (synchronous_) {
        // Freeze time during host memory service; the socket thread stays live.
        auto *response = resp_q_fw_.wait_pop();
        assert(response == payload);
        sink_.resp_port->write(response);
        response->release();
      }
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
      Send(libcomm::Msg::Respond(probe_msg_), probe_peer_);
    }
  }

  void HandleTerminate() {
    for (;;) {
      wait(terminate_event_);
      Send(libcomm::Msg::Respond(terminate_msg_), terminate_peer_);
      if (!ignore_terminate_) {
        terminated_ = true;
        sc_pause();
      }
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
    std::unique_lock<std::mutex> lock(id_payload_map_mutex_);
    cv_.wait(lock, [&] {
      return payload.get_response_status() != tlm::TLM_INCOMPLETE_RESPONSE;
    });
    return payload.is_response_ok() ? payload.get_data_length() : 0;
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
        payload->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        CompleteReverse(payload);
        return;
    }

    auto peer = GetTransceiver();
    std::lock_guard<std::mutex> lock(id_payload_map_mutex_);
    const auto id = ++id_;
    if (!terminated_ && peer && peer->IsConnectionAlive()) {
      id_payload_map_.emplace(id, Pending{payload, peer.get()});
      if (peer->Send(libcomm::Msg::Build(cmd)
                         .id(id)
                         .addr(payload->get_address())
                         .size(payload->get_data_length())
                         .data(payload->get_data_ptr())) == 0)
        return;
      id_payload_map_.erase(id);
    }
    payload->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    CompleteReverse(payload);
  }

  void CompleteReverse(tlm::tlm_generic_payload * payload) {
    if (debug_) {
      cv_.notify_one();
    } else {
      resp_q_fw_.push(payload, !synchronous_);
    }
  }

  void CancelReverse(libcomm::Transceiver * peer) {
    std::lock_guard<std::mutex> lock(id_payload_map_mutex_);
    for (auto it = id_payload_map_.begin(); it != id_payload_map_.end();) {
      if (it->second.peer != peer) {
        ++it;
        continue;
      }
      auto *payload = it->second.payload;
      payload->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      CompleteReverse(payload);
      it = id_payload_map_.erase(it);
    }
  }

  std::shared_ptr<libcomm::Transceiver> GetTransceiver() {
    std::lock_guard<std::mutex> lock(transceiver_mutex_);
    return transceiver_;
  }

  void Send(const libcomm::Msg &msg,
            std::weak_ptr<libcomm::Transceiver> origin) {
    auto peer = origin.lock();
    if (peer && peer->IsConnectionAlive()) peer->Send(msg);
  }

  void SendResponse(tlm::tlm_generic_payload * payload) {
    std::shared_ptr<libcomm::Transceiver> peer;
    std::lock_guard<std::mutex> lock(payload_meta_map_mutex_);
    auto it = payload_meta_map_.find(payload);
    if (it == payload_meta_map_.end()) {
      // internal error
      assert(0);
    }
    auto &[key, value] = *it;
    auto &[msg, data, origin] = value;

    peer = origin.lock();
    if (peer && peer->IsConnectionAlive())
      peer->Send(libcomm::Msg::Respond(msg)
                     .data(data.data())
                     .status(FromTlmStatus(payload->get_response_status())));

    payload_meta_map_.erase(payload);
    payload->release();
  }

  void set_transceiver(std::unique_ptr<libcomm::Transceiver> transceiver) {
    std::shared_ptr<libcomm::Transceiver> peer = std::move(transceiver);
    std::shared_ptr<libcomm::Transceiver> previous;
    {
      std::lock_guard<std::mutex> lock(transceiver_mutex_);
      previous = std::exchange(transceiver_, peer);
    }
    peer->RegisterSyncHandler(
        [this, origin = std::weak_ptr(peer)](auto *self, libcomm::Msg msg) {
          if (msg.is_request()) {
            if (msg.cmd() == libcomm::Cmd::Wait) {
              QueueIncoming(msg, origin);
              return;
            }
            if (msg.cmd() == libcomm::Cmd::Probe) {
              if (synchronous_) {
                QueueIncoming(msg, origin);
                return;
              }
              probe_msg_ = msg;
              probe_peer_ = origin;
              probe_event_.notify(SC_ZERO_TIME);
              return;
            }

            if (msg.cmd() == libcomm::Cmd::Terminate) {
              if (synchronous_) {
                QueueIncoming(msg, origin);
                return;
              }
              terminate_msg_ = msg;
              terminate_peer_ = origin;
              terminate_event_.notify(SC_ZERO_TIME);
              return;
            }

            std::lock_guard<std::mutex> lock(payload_meta_map_mutex_);
            auto *payload = lv::mm::ThreadSafePool::Allocate();
            auto [it, ok] = payload_meta_map_.insert(
                {payload, {msg, std::vector<uint8_t>(msg.size(), 0), origin}});

            if (!ok) {
              // internal error
              assert(0);
            }

            auto &[key, value] = *it;
            auto &[msg, buf, peer] = value;

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

            req_q_fw_.push(payload, !synchronous_);
            if (synchronous_) QueueIncoming(msg, origin);
          }

          if (msg.is_response()) {
            std::lock_guard<std::mutex> lock(id_payload_map_mutex_);

            auto it = id_payload_map_.find(msg.id());
            if (it == id_payload_map_.end() || it->second.peer != self) return;
            auto *payload = it->second.payload;
            payload->set_response_status(ToTlmStatus(msg.status()));

            if (msg.has_data()) {
              std::memcpy(payload->get_data_ptr(), msg.data(), msg.size());
            }
            id_payload_map_.erase(msg.id());

            CompleteReverse(payload);
          }
        },
        [this](auto *self) {
          CancelReverse(self);
        });
  }

  void QueueIncoming(const libcomm::Msg &msg,
                     std::weak_ptr<libcomm::Transceiver> peer) {
    std::lock_guard<std::mutex> lock(incoming_mutex_);
    incoming_.push({msg, peer});
    incoming_cv_.notify_one();
    if (!synchronous_) wait_event_.notify();
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
  std::shared_ptr<libcomm::Transceiver> transceiver_;
  std::mutex transceiver_mutex_;

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

  std::weak_ptr<libcomm::Transceiver> probe_peer_;
  std::weak_ptr<libcomm::Transceiver> terminate_peer_;
  libcomm::Msg probe_msg_{libcomm::Msg::Build(libcomm::Cmd::Probe)};
  thread_safe_event probe_event_;
  libcomm::Msg terminate_msg_{libcomm::Msg::Build(libcomm::Cmd::Terminate)};
  thread_safe_event terminate_event_;
  thread_safe_event wait_event_;

  // Thread-safe std::queue of tlm_generic_payload, with push event.
  class PayloadQueue {
   public:
    using T = tlm::tlm_generic_payload *;

    void push(const T &payload, bool notify_simulation = true) {
      std::lock_guard<std::mutex> lock(m_);
      q_.push(payload);
      if (notify_simulation) push_event_.notify();
      cv_.notify_one();
    }

    T pop() {
      std::lock_guard<std::mutex> lock(m_);
      assert(!q_.empty());
      T tmp = q_.front();
      q_.pop();
      return tmp;
    }

    T wait_pop() {
      std::unique_lock<std::mutex> lock(m_);
      cv_.wait(lock, [&] {
        return !q_.empty();
      });
      T result = q_.front();
      q_.pop();
      return result;
    }

    size_t size() const {
      std::lock_guard<std::mutex> lock(m_);
      return q_.size();
    }

    const sc_event &push_event() const { return push_event_; }

   private:
    thread_safe_event push_event_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::queue<tlm::tlm_generic_payload *> q_;
  };

  PayloadQueue req_q_fw_;
  PayloadQueue resp_q_fw_;

  struct Meta {
    libcomm::Msg msg;
    std::vector<uint8_t> data;
    std::weak_ptr<libcomm::Transceiver> peer;
  };
  std::unordered_map<tlm::tlm_generic_payload *, Meta> payload_meta_map_;
  std::mutex payload_meta_map_mutex_;

  uint32_t id_ = 0;
  struct Pending {
    tlm::tlm_generic_payload *payload;
    libcomm::Transceiver *peer;
  };
  std::unordered_map<uint32_t, Pending> id_payload_map_;
  std::mutex id_payload_map_mutex_;

  lv::TlmSink sink_;
  lv::TlmSource source_;

  std::function<void()> probe_hook_;

  bool debug_;
  bool ignore_terminate_;
  bool synchronous_;
  bool started_ = false;
  bool terminated_ = false;
  sc_event start_event_;
  struct Incoming {
    libcomm::Msg msg;
    std::weak_ptr<libcomm::Transceiver> peer;
  };
  std::mutex incoming_mutex_;
  std::condition_variable incoming_cv_;
  std::queue<Incoming> incoming_;
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
              param.ignore_terminate, param.synchronous);
        },
        lv::params("name", "param"), lv::doc("Create an IPC bridge agent"))
    .method("start", &Agent::Start,
            lv::doc("Start synchronous service after initialization; "
                    "asynchronous service starts automatically"))
    .property("target", &Agent::set_target, lv::doc("Outgoing memory target"))
    .property("port", &Agent::port, lv::doc("Incoming IPC TLM port"));

}  // namespace ipc
