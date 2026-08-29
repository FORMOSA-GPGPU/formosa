/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace libcomm {

#define ENUM(name, ...)                                       \
  enum class name : uint32_t { __VA_ARGS__ };                 \
  inline std::ostream &operator<<(std::ostream &os, name e) { \
    constexpr static std::string_view s = #__VA_ARGS__;       \
    uint32_t n = static_cast<uint32_t>(e);                    \
    size_t curr = 0;                                          \
    for (size_t i = 0; i < n && curr < s.length(); i++) {     \
      size_t comma_pos = s.find(',', curr);                   \
      curr = comma_pos + 1;                                   \
      while (curr < s.length() && s[curr] == ' ') {           \
        curr++;                                               \
      }                                                       \
    }                                                         \
    return os << s.substr(curr, s.find(',', curr) - curr);    \
  }

ENUM(Cmd, Get, Put, Probe, Terminate, AccessAckData, AccessAck, ProbeAck,
     TerminateAck);

ENUM(Status, Okay, AddrErr, CmdErr, GenericErr);

#undef ENUM

class Msg;
class Transceiver;

namespace internal {
class MsgBuilder;
std::vector<uint8_t> Serialize(const Msg &);
}  // namespace internal

/**
 * Message for synchronous communications.
 */
class Msg {
 public:
  Cmd cmd() const { return header_.cmd; }
  uint32_t id() const { return header_.id; }
  uint64_t addr() const { return header_.addr; }
  size_t size() const { return header_.size; }
  Status status() const { return header_.status; }
  const uint8_t *data() const { return data_; }

  bool is_request() const {
    return cmd() == Cmd::Get || cmd() == Cmd::Put || cmd() == Cmd::Probe ||
           cmd() == Cmd::Terminate;
  }

  bool is_response() const {
    return cmd() == Cmd::AccessAckData || cmd() == Cmd::AccessAck ||
           cmd() == Cmd::ProbeAck || cmd() == Cmd::TerminateAck;
  }

  bool has_data() const {
    return cmd() == Cmd::Put || cmd() == Cmd::AccessAckData;
  }

  bool operator==(const Msg &other) const {
    // Make sure all fields are identical.
    if (cmd() != other.cmd() || id() != other.id() || addr() != other.addr() ||
        size() != other.size() || status() != other.status()) {
      return false;
    }
    // If so, make sure the carrying data is identical too.
    return std::memcmp(data(), other.data(), size()) == 0;
  }

  /**
   * Construct a message with the given command.
   */
  static internal::MsgBuilder Build(Cmd cmd);
  /**
   * Clone a given message.
   */
  static internal::MsgBuilder Build(const Msg &msg);
  /**
   * Construct a corresponding response based on the given message.
   */
  static internal::MsgBuilder Respond(const Msg &msg);

 private:
  explicit Msg(Cmd cmd) : header_{cmd, 0, 0, 0, Status::Okay}, data_(nullptr) {}

  struct Header {
    Cmd cmd;
    uint32_t id;
    uint64_t addr;
    uint64_t size;
    Status status;
  } header_;
  const uint8_t *data_;

  inline static constexpr size_t kHeaderSize = 4 + 4 + 8 + 8 + 4;

  friend std::ostream &operator<<(std::ostream &os, const Msg &msg) {
    // clang-format off
    os << "cmd    = " << msg.header_.cmd << "\n"
       << "id     = " << msg.header_.id << "\n"
       << "addr   = " << reinterpret_cast<void *>(msg.header_.addr) << "\n"
       << "size   = " << msg.header_.size << "\n"
       << "status = " << msg.header_.status << "\n"
       << "data   = " << static_cast<const void *>(msg.data_);
    // clang-format on
    return os;
  }

  // Allow MsgBuilder to access the private constructor.
  friend class internal::MsgBuilder;

  // Allow these functions to access kHeaderSize, which is not intended to be
  // access by external users.
  friend std::vector<uint8_t> internal::Serialize(const Msg &);
  friend class Transceiver;
};

/**
 * Open a server.
 */
std::unique_ptr<std::thread> Serve(
    const char *, timeval *, std::function<void(std::unique_ptr<Transceiver>)>);

/**
 * Connect to a server.
 */
std::unique_ptr<Transceiver> Connect(
    const char *, timeval *, std::function<void()> f = [] {});

/**
 * Transceiver to send requests and receive responses via a given fd.
 */
class Transceiver {
 public:
  using SyncHandler = std::function<void(Transceiver *, const Msg &)>;

  virtual ~Transceiver() {
    connection_down_ = true;
    if (fd_ != -1) {
      shutdown(fd_, SHUT_RDWR);
      close(fd_);
    }
    if (recv_thread_.joinable()) {
      recv_thread_.join();
    }
  }

  void RegisterSyncHandler(SyncHandler handler) {
    sync_handler_ = std::move(handler);

    if (!recv_thread_.joinable()) {
      recv_thread_ = std::thread(&Transceiver::Recv, this);
    }
  }

  /**
   * Send a message via synchronous communication.
   */
  int Send(const Msg &msg);

  /**
   * Send an asynchronous event.
   */
  int Send();

  bool IsConnectionAlive() const { return !connection_down_; }

 private:
  explicit Transceiver(int fd, pid_t pid = 0)
      : fd_(fd), pid_(pid), connection_down_(false), sync_handler_(sync_nop_) {}

  void Recv();

  int fd_;
  pid_t pid_;
  bool connection_down_;
  SyncHandler sync_handler_;
  std::thread recv_thread_;
  std::mutex send_mtx_;

  // Default handlers do nothing.
  static inline const SyncHandler sync_nop_ = [](Transceiver *, const Msg &) {};

  // Only these functions can construct a transceiver.
  friend std::unique_ptr<std::thread> Serve(
      const char *, timeval *,
      std::function<void(std::unique_ptr<Transceiver>)>);
  friend std::unique_ptr<Transceiver> Connect(const char *, timeval *,
                                              std::function<void()>);
  friend std::unique_ptr<Transceiver> std::make_unique<Transceiver>(int &);
  friend std::unique_ptr<Transceiver> std::make_unique<Transceiver>(int &,
                                                                    pid_t &);
};

// Internal modules that should not be accessed by users.
namespace internal {

class MsgBuilder final {
 public:
  MsgBuilder &id(uint32_t id) {
    msg_.header_.id = id;
    return *this;
  }

  MsgBuilder &addr(uint64_t addr) {
    msg_.header_.addr = addr;
    return *this;
  }

  MsgBuilder &size(size_t size) {
    msg_.header_.size = size;
    return *this;
  }

  MsgBuilder &status(Status status) {
    msg_.header_.status = status;
    return *this;
  }

  MsgBuilder &data(const uint8_t *data) {
    msg_.data_ = data;
    return *this;
  }

  operator Msg() const { return msg_; }

 private:
  /**
   * Copy a message.
   */
  MsgBuilder(const Msg &msg) : msg_(msg) {}
  /**
   * Create a message with the given command.
   */
  MsgBuilder(Cmd cmd) : msg_(cmd) {}
  /**
   * Copy a message but set its command to the given one.
   */
  MsgBuilder(const Msg &msg, Cmd cmd) : msg_(msg) { msg_.header_.cmd = cmd; }

  libcomm::Msg msg_;

  friend class libcomm::Msg;
};

/**
 * This function appends the byte representation of a given value `val` to a
 * `uint8_t` array pointed to by `p`, returning a pointer to the next available
 * position.
 */
template <class T>
inline uint8_t *AppendValue(uint8_t *p, T val) {
  for (size_t i = 0; i < sizeof(T); ++i) {
    *(p++) = val & 0xff;
    val >>= 8;
  }
  return p;
}

/**
 * This function reads `sizeof(T)` bytes from a `uint8_t` array pointed to by
 * `p`, reconstructs them into a value of type `T`, stores it in `*val`, and
 * returns a pointer to the next unread position.
 */
template <class T>
inline const uint8_t *ExtractValue(const uint8_t *p, T *val) {
  T tmp = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    tmp |= static_cast<T>(*(p++)) << i * 8;
  }
  *val = tmp;
  return p;
}

inline std::vector<uint8_t> Serialize(const Msg &msg) {
  size_t data_size = msg.has_data() * msg.size();
  std::vector<uint8_t> frame(Msg::kHeaderSize + data_size);
  uint8_t *curr = frame.data();

  // The order of the following statements determines the frame format.
  curr = AppendValue(curr, static_cast<uint32_t>(msg.cmd()));
  curr = AppendValue(curr, msg.id());
  curr = AppendValue(curr, msg.addr());
  curr = AppendValue(curr, msg.size());
  curr = AppendValue(curr, static_cast<uint32_t>(msg.status()));
  std::memcpy(curr, msg.data(), data_size);
  return frame;
}

inline Msg Deserialize(const std::vector<uint8_t> &frame) {
  uint32_t cmd = 0;
  uint32_t id = 0;
  uint64_t addr = 0;
  size_t size = 0;
  uint32_t status = 0;
  const uint8_t *curr = frame.data();

  // The order of the following statements must be consistent with the frame
  // format.
  curr = ExtractValue(curr, &cmd);
  curr = ExtractValue(curr, &id);
  curr = ExtractValue(curr, &addr);
  curr = ExtractValue(curr, &size);
  curr = ExtractValue(curr, &status);
  return Msg::Build(static_cast<Cmd>(cmd))
      .id(id)
      .addr(addr)
      .size(size)
      .status(static_cast<Status>(status))
      .data(size == 0 ? nullptr : curr);
}

/**
 * Send the data of given number of bytes via the fd.
 */
inline int Send(int fd, const uint8_t *buf, size_t len) {
  size_t total_bytes_sent = 0;
  while (total_bytes_sent < len) {
    ssize_t bytes_sent =
        send(fd, buf + total_bytes_sent, len - total_bytes_sent, MSG_NOSIGNAL);
    if (bytes_sent == 0) {
      return -1;
    }
    if (bytes_sent < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      return -1;
    }
    total_bytes_sent += static_cast<size_t>(bytes_sent);
  }
  return 0;
}

/**
 * Receive the given number of bytes via the fd and store it to the buffer.
 */
inline int Recv(int fd, uint8_t *buf, size_t len) {
  size_t total_bytes_recv = 0;
  while (total_bytes_recv < len) {
    ssize_t bytes_recv =
        recv(fd, buf + total_bytes_recv, len - total_bytes_recv, 0);
    if (bytes_recv == 0) {
      return -1;
    }
    if (bytes_recv < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      return -1;
    }
    total_bytes_recv += static_cast<size_t>(bytes_recv);
  }
  return 0;
}

/**
 * Process-global asynchronous handler.
 */
inline std::function<void()> async_handler;

}  // namespace internal

