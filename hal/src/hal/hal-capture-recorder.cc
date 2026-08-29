// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "hal-capture-recorder.h"

#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace formosa::hal {

CaptureRecorder::~CaptureRecorder() { Close(); }

void CaptureRecorder::InitIfNeeded() {
  if (initialized_) {
    return;
  }
  initialized_ = true;

  const char *path = std::getenv("FORMOSA_HAL_CAPTURE_TRACE");
  if (path == nullptr || path[0] == '\0') {
    return;
  }

  dir_ = path;
  std::error_code ec;
  std::filesystem::create_directories(dir_ / "blobs", ec);
  if (ec) {
    fprintf(stderr, "Failed to create HAL capture directory %s: %s\n",
            dir_.string().c_str(), ec.message().c_str());
    return;
  }

  events_.open(dir_ / "events.tsv", std::ios::out | std::ios::trunc);
  manifest_.open(dir_ / "manifest.txt", std::ios::out | std::ios::trunc);
  if (!events_.is_open() || !manifest_.is_open()) {
    fprintf(stderr, "Failed to open HAL capture files in %s\n",
            dir_.string().c_str());
    return;
  }

  enabled_ = true;
  events_ << "seq\ttype\taddr\tsize\talloc_tag\tvalue\tstatus\tblob\n";
  manifest_ << "version=2\n";
}

bool CaptureRecorder::enabled() {
  InitIfNeeded();
  return enabled_;
}

void CaptureRecorder::Close() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (events_.is_open()) {
    events_.flush();
    events_.close();
  }
  if (manifest_.is_open()) {
    manifest_.flush();
    manifest_.close();
  }
}

void CaptureRecorder::ManifestU64(const char *key, uint64_t value) {
  if (!enabled()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mtx_);
  manifest_ << key << "=0x" << std::hex << value << std::dec << "\n";
}

void CaptureRecorder::Event(std::string_view type, uint64_t addr, uint64_t size,
                            int64_t value, int64_t status,
                            std::string_view blob) {
  if (!enabled()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mtx_);
  EventLocked(type, addr, size, 0, value, status, blob);
}

void CaptureRecorder::CompletionSlotEvent(uint64_t slot_addr,
                                          uint64_t alloc_tag, int64_t result) {
  if (!enabled()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mtx_);
  EventLocked("completion_slot", slot_addr, 0, alloc_tag, result, 0, "");
}

void CaptureRecorder::BlobEvent(std::string_view type, uint64_t addr,
                                const void *data, size_t size, int64_t value,
                                int64_t status) {
  if (!enabled()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mtx_);
  std::ostringstream os;
  os << "blobs/" << std::setw(8) << std::setfill('0') << blob_seq_++ << ".bin";
  std::string name = os.str();
  std::ofstream blob(dir_ / name, std::ios::binary | std::ios::trunc);
  if (!blob.is_open()) {
    fprintf(stderr, "Failed to open HAL capture blob %s\n",
            (dir_ / name).string().c_str());
    return;
  }
  blob.write(reinterpret_cast<const char *>(data), size);
  EventLocked(type, addr, size, 0, value, status, name);
}

void CaptureRecorder::EventLocked(std::string_view type, uint64_t addr,
                                  uint64_t size, uint64_t alloc_tag,
                                  int64_t value, int64_t status,
                                  std::string_view blob) {
  events_ << seq_++ << '\t' << type << '\t' << Hex(addr) << '\t' << Hex(size)
          << '\t' << Hex(alloc_tag) << '\t' << value << '\t' << status << '\t'
          << blob << '\n';
}

std::string CaptureRecorder::Hex(uint64_t value) {
  std::ostringstream os;
  os << "0x" << std::hex << value;
  return os.str();
}

}  // namespace formosa::hal
