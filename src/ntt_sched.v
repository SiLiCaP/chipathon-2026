/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_sched: when the multiplier starts, what for, and which result is coming
 * back out of it (Spec 3, per-mode schedules). Offsets are clocks after the
 * unit's first coefficient is taken (the clock it is on coeff_q).
 *
 *   forward  unit b,a     start b*zeta at offset 1             kind BF
 *   inverse  unit b,a     start (b-a)*zeta at offset 2+PRE_LAT  kind BF
 *   scale    unit c       start c*304 at offset 1               kind SC
 *   basemul  g1,f1,g0,f0  piece timer starts the five passes:
 *                           offset  2  f1*g1     = m1  (M1)
 *                           offset  5  f0*g0     = m3  (M3)
 *                           offset  8  m1*gamma  = m2  (M2, loop-back)
 *                           offset 11  f0*g1     = m4  (M4)
 *                           offset 14  f1*g0     = m5  (M5)
 *   m3 runs between m1 and m2 so m1 has L_MUL <= 6 clocks to come back (at
 *   L_MUL = 6 it arrives on the m2 start clock and ntt_core forwards p_out).
 *
 * The start kind travels down a tag line of length L_MUL beside the multiplier,
 * so res_v / res_kind line up with p_valid / p_out.
 */

`default_nettype none

module ntt_sched #(
    parameter L_MUL   = 3,   // modq_mul latency, start -> p_valid
    parameter PRE_LAT = 0    // Pre +/- modq_addsub latency (REG_OUT)
) (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       job_start,
    input  wire       cancel,
    input  wire [1:0] op,        // latched op
    input  wire [1:0] op_now,    // op of this clock's input
    input  wire       take,
    input  wire [1:0] slot,      // position of the input in its unit
    output wire       start,     // start the multiplier this clock
    output reg  [2:0] kind,      // what this start is
    output wire       res_v,     // a result is on p_out this clock
    output wire [2:0] res_kind,  // what it is
    output reg  [3:0] bm_t       // basemul piece timer (offset), 0 = idle
);

  localparam [1:0] OP_FWD = 2'b00, OP_INV = 2'b01, OP_BM = 2'b10, OP_SC = 2'b11;
  localparam [2:0] K_BF = 3'd0, K_SC = 3'd1, K_M1 = 3'd2, K_M2 = 3'd3,
                   K_M3 = 3'd4, K_M4 = 3'd5, K_M5 = 3'd6;

  // Inputs taken in the last two clocks (cleared at a job start, so nothing
  // from a cancelled job can start the multiplier under the new op).
  reg       take_d1, take_d2;
  reg [1:0] slot_d1, slot_d2;
  always @(posedge clk) begin
    if (!rst_n) begin
      take_d1 <= 1'b0; take_d2 <= 1'b0;
      slot_d1 <= 2'd0; slot_d2 <= 2'd0;
    end else begin
      take_d1 <= take;
      slot_d1 <= slot;
      take_d2 <= take_d1 & ~job_start;
      slot_d2 <= slot_d1;
    end
  end

  // Basemul piece timer: 1 on the clock after g1, counts to 14, then idle.
  always @(posedge clk) begin
    if (!rst_n || (job_start && op_now != OP_BM)) bm_t <= 4'd0;
    else if (take && op_now == OP_BM && slot == 2'd0) bm_t <= 4'd1;
    else if (bm_t == 4'd14) bm_t <= 4'd0;
    else if (bm_t != 4'd0) bm_t <= bm_t + 4'd1;
  end

  wire inv_go = (PRE_LAT != 0) ? (take_d2 && slot_d2 == 2'd1) : (take_d1 && slot_d1 == 2'd1);

  reg go;
  always @(*) begin
    go   = 1'b0;
    kind = K_BF;
    case (op)
      OP_FWD: go = take_d1 && slot_d1 == 2'd0;
      OP_INV: go = inv_go;
      OP_SC:  begin go = take_d1; kind = K_SC; end
      default: begin  // OP_BM
        case (bm_t)
          4'd2:    begin go = 1'b1; kind = K_M1; end
          4'd5:    begin go = 1'b1; kind = K_M3; end
          4'd8:    begin go = 1'b1; kind = K_M2; end
          4'd11:   begin go = 1'b1; kind = K_M4; end
          4'd14:   begin go = 1'b1; kind = K_M5; end
          default: begin go = 1'b0; kind = K_M1; end
        endcase
      end
    endcase
  end

  // Nothing starts on the cancel clock (that clock flushes the multiplier).
  assign start = go & ~cancel;

  wire [3:0] tag_q;
  ntt_delay #(.W(4), .N(L_MUL)) u_tag (
      .clk  (clk),
      .rst_n(rst_n),
      .clr  (cancel),
      .d    ({start, kind}),
      .q    (tag_q)
  );
  assign res_v    = tag_q[3];
  assign res_kind = tag_q[2:0];

endmodule