inline internal::MsgBuilder Msg::Build(const Msg &msg) {
  return internal::MsgBuilder(msg);
}

inline internal::MsgBuilder Msg::Build(Cmd cmd) {
  return internal::MsgBuilder(cmd);
}

inline internal::MsgBuilder Msg::Respond(const Msg &msg) {
  switch (msg.cmd()) {
    case Cmd::Get:
      return internal::MsgBuilder(msg, Cmd::AccessAckData);
    case Cmd::Put:
      return internal::MsgBuilder(msg, Cmd::AccessAck);
    case Cmd::Probe:
      return internal::MsgBuilder(msg, Cmd::ProbeAck);
    case Cmd::Terminate:
      return internal::MsgBuilder(msg, Cmd::TerminateAck);
    default:
      return internal::MsgBuilder(msg);
  }
}

inline int Transceiver::Send(const Msg &msg) {
  std::lock_guard<std::mutex> lock(send_mtx_);
  std::vector<uint8_t> frame = internal::Serialize(msg);
  return internal::Send(fd_, frame.data(), frame.size());
}

inline int Transceiver::Send() {
  if (pid_ <= 0) {
    return -1;
  }
  return kill(pid_, SIGUSR1);
}

inline void Transceiver::Recv() {
  std::vector<uint8_t> buf(Msg::kHeaderSize);
  for (;;) {
    // First receive `HeaderSize` bytes of data for the header.
    if (internal::Recv(fd_, buf.data(), Msg::kHeaderSize) < 0) {
      break;
    }
    Msg msg = internal::Deserialize(buf);

    // If the message contains extra data, then receive again.
    if (msg.has_data()) {
      // Resize if needed.
      if (buf.size() < Msg::kHeaderSize + msg.size()) {
        buf.resize(Msg::kHeaderSize + msg.size());
      }
      if (internal::Recv(fd_, buf.data() + Msg::kHeaderSize, msg.size()) < 0) {
        break;
      }
      msg = internal::Deserialize(buf);
    }
    sync_handler_(this, msg);
  }
  connection_down_ = true;
}

