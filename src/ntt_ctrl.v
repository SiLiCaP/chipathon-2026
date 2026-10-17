/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_ctrl: op latch, job lifecycle and the job counters (Spec 3).
 *
 *   idle    -> wait for valid_q & sof_q
 *   start   -> latch op_q, clear the counters, take this coefficient as input 0
 *   running -> take one coefficient per clock with valid_q, up to the mode's
 *              input count; valid_q low changes nothing; extra inputs ignored
 *   end     -> after the mode's last result pair, back to idle
 *   cancel  -> sof while running: restart, one-clock cancel pulse (flush)
 *
 * idx is the index of the coefficient taken this clock (0 on the sof clock).
 * Every other counter in the core is a slice of idx:
 *   forward / inverse : idx[0] = b/a, idx[7:1] = butterfly in layer, idx[10:8] = layer
 *   basemul           : idx[1:0] = g1/f1/g0/f0, idx[8:2] = piece
 *   scale             : idx[7:0] = coefficient
 */

`default_nettype none

module ntt_ctrl (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        valid_q,
    input  wire        sof_q,
    input  wire [1:0]  op_q,
    input  wire        pair_valid,  // result pairs leaving the core
    output reg  [1:0]  op,          // latched op of the running job
    output wire [1:0]  op_now,      // op that applies to this clock's input
    output reg         busy,        // a job is running (inputs or results pending)
    output wire        take,        // this clock's coefficient is a job input
    output wire [10:0] idx,         // its index in the job
    output wire        job_start,   // a job starts this clock
    output wire        cancel       // a running job is cancelled this clock
);

  localparam [1:0] OP_FWD = 2'b00, OP_INV = 2'b01, OP_BM = 2'b10;  // 2'b11 = scale

  reg [10:0] in_cnt;    // inputs taken so far
  reg [9:0]  pair_cnt;  // pairs emitted so far

  reg [10:0] n_in;
  reg [9:0]  n_pairs;
  always @(*) begin
    case (op)
      OP_FWD, OP_INV: begin n_in = 11'd1792; n_pairs = 10'd896; end
      OP_BM:          begin n_in = 11'd512;  n_pairs = 10'd128; end
      default:        begin n_in = 11'd256;  n_pairs = 10'd128; end  // OP_SC
    endcase
  end

  assign job_start = valid_q & sof_q;
  assign cancel    = job_start & busy;
  assign take      = job_start | (busy & valid_q & (in_cnt < n_in));
  assign idx       = job_start ? 11'd0 : in_cnt;
  assign op_now    = job_start ? op_q : op;

  always @(posedge clk) begin
    if (!rst_n) begin
      op       <= OP_FWD;
      busy     <= 1'b0;
      in_cnt   <= 11'd0;
      pair_cnt <= 10'd0;
    end else if (job_start) begin
      op       <= op_q;
      busy     <= 1'b1;
      in_cnt   <= 11'd1;
      pair_cnt <= 10'd0;
    end else begin
      if (take) in_cnt <= in_cnt + 11'd1;
      if (pair_valid) begin
        pair_cnt <= pair_cnt + 10'd1;
        if (pair_cnt == n_pairs - 10'd1) busy <= 1'b0;
      end
    end
  end

endmodule
