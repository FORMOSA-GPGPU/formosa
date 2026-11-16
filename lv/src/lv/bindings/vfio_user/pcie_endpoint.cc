// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// libvfio-user's C headers use C11 _Static_assert, which C++ spells
// differently.
#define _Static_assert static_assert
extern "C" {
#include <libvfio-user.h>
}
#undef _Static_assert
// <syslog.h> priorities would shadow quill's LOG_* macros used by liblv/log.h.
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARNING

#include <liblv/binding.h>
#include <liblv/common/thread_safe_queue.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/log.h>
#include <liblv/mm/thread_safe_pool.h>
#include <liblv/schema.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <systemc.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace vfio_user {

/**
 * PCI device endpoint served to a vfio-user client such as QEMU's
 * vfio-user-pci. BAR accesses become TLM transactions on `target`.
 */
SC_MODULE(PcieEndpoint) {
 public:
  struct Bar {
    uint32_t index = 0;
    uint64_t base = 0;
    uint64_t size = 0;

    // clang-format off
    LV_SCHEMA(PcieEndpoint, Bar,
              LV_FIELD(index, "PCI BAR index (0-5)"),
              LV_FIELD(base, "Device address mapped at BAR offset 0"),
              LV_FIELD(size, "BAR size in bytes; a power of two"))
    // clang-format on
  };

  struct Param {
    std::string socket_path = "/tmp/vfio-user.sock";
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint32_t class_code = 0;
    std::vector<Bar> bars;

    // clang-format off
    LV_SCHEMA(PcieEndpoint, Param,
              LV_FIELD(socket_path, "vfio-user UNIX socket path"),
              LV_FIELD(vendor_id, "PCI vendor ID"),
              LV_FIELD(device_id, "PCI device ID"),
              LV_FIELD(class_code, "24-bit PCI class code (base, sub, prog-if)"),
              LV_FIELD(bars, "Memory BARs"))
    // clang-format on
  };

  PcieEndpoint(const sc_module_name &name, const Param &param)
      : sc_module(name), sink_("sink"), source_("source") {
    for (const auto &bar : param.bars) {
      if (bar.index >= 6 || bar.size < 16 || (bar.size & (bar.size - 1)) != 0)
        throw std::invalid_argument("vfio_user.PcieEndpoint: invalid BAR");
      if (bars_[bar.index].size != 0)
        throw std::invalid_argument(
            "vfio_user.PcieEndpoint: duplicate BAR index");
      bars_[bar.index] = bar;
    }
    if (param.vendor_id == 0 || param.vendor_id >= 0xffff ||
        param.device_id > 0xffff || param.class_code > 0xffffff)
      throw std::invalid_argument("vfio_user.PcieEndpoint: invalid PCI ID");

    // A stale socket from a previous run would make bind() fail.
    struct stat st{};
    if (stat(param.socket_path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode))
      unlink(param.socket_path.c_str());

    ctx_ = vfu_create_ctx(VFU_TRANS_SOCK, param.socket_path.c_str(),
                          LIBVFIO_USER_FLAG_ATTACH_NB, this, VFU_DEV_TYPE_PCI);
    if (ctx_ == nullptr) Fail("vfu_create_ctx");
    Check(vfu_setup_log(ctx_, Log, LOG_ERR), "vfu_setup_log");
    Check(vfu_pci_init(ctx_, VFU_PCI_TYPE_EXPRESS, PCI_HEADER_TYPE_NORMAL, 0),
          "vfu_pci_init");
    vfu_pci_set_id(ctx_, param.vendor_id, param.device_id, 0, 0);
    vfu_pci_set_class(ctx_, param.class_code >> 16,
                      (param.class_code >> 8) & 0xff, param.class_code & 0xff);
    struct pxcap px{};
    px.hdr.id = PCI_CAP_ID_EXP;
    px.pxcaps.ver = 2;
    Check(vfu_pci_add_capability(ctx_, 0, 0, &px), "vfu_pci_add_capability");
    for (const auto &bar : param.bars) {
      Check(vfu_setup_region(ctx_, VFU_PCI_DEV_BAR0_REGION_IDX + bar.index,
                             bar.size, kBarCallbacks[bar.index],
                             VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM, nullptr,
                             0, -1, 0),
            "vfu_setup_region");
    }
    Check(vfu_setup_device_dma(ctx_, LIBVFIO_USER_MAX_DMA_REGIONS, RegisterDma,
                               UnregisterDma),
          "vfu_setup_device_dma");
    Check(vfu_realize_ctx(ctx_), "vfu_realize_ctx");

    SC_THREAD(HandleBarAccess);
    SC_THREAD(HandleDma);
  }

  ~PcieEndpoint() override {
    stop_ = true;
    // Release a BAR access the stopped simulation will never complete.
    done_q_.push(nullptr, false);
    if (server_thread_.joinable()) server_thread_.join();
    vfu_destroy_ctx(ctx_);
  }

  // Serve the client only after reset so BAR accesses see an initialized
  // device.
  void Start() {
    if (server_thread_.joinable()) return;
    server_thread_ = std::thread(&PcieEndpoint::Serve, this);
  }

  using Target =
      tlm_utils::simple_initiator_socket<PcieEndpoint>::base_target_socket_type;
  void set_target(Target * t) { source_.set_target(t); }

  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;
  Source *port() const { return &sink_.port; }

 private:
  // libvfio-user calls are not thread-safe; only the server thread uses ctx_
  // after realize.
  void Serve() {
    while (!stop_) {
      if (vfu_attach_ctx(ctx_) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          LV_ERROR("vfio-user attach failed: {}", std::strerror(errno));
          return;
        }
        PollClient();
        continue;
      }
      LV_INFO("vfio-user client attached");
      while (!stop_) {
        if (!PollClient()) continue;
        if (vfu_run_ctx(ctx_) >= 0 || errno == EAGAIN || errno == EWOULDBLOCK)
          continue;
        if (errno != ENOTCONN) {
          LV_ERROR("vfio-user request failed: {}", std::strerror(errno));
          return;
        }
        LV_INFO("vfio-user client detached");
        break;
      }
    }
  }

  // Waits for a client message; bounded so the destructor can stop the server
  // thread. Returns whether the client is readable.
  bool PollClient() {
    pollfd fd = {vfu_get_poll_fd(ctx_), POLLIN, 0};
    return poll(&fd, 1, 100) > 0;
  }

  // Guest memory the client shares with this process. libvfio-user maps the
  // regions and calls these on the server thread; vaddr is null for a region
  // without a shareable fd.
  static void RegisterDma(vfu_ctx_t * ctx, vfu_dma_info_t * info) {
    auto *self = static_cast<PcieEndpoint *>(vfu_get_private(ctx));
    std::lock_guard<std::mutex> lock(self->dma_mtx_);
    self->dma_regions_[reinterpret_cast<uintptr_t>(info->iova.iov_base)] =
        *info;
  }

  // The SystemC thread holds dma_mtx_ while it copies, so no copy uses the
  // region once this returns.
  static void UnregisterDma(vfu_ctx_t * ctx, vfu_dma_info_t * info) {
    auto *self = static_cast<PcieEndpoint *>(vfu_get_private(ctx));
    std::lock_guard<std::mutex> lock(self->dma_mtx_);
    self->dma_regions_.erase(reinterpret_cast<uintptr_t>(info->iova.iov_base));
  }

  // Runs on the SystemC thread; copies straight to or from the shared guest
  // RAM, without waiting for the server thread.
  tlm::tlm_response_status TransferGuestMemory(tlm::tlm_generic_payload &
                                               payload) {
    const uint64_t addr = payload.get_address();
    const uint64_t size = payload.get_data_length();
    const bool to_guest = payload.is_write();
    std::lock_guard<std::mutex> lock(dma_mtx_);
    // shortcut: a transfer must lie in one client region. The DMA engine never
    // crosses a 64 B block and a guest IOMMU maps whole pages; split the
    // transfer if either changes.
    auto it = dma_regions_.upper_bound(addr);
    const vfu_dma_info_t *region =
        it == dma_regions_.begin() ? nullptr : &std::prev(it)->second;
    const uint64_t offset =
        region ? addr - reinterpret_cast<uintptr_t>(region->iova.iov_base) : 0;
    if (region == nullptr || offset >= region->iova.iov_len ||
        size > region->iova.iov_len - offset) {
      LV_WARNING("vfio-user DMA at 0x{:x} (+{}) is not mapped by the client",
                 addr, size);
      return tlm::TLM_ADDRESS_ERROR_RESPONSE;
    }
    // Message-based DMA collides with BAR accesses while QEMU's client lacks
    // twin-socket mode, so only guest RAM shared through an fd works.
    if (region->vaddr == nullptr) {
      LV_WARNING(
          "vfio-user DMA at 0x{:x}: guest RAM is not shared; start QEMU with "
          "memory-backend-memfd,share=on",
          addr);
      return tlm::TLM_ADDRESS_ERROR_RESPONSE;
    }
    if ((region->prot & (to_guest ? PROT_WRITE : PROT_READ)) == 0)
      return tlm::TLM_COMMAND_ERROR_RESPONSE;
    auto *guest = static_cast<unsigned char *>(region->vaddr) + offset;
    if (to_guest)
      std::memcpy(guest, payload.get_data_ptr(), size);
    else
      std::memcpy(payload.get_data_ptr(), guest, size);
    return tlm::TLM_OK_RESPONSE;
  }

  // Runs on the server thread; blocks the client until SystemC answers.
  ssize_t AccessBar(uint32_t index, char *buf, size_t count, loff_t offset,
                    bool is_write) {
    auto *payload = lv::mm::ThreadSafePool::Allocate();
    payload->acquire();
    payload->set_command(is_write ? tlm::TLM_WRITE_COMMAND
                                  : tlm::TLM_READ_COMMAND);
    payload->set_address(bars_[index].base + offset);
    payload->set_data_ptr(reinterpret_cast<unsigned char *>(buf));
    payload->set_data_length(count);
    payload->set_streaming_width(count);
    payload->set_byte_enable_ptr(nullptr);
    payload->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    req_q_.push(payload);

    auto *done = done_q_.wait_pop();
    if (done == nullptr) {
      errno = EIO;
      return -1;
    }
    assert(done == payload);
    const bool ok = payload->is_response_ok();
    payload->release();
    if (!ok) {
      errno = EIO;
      return -1;
    }
    return static_cast<ssize_t>(count);
  }

  void HandleBarAccess() {
    for (;;) {
      while (req_q_.size() == 0) wait(req_q_.push_event());
      auto *payload = req_q_.pop();
      source_.req_port->write(payload);
      auto *response = source_.resp_port->read();
      assert(response == payload);
      done_q_.push(response, false);
    }
  }

  void HandleDma() {
    for (;;) {
      auto *payload = sink_.req_port->read();
      payload->set_response_status(TransferGuestMemory(*payload));
      sink_.resp_port->write(payload);
    }
  }

  template <uint32_t I>
  static ssize_t BarCallback(vfu_ctx_t * ctx, char *buf, size_t count,
                             loff_t offset, bool is_write) {
    return static_cast<PcieEndpoint *>(vfu_get_private(ctx))
        ->AccessBar(I, buf, count, offset, is_write);
  }
  static constexpr vfu_region_access_cb_t *kBarCallbacks[] = {
      BarCallback<0>, BarCallback<1>, BarCallback<2>,
      BarCallback<3>, BarCallback<4>, BarCallback<5>};

  static void Log(vfu_ctx_t *, int, const char *msg) {
    LV_ERROR("libvfio-user: {}", msg);
  }

  [[noreturn]] static void Fail(const char *what) {
    throw std::runtime_error(std::string("vfio_user.PcieEndpoint: ") + what +
                             ": " + std::strerror(errno));
  }
  static void Check(int rc, const char *what) {
    if (rc < 0) Fail(what);
  }

  Bar bars_[6];
  vfu_ctx_t *ctx_ = nullptr;
  std::thread server_thread_;
  std::atomic<bool> stop_{false};
  lv::ThreadSafePayloadQueue req_q_;
  lv::ThreadSafePayloadQueue done_q_;
  std::mutex dma_mtx_;
  // The client's DMA regions by first IOVA; guarded by dma_mtx_.
  std::map<uint64_t, vfu_dma_info_t> dma_regions_;
  lv::TlmSink sink_;
  lv::TlmSource source_;
};

LV_BINDING(vfio_user, PcieEndpoint)
    .constructor(
        [](const char *name, const PcieEndpoint::Param &param) {
          return std::make_shared<PcieEndpoint>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a vfio-user PCI device endpoint"))
    .method("start", &PcieEndpoint::Start,
            lv::doc("Start serving the vfio-user client after reset"))
    .property("target", &PcieEndpoint::set_target,
              lv::doc("Target of BAR accesses"))
    .property("port", &PcieEndpoint::port,
              lv::doc("Device-initiated accesses to host memory"));

}  // namespace vfio_user
