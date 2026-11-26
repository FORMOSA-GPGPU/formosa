// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// REAL backend for the device on a PCI bus, driven from user space through
// VFIO. HAL uses it when FORMOSA_REAL=pci.
//
// Device access
//   The device is picked by FORMOSA_PCI_DEVICE (e.g. 0000:01:00.0) and must be
//   bound to vfio-pci behind an IOMMU. BAR0 is mapped from the VFIO device,
//   and every REAL register or scratchpad access becomes an MMIO load or
//   store on that mapping.
//
// Host memory
//   Each DMA mapping pins the pages that back the buffer now and maps them in
//   the IOMMU at an IOVA of its own, which HAL hands the device; nothing is
//   copied. Two mappings of one page are two IOVAs, so a mapping never
//   outlives its request or follows a page the application has replaced.

#include <fcntl.h>
#include <formosa_addr_map.h>
#include <libgen.h>
#include <linux/vfio.h>
#include <real/real.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <string>

namespace {

// PCI command register in config space.
constexpr off_t kPciCommandOffset = 0x4;
constexpr uint16_t kPciCommandMemory = 0x2;
constexpr uint16_t kPciCommandBusMaster = 0x4;

int container_fd = -1;
int group_fd = -1;
int device_fd = -1;
volatile uint8_t *bar = nullptr;
size_t bar_size = 0;

// IOVAs handed to DMA mappings.
// shortcut: a fixed window above x86's MSI range that 39-bit IOMMUs cover;
// read VFIO's IOVA ranges once a platform reserves part of it.
constexpr uint64_t kIovaBase = uint64_t{1} << 32;
constexpr uint64_t kIovaEnd = uint64_t{1} << 39;

// IOMMU mappings in flight by first IOVA, with the IOVA past their end.
std::map<uint64_t, uint64_t> mappings;
// Where the next search for free IOVA starts.
// shortcut: firmware publishes a timed-out copy before its DMA stops, so a
// released IOVA is reused only once the search wraps, and a late access hits
// no other buffer; reuse freely once firmware aborts the DMA on a timeout.
uint64_t next_iova = kIovaBase;
std::mutex mappings_mtx;

void pci_abort();

// One MMIO load or store of T. The volatile access keeps the compiler from
// merging, splitting or dropping it.
template <typename T>
void access_word(volatile uint8_t *mmio, uint8_t *host, bool write) {
  T value;
  if (write) {
    memcpy(&value, host, sizeof(T));
    *reinterpret_cast<volatile T *>(mmio) = value;
  } else {
    value = *reinterpret_cast<volatile T *>(mmio);
    memcpy(host, &value, sizeof(T));
  }
}

// Reads or writes `size` bytes at device address `dev_addr` through BAR0.
// MMIO only takes naturally aligned 4- or 8-byte accesses, which plain
// memcpy does not guarantee, so the transfer is split into such words.
int access_bar(uintptr_t dev_addr, void *host_ptr, size_t size, bool write) {
  const uint64_t offset = dev_addr - FSA_HOST_BAR_BASE;
  const bool in_bar = dev_addr >= FSA_HOST_BAR_BASE && offset <= bar_size &&
                      size <= bar_size - offset;
  if (bar == nullptr || host_ptr == nullptr || !in_bar || (offset | size) % 4)
    return -1;

  auto *host = static_cast<uint8_t *>(host_ptr);
  for (size_t done = 0; done < size;) {
    const bool wide = size - done >= 8 && (offset + done) % 8 == 0;
    if (wide)
      access_word<uint64_t>(bar + offset + done, host + done, write);
    else
      access_word<uint32_t>(bar + offset + done, host + done, write);
    done += wide ? 8 : 4;
  }
  return 0;
}

const char *pci_device() {
  const char *device = getenv("FORMOSA_PCI_DEVICE");
  if (device == nullptr || *device == '\0') {
    fprintf(stderr, "FORMOSA_PCI_DEVICE is not set (e.g. 0000:01:00.0)\n");
    return nullptr;
  }
  return device;
}

// The VFIO group node of the device's IOMMU group.
std::string group_path(const char *device) {
  const std::string link =
      std::string("/sys/bus/pci/devices/") + device + "/iommu_group";
  char target[256];
  const ssize_t length = readlink(link.c_str(), target, sizeof(target) - 1);
  if (length <= 0) {
    fprintf(stderr, "%s: %s (no IOMMU?)\n", link.c_str(), strerror(errno));
    return {};
  }
  target[length] = '\0';
  return std::string("/dev/vfio/") + basename(target);
}

int fail(const char *what) {
  fprintf(stderr, "VFIO %s: %s\n", what, strerror(errno));
  pci_abort();
  return -1;
}

// The first gap of `length` at or after `from` between the mappings in
// flight, or kIovaEnd if none fits.
uint64_t free_iova(uint64_t from, uint64_t length) {
  uint64_t iova = from;
  auto it = mappings.upper_bound(iova);
  if (it != mappings.begin() && std::prev(it)->second > iova)
    iova = std::prev(it)->second;
  for (; it != mappings.end(); ++it) {
    if (it->first - iova >= length) break;
    iova = it->second;
  }
  return length <= kIovaEnd - iova ? iova : kIovaEnd;
}

// The page-aligned range a request covers.
void page_range(uint64_t addr, size_t size, uint64_t *start, uint64_t *end) {
  const uint64_t page = sysconf(_SC_PAGESIZE);
  *start = addr & ~(page - 1);
  *end = (addr + size + page - 1) & ~(page - 1);
}

int iommu_map(uint64_t vaddr, uint64_t iova, uint64_t size) {
  vfio_iommu_type1_dma_map map = {};
  map.argsz = sizeof(map);
  map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
  map.vaddr = vaddr;
  map.iova = iova;
  map.size = size;
  if (ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &map) == 0) return 0;
  // Read-only memory cannot be pinned for writing. The device may still read
  // it; a write the IOMMU then refuses fails the command.
  if (errno != EFAULT) return -1;
  map.flags = VFIO_DMA_MAP_FLAG_READ;
  return ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &map);
}

