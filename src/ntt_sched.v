/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_sched: when the multiplier starts, what for, which operands it takes,
 * and which result is coming back out of it (Spec 3, per-mode schedules).
 * Offsets are clocks after the unit's first coefficient is taken (the clock
 * it is on coeff_q).
 *
 *   forward  unit b,a     start b*zeta at offset 1               kind BF
 *   inverse  unit b,a     start (b-a)*zeta at offset 2+PRE_LAT   kind BF
 *   scale    unit c,c'    start c*304 at 1, c'*304 at 2          kind SC
 *   basemul  g1,f1,g0,f0  one start per clock (full rate):
 *                           offset 2  f1*g1    = m1  (M1)
 *                           offset 3  f1*g0    = m5  (M5)
 *                           offset 4  f0*g1    = m4  (M4)
 *                           offset 5  m1*gamma = m2  (M2, m1 straight from p_out)
 *                           offset 6  f0*g0    = m3  (M3)
 *   m2 starts on the clock m1 comes out, so basemul needs L_MUL = 3. The next
 *   piece may start at offset 5; its starts (7..11) never overlap these.
 *
 * The operand selects (sel) are registered and one-hot: they are decoded one
 * clock early from the next clock's op and schedule position, so the
 * multiplier's input mux is a single AND-OR level driven by flip-flops
 * (Spec 3, Timing closure).
 *
 * The start kind travels down a tag line of length L_MUL beside the multiplier,
 * so res_v / res_kind line up with p_valid / p_out.
 */

`default_nettype none

module ntt_sched #(
    parameter L_MUL   = 3,   // modq_mul latency, start -> p_valid
    parameter PRE_LAT = 1    // Pre +/- modq_addsub latency (REG_OUT)
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
    output reg  [9:0] sel,       // operand selects for this clock (see SEL_*)
    output wire       res_v,     // a result is on p_out this clock
    output wire [2:0] res_kind   // what it is
);

  localparam [1:0] OP_FWD = 2'b00, OP_INV = 2'b01, OP_BM = 2'b10, OP_SC = 2'b11;
  localparam [2:0] K_BF = 3'd0, K_SC = 3'd1, K_M1 = 3'd2, K_M2 = 3'd3,
                   K_M3 = 3'd4, K_M4 = 3'd5, K_M5 = 3'd6;
  // One-hot operand selects: a in sel[4:0], b in sel[9:5]
  //   a: 0 R0, 1 R1, 2 p_out, 3 R3, 4 pre_diff
  //   b: 5 304, 6 R0, 7 zeta_r, 8 R2, 9 gamma_r
  localparam [9:0] SEL_BF_FWD = 10'b00_1000_0001,   // b * zeta
                   SEL_BF_INV = 10'b00_1001_0000,   // (b - a) * zeta
                   SEL_SC_C0  = 10'b00_0010_0001,   // c * 304
                   SEL_SC_C1  = 10'b00_0010_0010,   // c' * 304
                   SEL_M1     = 10'b00_0100_0010,   // f1 * g1
                   SEL_M5     = 10'b01_0000_0010,   // f1 * g0
                   SEL_M4     = 10'b00_0100_1000,   // f0 * g1
                   SEL_M2     = 10'b10_0000_0100,   // m1 * gamma
                   SEL_M3     = 10'b01_0000_1000;   // f0 * g0

  // Inputs taken in the last two clocks, and basemul pieces started (g1
  // taken) 1..6 clocks ago. Cleared at a job start, so nothing from a
  // cancelled job can start the multiplier under the new op.
  reg        take_d1, take_d2;
  reg  [1:0] slot_d1, slot_d2;
  reg  [6:1] g1_d;
  wire       g1_now = take && op_now == OP_BM && slot == 2'd0;
  wire [6:1] g1_n   = {job_start ? 5'd0 : g1_d[5:1], g1_now};

  always @(posedge clk) begin
    if (!rst_n) begin
      take_d1 <= 1'b0; take_d2 <= 1'b0;
      slot_d1 <= 2'd0; slot_d2 <= 2'd0;
      g1_d    <= 6'd0;
    end else begin
      take_d1 <= take;
      slot_d1 <= slot;
      take_d2 <= take_d1 & ~job_start;
      slot_d2 <= slot_d1;
      g1_d    <= g1_n;
    end
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
        go = |g1_d[6:2];
        if      (g1_d[2]) kind = K_M1;
        else if (g1_d[3]) kind = K_M5;
        else if (g1_d[4]) kind = K_M4;
        else if (g1_d[5]) kind = K_M2;
        else              kind = K_M3;
      end
    endcase
  end

  // Nothing starts on the cancel clock (that clock flushes the multiplier).
  assign start = go & ~cancel;

  // Next clock's operand selects. The next clock runs op_now; its scale start
  // takes the coefficient taken now, and its basemul start is set by g1_n.
  reg [9:0] sel_n;
  always @(*) begin
    case (op_now)
      OP_FWD: sel_n = SEL_BF_FWD;
      OP_INV: sel_n = SEL_BF_INV;
      OP_SC:  sel_n = (slot == 2'd1) ? SEL_SC_C1 : SEL_SC_C0;
      default:
        if      (g1_n[2]) sel_n = SEL_M1;
        else if (g1_n[3]) sel_n = SEL_M5;
        else if (g1_n[4]) sel_n = SEL_M4;
        else if (g1_n[5]) sel_n = SEL_M2;
        else              sel_n = SEL_M3;
    endcase
  end

  always @(posedge clk) begin
    if (!rst_n) sel <= 10'd0;
    else        sel <= sel_n;
  end

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
