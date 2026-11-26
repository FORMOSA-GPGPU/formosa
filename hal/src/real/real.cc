// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// fsa_real_* forward to the REAL backend FORMOSA_REAL names, which is chosen
// once, the first time HAL reaches the device, and kept for the process.

#include <real/real.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using formosa::real::Backend;

// pci-driver.cc is built for Linux only.
const Backend *const kBackends[] = {
    &formosa::real::kLvBackend,
#if defined(__linux__)
    &formosa::real::kPciBackend,
#endif
};

// FORMOSA_REAL names no backend of this build: there is no device to reach,
// so every call fails.
template <typename... Args>
int fail(Args...) {
  return -1;
}
template <typename... Args>
void ignore(Args...) {}

const Backend kUnavailable = {
    .name = "unavailable",
    .init = fail,
    .probe = fail,
    .mmio = fail,
    .cp_reset = fail,
    .copy_to_scratchpad = fail,
    .copy_from_scratchpad = fail,
    .get = fail,
    .dma_map = fail,
    .dma_unmap = ignore,
    .poll_backoff = fail,
    .time_ns = fail,
    .annotate = fail,
    .abort = ignore,
    .cleanup = ignore,
};

const Backend &select_backend() {
  const char *name = getenv("FORMOSA_REAL");
  if (name == nullptr || *name == '\0') return formosa::real::kLvBackend;
  std::string names;
  for (const Backend *backend : kBackends) {
    if (strcmp(backend->name, name) == 0) return *backend;
    names += names.empty() ? backend->name : std::string(", ") + backend->name;
  }
  fprintf(stderr, "FORMOSA_REAL=%s: no such REAL backend (built: %s)\n", name,
          names.c_str());
  return kUnavailable;
}

const Backend &backend() {
  static const Backend &selected = select_backend();
  return selected;
}

}  // namespace

int fsa_real_init() { return backend().init(); }

int fsa_real_probe() { return backend().probe(); }

int fsa_real_mmio(uint64_t offset, int64_t wr_val, uint64_t *rd_ptr) {
  return backend().mmio(offset, wr_val, rd_ptr);
}

int fsa_real_cp_reset() { return backend().cp_reset(); }

int fsa_real_copy_to_scratchpad(uintptr_t dev_addr, const void *host_ptr,
                                size_t size) {
  return backend().copy_to_scratchpad(dev_addr, host_ptr, size);
}

int fsa_real_copy_from_scratchpad(uintptr_t dev_addr, void *host_ptr,
                                  size_t size) {
  return backend().copy_from_scratchpad(dev_addr, host_ptr, size);
}

int fsa_real_get(uintptr_t dev_addr, void *host_ptr, size_t size) {
  return backend().get(dev_addr, host_ptr, size);
}

int fsa_real_dma_map(void *host_ptr, size_t size, uint64_t *dma_addr) {
  return backend().dma_map(host_ptr, size, dma_addr);
}

void fsa_real_dma_unmap(uint64_t dma_addr, size_t size) {
  backend().dma_unmap(dma_addr, size);
}

int fsa_real_poll_backoff(uint64_t *now_ns) {
  return backend().poll_backoff(now_ns);
}

int fsa_real_time_ns(uint64_t *now_ns) { return backend().time_ns(now_ns); }

int fsa_real_annotate(uintptr_t dev_addr, const char *text, size_t size) {
  return backend().annotate(dev_addr, text, size);
}

void fsa_real_abort() { backend().abort(); }

void fsa_real_cleanup() { backend().cleanup(); }
