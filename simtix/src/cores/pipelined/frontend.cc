// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/frontend.h"

#include <algorithm>

#include "cores/decode.h"
#include "cores/pipelined/konata_labels.h"

#define WITH_TRACER(code)            \
  do {                               \
    if (auto *t = core_->tracer()) { \
      t->code;                       \
    }                                \
  } while (0)

namespace simtix::pipelined {

void Frontend::PCGen() {
  // Skip PC gen if we don't have enough capacity for the new fetch requests
  if (fetch_filter_.size() >= num_fetch_filter_entries_ ||
      free_fetch_entries_.empty() || pc_gen_q_.num_free() == 0) {
    goto FINALLY;
  }

  // Make sure these warps are active
  starving_warps_ &= core_->active_warps().val();
  issuing_warps_ &= core_->active_warps().val();

  // Fetch request arbitration
  for (auto *warp_mask : eligible_warps_) {
    for (uint32_t i = 0; i < num_warps_; ++i) {
      uint32_t wid = (prioritized_ + i) % num_warps_;
      if ((*warp_mask)[wid]) {
        GenerateFetchRequest(wid);
        prioritized_ = (wid + 1) % num_warps_;

        if (warp_mask == &issuing_warps_) {
          core_->stats()->fetch_due_to_issuing++;
        } else if (warp_mask == &starving_warps_) {
          core_->stats()->fetch_due_to_starving++;
        }

        goto FINALLY;
      }
    }
  }
FINALLY:
  issuing_warps_ = 0;
  for (uint32_t i = 0; i < num_warps_; ++i) {
    starving_warps_[i] = ibuffer_[i].nb_can_get() == false;
  }
}

void Frontend::Fetch() {
  // 1. Collect a ready fetch entry
  //
  // Make sure there are enough spaces for the fetched packets
  if (fetch_q_.num_free() >= fetch_width_) {
    // Make sure there is a ready fetch entry
    auto *entry = GetReadyFetchEntry();
    if (entry) {
      // Move all the packets to the fetch Q and recycle the fetch entry
      for (Packet *p : entry->packets()) {
        fetch_q_.write(p);
        WITH_TRACER(StartStage(p, 0, "F2"));
      }
      FreeFetchEntry(entry);
    }
  }

  // 2. Issue a new fetch request
  //
  // Make sure there is a fetch request and imem_port_ has a free slot for it
  if (pc_gen_q_.num_available() > 0 && imem_port_.req_port->num_free() > 0) {
    FetchEntry *entry = pc_gen_q_.read();
    imem_port_.req_port->nb_write(entry->payload());
  }
}

void Frontend::Decode() {
  // Run the decoding process for the maximum allowed iterations
  for (uint32_t i = 0; i < decode_width_; ++i) {
    if (fetch_q_.num_available() == 0 || !decode_q_.nb_can_put()) {
      break;
    }
    Packet *p = fetch_q_.read();
    p->instr = simtix::Decode(p->iword);
    decode_q_.put(p);
    WITH_TRACER(StartStage(p, 0, "D"));
    WITH_TRACER(
        AddMnemonic(p, fmt::format("{:#x} {}", p->wpc, Mnemonic(p->instr))));
  }
}

void Frontend::Broadcast() {
  // IWIS-style instruction broadcasting to the ibuffer
  for (uint32_t i = 0; i < decode_width_; ++i) {
    if (!decode_q_.nb_can_get()) {
      return;
    }

    Packet *packet = nullptr;
    decode_q_.nb_peek(packet);

    // If the warp is active, make sure the packet's warp's ibuffer has a free
    // slot for it so that it won't be lost
    if (core_->active_warps().val()[packet->wid] &&
        !ibuffer_[packet->wid].nb_can_put()) {
      core_->stats()->ibuffer_full++;
      return;
    }

    // Pop the packet and insert it into ibuffer
    bool accepted = false;
    bool can_share = false;
    decode_q_.get();
    core_->stats()->instr_filled++;

    // Retire one word from the in-flight fetch group; erase only when the
    // whole group has been broadcast so mid-group catch-up warps stay filtered.
    const uint64_t fg_addr = ToFetchGroupAddress(packet->wpc);
    auto it = FindFetchFilterEntry(fg_addr);
    if (it != fetch_filter_.end() && --it->remaining == 0) {
      fetch_filter_.erase(it);
    }

    for (uint32_t i = 0; i < num_warps_; ++i) {
      // If IWIS is not enabled, only the packet's warp can accept the packet
      if (!enable_iwis_ && i != packet->wid) {
        can_share |= next_pc_[i] == packet->wpc;
        core_->stats()->instr_shared += can_share;
        continue;
      }

      // Make sure the warp is active and its ibuffer is capacious and next PC
      // is exactly the packet's wpc
      if (!core_->active_warps().val()[i] || !ibuffer_[i].nb_can_put() ||
          next_pc_[i] != packet->wpc)
        continue;

      // Allocate another packet for the sharing warps
      Packet *p = packet;
      if (i != packet->wid) {
        p = core_->AllocatePacket();
        p->wid = i;
        p->wpc = packet->wpc;
        p->iword = packet->iword;
        p->instr = packet->instr;
        p->is_shared = true;
        WITH_TRACER(Declare(p));
        WITH_TRACER(AddMnemonic(
            p, fmt::format("{:#x} {}", p->wpc, Mnemonic(p->instr))));
        WITH_TRACER(
            AddComment(p, fmt::format("Shared from {}\\n", packet->unique_id)));
        core_->stats()->instr_shared++;
        can_share = true;
      }

      ibuffer_[i].put(p);
      next_pc_[i] += 4;
      fetch_pc_[i] = std::max(fetch_pc_[i], next_pc_[i]);
      WITH_TRACER(StartStage(p, 0, "Sc"));
      TryLinkRedirectTarget(i, p);
      accepted |= i == packet->wid;  // True if the packet's warp accepts it
    }

    if (!accepted) {
      const uint32_t wid = packet->wid;
      const bool inactive = !core_->active_warps().val()[wid];
      const FlushReason reason =
          inactive ? FlushReason::kMisc
                   : AttributeBroadcastReject(packet->wpc, next_pc_[wid],
                                              last_invalidation_reason_[wid],
                                              last_invalidation_wpc_[wid]);

      if (inactive || reason == FlushReason::kUnknown ||
          reason == FlushReason::kDuplicateInst) {
        WITH_TRACER(SpeculativeDiscard(
            packet, KonataSpecDiscardLabel(reason, packet->wpc)));
      } else {
        WITH_TRACER(ArchitecturalFlush(
            packet, KonataFlushLabel(reason, last_invalidation_wpc_[wid])));
      }
      core_->stats()->RecordInvalidation(reason);
      core_->FreePacket(packet);
    }
    core_->stats()->can_share_instr += can_share;
  }
}

void Frontend::CollectReadyFetchEntries() {
  bool ready = imem_port_.resp_port->num_available() > 0;
  if (!ready) {
    next_trigger(imem_port_.resp_port->data_written_event());
    return;
  }

  if (clock->posedge()) {
    auto *payload = imem_port_.resp_port->read();

    // Get the corresponding fetch entry from the payload via TLM extension
    FetchEntryExtension *fe_ext = nullptr;
    payload->get_extension(fe_ext);
    assert(fe_ext);

    // Notify the fill of the corresponding entry, setting ready
    FetchEntry *entry = fe_ext->entry;
    entry->NotifyFill();
  }

  next_trigger(clock->posedge_event());
}

void Frontend::UpdateFetchPC() {
  uint32_t wid = update_fetch_pc_.wid();
  uint64_t fg_addr = update_fetch_pc_.fg_addr();

  // The address for the next fetch group following this one
  uint64_t next_fg_addr = fg_addr + fetch_width_ * 4;
  for (uint32_t i = 0; i < num_warps_; ++i) {
    // If IWIS is not enable, the fetch PC update is only valid for the
    // fetching warp
    if (!enable_iwis_ && i != wid) {
      continue;
    }
    // If the addresses in the fetch group happen to be the next fetch
    // addresses for this warp, update its fetch PC to prevent duplicated
    // fetches
    if (fg_addr <= fetch_pc_[i] && fetch_pc_[i] < next_fg_addr) {
      // The maximum allowed fetch PC bounded by the capacity
      int num_free = ibuffer_[i].size() - ibuffer_[i].used();
      uint64_t max_fetch_addr = next_pc_[i] + num_free * 4;

      if (i == wid) {
        // If this is the fetching warp, it must not drop the fetched
        // instructions so we don't care about the capacity of ibuffer
        fetch_pc_[i] = next_fg_addr;
      } else {
        // For the shared warp, the fetch PC for it is either updated with the
        // next fetch group address or the maximum allowed fetch PC to prevent
        // overflow since the broadcast stage won't share the instruction as
        // long as the buffer is full
        fetch_pc_[i] = std::min(next_fg_addr, max_fetch_addr);
      }
    }
  }
}

void Frontend::NoteFlush(uint32_t wid, FlushReason reason, uint64_t wpc,
                         uint64_t producer_id) {
  pending_flush_reason_[wid] = reason;
  pending_flush_wpc_[wid] = wpc;
  if (producer_id != 0 && wpc != 0) {
    pending_edge_valid_[wid] = true;
    pending_edge_producer_id_[wid] = producer_id;
    pending_edge_target_wpc_[wid] = wpc;
    Packet *head = nullptr;
    if (ibuffer_[wid].nb_peek(head) && head->wpc == wpc) {
      TryLinkRedirectTarget(wid, head);
    }
  } else {
    pending_edge_valid_[wid] = false;
  }
}

void Frontend::ExecutePendingFlushes() {
  for (uint32_t i = 0; i < num_warps_; ++i) {
    if (pending_flush_mask_[i]) {
      const FlushReason reason = pending_flush_reason_[i];
      const uint64_t redirect_wpc = pending_flush_wpc_[i];
      last_invalidation_reason_[i] = reason;
      last_invalidation_wpc_[i] = redirect_wpc;
      const std::string label = KonataFlushLabel(reason, redirect_wpc);
      Packet *packet = nullptr;
      uint64_t flushed = 0;
      while (ibuffer_[i].nb_get(packet)) {
        WITH_TRACER(ArchitecturalFlush(packet, label));
        if (!packet->is_shared) {
          ++flushed;
        }
        core_->FreePacket(packet);
      }

      if (flushed > 0) {
        core_->stats()->RecordInvalidation(reason, flushed);
      }

      pending_flush_mask_[i] = 0;
    }
  }
}

void Frontend::TryLinkRedirectTarget(uint32_t wid, Packet *consumer) {
  if (!pending_edge_valid_[wid]) {
    return;
  }
  if (consumer->wpc != pending_edge_target_wpc_[wid]) {
    return;
  }
  WITH_TRACER(Wake(consumer, pending_edge_producer_id_[wid]));
  pending_edge_valid_[wid] = false;
}

std::vector<Frontend::FetchFilterEntry>::iterator
Frontend::FindFetchFilterEntry(uint64_t fg_addr) {
  return std::find_if(fetch_filter_.begin(), fetch_filter_.end(),
                      [fg_addr](const FetchFilterEntry &e) {
                        return e.fg_addr == fg_addr;
                      });
}

void Frontend::GenerateFetchRequest(uint32_t wid) {
  // Filter the request if IWIS is enabled and the FG addr already exists
  uint64_t fg_addr = ToFetchGroupAddress(fetch_pc_[wid]);
  if (enable_iwis_) {
    if (FindFetchFilterEntry(fg_addr) != fetch_filter_.end()) {
      core_->stats()->num_fetches_filtered++;
      return;
    }
  }

  // Fetch filter missed or IWIS is not enabled
  fetch_filter_.push_back({fg_addr, fetch_width_});
  update_fetch_pc_.notify(wid, fg_addr);

  auto *entry = AllocateFetchEntry(fg_addr);
  for (uint32_t i = 0; i < fetch_width_; ++i) {
    Packet *p = core_->AllocatePacket();
    p->wid = wid;
    p->wpc = fg_addr + i * 4;
    entry->AddPacket(p);
    WITH_TRACER(Declare(p));
    WITH_TRACER(StartStage(p, 0, "F1"));
  }
  pc_gen_q_.write(entry);
}

#undef WITH_TRACER

}  // namespace simtix::pipelined
