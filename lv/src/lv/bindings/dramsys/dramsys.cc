// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <DRAMSys/DRAMSys.h>
#include <liblv/binding.h>
#include <sys/mman.h>
#include <systemc.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view default_config_path =
    DRAMSYS_RESOURCE_DIR "/ddr4-example.json";

struct Param {
  std::string config = std::string(default_config_path);

  LV_SCHEMA(DRAMSys::DRAMSys, Param,
            LV_FIELD(config, "Path to the DRAMSys configuration JSON file"));
};

// DRAMSys 5.6 no longer owns the backing store; when the configuration
// enables storage ("StoreMode": "Store"), the integrator must provide the
// memory. Mirror the upstream simulator app: a lazily-committed, zero-filled
// mmap region spanning the whole memory, handed over via setBackingStore().
//
// Unlike upstream's simulator, the region is mapped unconditionally rather
// than gated on getSimConfig().storageEnabled: that accessor is public, but
// its return type is defined in DRAMSys/simulation/SimConfig.h, which
// DRAMSYS_INSTALL does not ship, so builds against an installed DRAMSys see
// only an incomplete type (upstream builds its simulator in-tree). Deriving
// the flag from the public Config::SimConfig instead would duplicate
// upstream's StoreMode defaulting rules. A MAP_NORESERVE mapping costs
// nothing until touched, and DRAMSys ignores the store when storage is
// disabled.
class BackedDRAMSys : public DRAMSys::DRAMSys {
 public:
  // Note: inside this class the injected-class-name DRAMSys shadows the
  // namespace, hence the :: qualification.
  BackedDRAMSys(const sc_core::sc_module_name &name,
                const ::DRAMSys::Config::Configuration &config)
      : ::DRAMSys::DRAMSys(name, config) {
    size_ = memorySize();
    void *storage = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (storage == MAP_FAILED)
      throw std::runtime_error("DRAMSys: failed to mmap backing store");
    storage_ = static_cast<unsigned char *>(storage);
    setBackingStore(storage_);
  }

  ~BackedDRAMSys() override {
    if (storage_ != nullptr) munmap(storage_, size_);
  }

 private:
  unsigned char *storage_ = nullptr;
  std::uint64_t size_ = 0;
};

std::shared_ptr<DRAMSys::DRAMSys> make_dramsys(
    const char *name, const DRAMSys::Config::Configuration &config) {
  return std::make_shared<BackedDRAMSys>(name, config);
}
}  // namespace

LV_MODULE(dramsys).constant("RESOURCE_DIR", DRAMSYS_RESOURCE_DIR,
                            lv::doc("DRAMSys resource directory"));

LV_BINDING_WITH_NAME(dramsys, DRAMSys::DRAMSys, "DRAMSys")
    .constructor(
        [](const char *name) {
          auto config = DRAMSys::Config::from_path(default_config_path);
          return make_dramsys(name, config);
        },
        lv::params("name"), lv::doc("Create a DRAMSys memory system"))
    .constructor(
        [](const char *name, const Param &param) {
          auto config = DRAMSys::Config::from_path(param.config);
          return make_dramsys(name, config);
        },
        lv::params("name", "param"),
        lv::doc("Create a DRAMSys memory system from a configuration table"))
    .property("port", &DRAMSys::DRAMSys::tSocket, lv::doc("TLM target socket"));