void iommu_unmap(uint64_t start, uint64_t end) {
  vfio_iommu_type1_dma_unmap unmap = {};
  unmap.argsz = sizeof(unmap);
  unmap.iova = start;
  unmap.size = end - start;
  if (ioctl(container_fd, VFIO_IOMMU_UNMAP_DMA, &unmap) != 0)
    fprintf(stderr, "VFIO unmap: %s\n", strerror(errno));
}

int pci_probe() {
  const char *device = pci_device();
  if (device == nullptr || group_path(device).empty()) return -1;
  if (access("/dev/vfio/vfio", R_OK | W_OK) != 0) {
    fprintf(stderr, "/dev/vfio/vfio: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

int pci_init() {
  const char *device = pci_device();
  if (device == nullptr) return -1;
  const std::string group = group_path(device);
  if (group.empty()) return -1;

  container_fd = open("/dev/vfio/vfio", O_RDWR | O_CLOEXEC);
  if (container_fd < 0) return fail("container");
  // A group opens once, so this also claims the device for one application.
  // shortcut: a kernel driver must arbitrate contexts before applications can
  // share the device.
  group_fd = open(group.c_str(), O_RDWR | O_CLOEXEC);
  if (group_fd < 0) return fail(group.c_str());
  vfio_group_status status = {};
  status.argsz = sizeof(status);
  if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &status) != 0)
    return fail("group status");
  if ((status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0) {
    errno = EINVAL;
    return fail("group (bind every device in it to vfio-pci)");
  }
  if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd) != 0 ||
      ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1v2_IOMMU) != 0)
    return fail("IOMMU");
  device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, device);
  if (device_fd < 0) return fail(device);

  vfio_region_info bar0 = {};
  bar0.argsz = sizeof(bar0);
  bar0.index = VFIO_PCI_BAR0_REGION_INDEX;
  if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &bar0) != 0)
    return fail("BAR0");
  void *mapping = mmap(nullptr, bar0.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       device_fd, bar0.offset);
  if (mapping == MAP_FAILED) return fail("BAR0 mmap");
  bar = static_cast<volatile uint8_t *>(mapping);
  bar_size = bar0.size;

  // vfio-pci leaves memory decoding and bus mastering off until asked.
  vfio_region_info config = {};
  config.argsz = sizeof(config);
  config.index = VFIO_PCI_CONFIG_REGION_INDEX;
  if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &config) != 0)
    return fail("config space");
  const off_t command_at = config.offset + kPciCommandOffset;
  uint16_t command = 0;
  if (pread(device_fd, &command, sizeof(command), command_at) !=
      sizeof(command))
    return fail("config space");
  command |= kPciCommandMemory | kPciCommandBusMaster;
  if (pwrite(device_fd, &command, sizeof(command), command_at) !=
      sizeof(command))
    return fail("config space");
  return 0;
}

int pci_get(uintptr_t dev_addr, void *host_ptr, size_t size) {
  return access_bar(dev_addr, host_ptr, size, false);
}

