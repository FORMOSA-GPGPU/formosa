// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

module vclint #(
    parameter int NUM_CORES = 1
) (
    input  logic                  clk,
    input  logic                  mtime_tick,
    input  logic                  rst_n,
    input  logic [15:0]           addr,
    input  logic [31:0]           wdata,
    input  logic                  we,
    input  logic                  re,
    output logic [31:0]           rdata,
    output logic                  err_addr,
    output logic [NUM_CORES-1:0]  timer_irq,
    output logic [NUM_CORES-1:0]  msip_irq
);
  logic [63:0] mtime_q;
  logic [63:0] mtimecmp_q[NUM_CORES];
  logic        msip_q[NUM_CORES];

  logic        op;
  logic        msip_sel, cmp_sel, mtime_sel;
  int unsigned core_id;
  logic        core_err, range_err;
  logic        word_hi;

  assign op = we | re;
  assign msip_sel = (addr < 16'h4000);
  assign mtime_sel = (addr >= 16'h4000) && (addr < 16'hC000) && (addr >= 16'hBFF8);
  assign cmp_sel = (addr >= 16'h4000) && (addr < 16'hC000) && (addr < 16'hBFF8);
  assign range_err = op && !(msip_sel || mtime_sel || cmp_sel);
  assign core_id = msip_sel ? {18'd0, addr[15:2]}
                                : (({16'd0, addr} - 32'h4000) >> 3);
  assign core_err = op && (msip_sel || cmp_sel) && (core_id >= NUM_CORES);
  assign err_addr = range_err || core_err;
  assign word_hi = cmp_sel ? ((({16'd0, addr} - 32'h4000 - (core_id << 3)) >> 2) == 1)
                           : ((({16'd0, addr} - 32'hBFF8) >> 2) == 1);

  always_comb begin
    rdata = 32'b0;
    if (re && !err_addr) begin
      if (msip_sel) begin
        rdata = {31'b0, msip_q[core_id]};
      end else if (mtime_sel) begin
        rdata = word_hi ? mtime_q[63:32] : mtime_q[31:0];
      end else if (cmp_sel) begin
        rdata = word_hi ? mtimecmp_q[core_id][63:32] : mtimecmp_q[core_id][31:0];
      end
    end
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      mtime_q <= 64'b0;
      for (int i = 0; i < NUM_CORES; i++) begin
        mtimecmp_q[i] <= 64'hFFFF_FFFF_FFFF_FFFF;
        msip_q[i]     <= 1'b0;
      end
    end else begin
      mtime_q <= mtime_q + (mtime_tick ? 64'd1 : 64'd0);
      if (we && !err_addr) begin
        if (msip_sel) begin
          msip_q[core_id] <= wdata[0];
        end else if (mtime_sel) begin
          if (word_hi) begin
            mtime_q[63:32] <= wdata;
          end else begin
            mtime_q[31:0]  <= wdata;
          end
        end else if (cmp_sel) begin
          if (word_hi) begin
            mtimecmp_q[core_id][63:32] <= wdata;
          end else begin
            mtimecmp_q[core_id][31:0]  <= wdata;
          end
        end
      end
    end
  end

  always_comb begin
    for (int i = 0; i < NUM_CORES; i++) begin
      timer_irq[i] = (mtime_q >= mtimecmp_q[i]);
      msip_irq[i]  = msip_q[i];
    end
  end
endmodule
