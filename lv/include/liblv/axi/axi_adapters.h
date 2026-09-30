/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/** @file
 * Convenience umbrella include for the AXI pin-level/TLM adapter library.
 *
 * Transfer contract
 * -----------------
 * AXI -> TLM always maps one complete aligned INCR burst to one transaction.
 * AR is validated before allocation/issue; AW geometry and every WLAST are
 * validated before a write is issued. LEN is 0..255, SIZE is at most log2(bus
 * bytes), and the complete address range must fit AddrW and one 4-KiB page.
 * The payload starts at AxADDR, has (AxLEN + 1) * (1 << AxSIZE) data bytes,
 * and streaming_width equals data_length. Read byte enables are null. Writes
 * carry one byte enable per data byte (0xff/0x00), reconstructed from WSTRB;
 * even an all-disabled write retains its transaction boundary.
 *
 * Downstream targets must support that complete length and write byte-enable
 * pattern, or reject them before performing side effects. The bridge does not
 * break bursts into register accesses or make a permissive RAM's behavior an
 * MMIO contract. A device target owns access-size/alignment/register-boundary
 * and byte-enable restrictions. TLM address errors map to DECERR; other errors
 * map to SLVERR. A read's response status applies to every returned R beat.
 *
 * Latency and statistics are transaction based: simple.Memory increments its
 * read/write count once per accepted burst and waits its configured latency
 * once, independent of LEN. AXI address/data collection, response handshakes,
 * response gaps and backpressure add time outside that memory latency. Writes
 * reach the target only after the entire burst is assembled; reads cannot
 * emit their first beat until the full TLM response arrives. No per-beat
 * downstream latency or atomicity/exclusive-access guarantee is implied.
 *
 * TLM -> AXI retains optional single-beat splitting. With splitting, each AXI
 * beat is a separate AXI transaction while the original caller still receives
 * one TLM completion. A following AXI -> TLM bridge consequently creates one
 * downstream transaction per split beat.
 *
 * Limits and failures
 * -------------------
 * Ports use sc_signal<uint32_t> for addresses, IDs and control encodings,
 * sc_signal<uint64_t> for data, and sc_signal<bool> for handshakes/LAST.
 * AddrW/IdW are 1..32; DataW is 8, 16, 32 or 64. These template widths do not
 * create sc_uint/sc_bv ports: RTL with other pin types requires an explicit
 * binding/conversion layer. Wider addresses are not supported. RID is checked
 * on every beat; BID is checked for directly bound pins and non-null bundle
 * pins. A bundle with null BID uses an internal dummy that is never checked.
 *
 * AxLOCK/CACHE/PROT/QOS have no pins here. Extension fields are metadata only;
 * they are neither captured from AXI nor driven onto AXI. Exclusive accesses,
 * access protection and cache/QoS signaling are not implemented. Callers must
 * use ordinary accesses; EXOKAY mapping alone does not implement exclusives.
 *
 * Invalid incoming AXI geometry, WLAST, RID, RLAST or present BID terminates
 * with LV_FATAL. Malformed responses cannot be safely resynchronized without
 * a device-specific recovery protocol. Invalid TLM geometry returns a TLM
 * error before pin traffic (FIXED bursts retain their LV_FATAL behavior).
 * Within each read/write channel, same-ID response order is mandatory;
 * different IDs can complete independently. FIFO capacities are buffering,
 * not public outstanding limits. serialize_transactions admits one AXI -> TLM
 * transaction across both channels until its AXI response completes.
 *
 * Logging uses LV_DEBUG for requests and LV_TRACE for beats. The optional
 * LV_AXI_READ_BEAT_GAP, LV_AXI_READ_BURST_GAP and LV_AXI_WRITE_RESP_GAP timing
 * controls are sampled once at construction and retain clock/reset behavior.
 */

#include <liblv/axi/axi_helpers.h>
#include <liblv/axi/axi_initiator_adapter.h>
#include <liblv/axi/axi_signals.h>
#include <liblv/axi/axi_target_adapter.h>
#include <liblv/axi/axi_types.h>