inline std::unique_ptr<std::thread> Serve(
    const char *socket_path, timeval *timeout,
    std::function<void(std::unique_ptr<Transceiver>)> on_accept) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    perror("socket");
    return nullptr;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
  unlink(socket_path);

  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
    perror("bind");
    return nullptr;
  }

  if (listen(fd, 1024) == -1) {
    perror("listen");
    return nullptr;
  }

  return std::make_unique<std::thread>([fd, timeout, on_accept]() {
    for (;;) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(fd, &read_fds);

      // select(2) mutates the timeval argument; keep the original timeout by
      // passing a fresh copy on each loop iteration.
      timeval timeout_copy{};
      timeval *timeout_ptr = nullptr;
      if (timeout != nullptr) {
        timeout_copy = *timeout;
        timeout_ptr = &timeout_copy;
      }

      int activity = select(fd + 1, &read_fds, nullptr, nullptr, timeout_ptr);
      if (activity < 0) {
        perror("select");
        return;
      } else if (activity == 0) {
        // Timeout occurred, no incoming connection
        continue;
      }

      if (FD_ISSET(fd, &read_fds)) {
        struct sockaddr client_addr;
        socklen_t client_addr_len = 32;
        int client_fd = accept(fd, &client_addr, &client_addr_len);
        if (client_fd == -1) {
          perror("accept");
          return;
        }

#ifdef __APPLE__
        // macOS has neither struct ucred nor SO_PEERCRED. Use LOCAL_PEERPID to
        // obtain the peer's pid, which is the only credential field libcomm
        // consumes here.
        pid_t peer_pid;
        socklen_t len = sizeof(peer_pid);
        if (getsockopt(client_fd, SOL_LOCAL, LOCAL_PEERPID, &peer_pid, &len) <
            0) {
          perror("getsockopt");
          close(client_fd);
          continue;
        }
        on_accept(std::make_unique<Transceiver>(client_fd, peer_pid));
#else
        struct ucred creds;
        socklen_t len = sizeof(creds);
        if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) < 0) {
          perror("getsockopt");
          close(client_fd);
          continue;
        }
        on_accept(std::make_unique<Transceiver>(client_fd, creds.pid));
#endif
      }
    }
  });
}

inline std::unique_ptr<Transceiver> Connect(
    const char *socket_path, timeval *timeout,
    std::function<void()> async_handler) {
  if (socket_path == nullptr) {
    return nullptr;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    perror("socket");
    return nullptr;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
    perror("connect");
    close(fd);
    return nullptr;
  }

  if (timeout != nullptr && setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, timeout,
                                       sizeof(*timeout)) == -1) {
    perror("setsockopt");
    close(fd);
    return nullptr;
  }

  internal::async_handler = async_handler;
  std::signal(SIGUSR1, [](int sig) {
    if (sig == SIGUSR1) {
      internal::async_handler();
    }
  });

  return std::make_unique<Transceiver>(fd);
}

}  // namespace libcomm