int pci_mmio(uint64_t offset, int64_t wr_val, uint64_t *rd_ptr) {
  if (rd_ptr != nullptr)
    return access_bar(FSA_MMIO_BASE + offset, rd_ptr, 8, false);
  return access_bar(FSA_MMIO_BASE + offset, &wr_val, 8, true);
}

int pci_cp_reset() {
  /* Assert level-triggered MSIP.  Firmware clears the bit in the ISR
   * (or ROM reset_handler). */
  uint32_t reset = 1;
  return access_bar(FSA_CLINT_BASE + FSA_CLINT_MSIP_OFFSET, &reset,
                    sizeof(reset), true);
}

int pci_copy_to_scratchpad(uintptr_t dev_addr, const void *host_ptr,
                           size_t size) {
  return access_bar(dev_addr, const_cast<void *>(host_ptr), size, true);
}

int pci_copy_from_scratchpad(uintptr_t dev_addr, void *host_ptr, size_t size) {
  return access_bar(dev_addr, host_ptr, size, false);
}

int pci_dma_map(void *host_ptr, size_t size, uint64_t *dma_addr) {
  const uint64_t addr = reinterpret_cast<uintptr_t>(host_ptr);
  *dma_addr = addr;
  if (size == 0) return 0;
  uint64_t start, end;
  page_range(addr, size, &start, &end);
  const uint64_t length = end - start;

  std::lock_guard<std::mutex> lock(mappings_mtx);
  uint64_t iova = free_iova(next_iova, length);
  if (iova == kIovaEnd) iova = free_iova(kIovaBase, length);
  if (iova == kIovaEnd) {
    fprintf(stderr, "VFIO map of %zu bytes: IOVA space exhausted\n", size);
    return -1;
  }
  if (iommu_map(start, iova, length) != 0) {
    fprintf(stderr, "VFIO map of %zu bytes at %p: %s\n", size, host_ptr,
            strerror(errno));
    return -1;
  }
  mappings.emplace(iova, iova + length);
  next_iova = iova + length;
  *dma_addr = iova + (addr - start);
  return 0;
}

void pci_dma_unmap(uint64_t dma_addr, size_t size) {
  if (size == 0) return;
  uint64_t start, end;
  page_range(dma_addr, size, &start, &end);

  std::lock_guard<std::mutex> lock(mappings_mtx);
  const auto it = mappings.find(start);
  if (it == mappings.end()) return;
  iommu_unmap(it->first, it->second);
  mappings.erase(it);
}

// The device's clock is its timer, so HAL deadlines follow the device however
// fast it runs.
int pci_time_ns(uint64_t *now_ns) {
  const auto *config = formosa::real::configuration_snapshot();
  uint64_t ticks;
  if (config == nullptr ||
      access_bar(FSA_CLINT_MTIME_BASE, &ticks, sizeof(ticks), false) != 0)
    return -1;
  // Split so that ticks * 1e9 cannot overflow.
  const uint64_t hz = config->timebase_frequency;
  *now_ns = ticks / hz * 1000000000 + ticks % hz * 1000000000 / hz;
  return 0;
}

// A device on a PCI bus has no channel for annotations, so labels are not
// sent; HAL submits the command without its label.
int pci_annotate(uintptr_t, const char *, size_t) { return -1; }

int pci_poll_backoff(uint64_t *now_ns) {
  const timespec backoff = {0, 1000};
  nanosleep(&backoff, nullptr);
  return pci_time_ns(now_ns);
}

void pci_abort() {
  // Closing the container drops every IOMMU mapping.
  {
    std::lock_guard<std::mutex> lock(mappings_mtx);
    mappings.clear();
    next_iova = kIovaBase;
  }
  if (bar != nullptr) munmap(const_cast<uint8_t *>(bar), bar_size);
  bar = nullptr;
  bar_size = 0;
  for (int *fd : {&device_fd, &group_fd, &container_fd}) {
    if (*fd >= 0) close(*fd);
    *fd = -1;
  }
}

void pci_cleanup() { pci_abort(); }

}  // namespace

const formosa::real::Backend formosa::real::kPciBackend = {
    .name = "pci",
    .init = pci_init,
    .probe = pci_probe,
    .mmio = pci_mmio,
    .cp_reset = pci_cp_reset,
    .copy_to_scratchpad = pci_copy_to_scratchpad,
    .copy_from_scratchpad = pci_copy_from_scratchpad,
    .get = pci_get,
    .dma_map = pci_dma_map,
    .dma_unmap = pci_dma_unmap,
    .poll_backoff = pci_poll_backoff,
    .time_ns = pci_time_ns,
    .annotate = pci_annotate,
    .abort = pci_abort,
    .cleanup = pci_cleanup,
};
