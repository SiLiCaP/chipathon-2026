/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_core: control, sequencing and datapath of the NTT chip (Spec 3).
 *
 * Instantiates the arithmetic blocks owned by others (modq_mul, 2x modq_addsub,
 * 2x modq_acc) and builds around them: the op latch / job FSM and counters
 * (ntt_ctrl), the zeta' / gamma' select with zeta_rom (ntt_twiddle), the
 * multiplier schedule (ntt_sched), the operand registers, the multiplier input
 * mux, the bypass delay line, the basemul running sums, the scale pairing
 * and the flush. See docs/schedule.md and docs/host_timing.md.
 *
 * modq_mul returns 2197 * a * b (K-RED). zeta_rom stores zeta * 2197^-1, so
 * the NTT modes are plain at the pins; basemul results carry 2197, and scale
 * (constant 304) removes it again.
 *
 * Parameters must match the blocks that are instantiated:
 *   L_MUL    modq_mul latency start -> p_valid (3: the basemul schedule starts
 *            m2 on the clock m1 comes out)
 *   PRE_LAT  REG_OUT of the Pre +/- modq_addsub (1 for timing closure; 0 works)
 *   POST_LAT REG_OUT of the Post +/- modq_addsub (0 or 1)
 */

`default_nettype none

module ntt_core #(
    parameter L_MUL    = 3,
    parameter PRE_LAT  = 1,
    parameter POST_LAT = 0
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [11:0] coeff_q,
    input  wire        valid_q,
    input  wire        sof_q,
    input  wire [1:0]  op_q,
    output reg  [11:0] r0,
    output reg  [11:0] r1,
    output wire        pair_valid,
    output wire        flush
);

  localparam [1:0] OP_FWD = 2'b00, OP_INV = 2'b01, OP_BM = 2'b10;  // 2'b11 = scale
  // Result kinds from ntt_sched (K_M1 = 3'd2 is never routed: m1 goes
  // straight from p_out back into the multiplier)
  localparam [2:0] K_BF = 3'd0, K_SC = 3'd1, K_M2 = 3'd3,
                   K_M3 = 3'd4, K_M4 = 3'd5, K_M5 = 3'd6;
  // Scale constant 304 = 3303 * 2197^-2 mod 3329: the multiplier adds 2197 and,
  // in the full flow, the input already carries 2197 from basemul -> net 3303.
  localparam [11:0] C_SCALE = 12'd304;

  // ---------------------------------------------------------------- control
  wire [1:0]  op, op_now;
  wire        busy, take, job_start, cancel;
  wire [10:0] idx;

  ntt_ctrl u_ctrl (
      .clk       (clk),
      .rst_n     (rst_n),
      .valid_q   (valid_q),
      .sof_q     (sof_q),
      .op_q      (op_q),
      .pair_valid(pair_valid),
      .op        (op),
      .op_now    (op_now),
      .busy      (busy),
      .take      (take),
      .idx       (idx),
      .job_start (job_start),
      .cancel    (cancel)
  );

  assign flush = cancel;

  // Position of this input in its unit: b/a, g1/f1/g0/f0, or c/c'.
  wire [1:0] slot = (op_now == OP_BM) ? idx[1:0] : {1'b0, idx[0]};

  wire       mul_start, res_v;
  wire [2:0] mul_kind, res_kind;
  wire [9:0] mul_sel;

  ntt_sched #(.L_MUL(L_MUL), .PRE_LAT(PRE_LAT)) u_sched (
      .clk      (clk),
      .rst_n    (rst_n),
      .job_start(job_start),
      .cancel   (cancel),
      .op       (op),
      .op_now   (op_now),
      .take     (take),
      .slot     (slot),
      .start    (mul_start),
      .kind     (mul_kind),
      .sel      (mul_sel),
      .res_v    (res_v),
      .res_kind (res_kind)
  );

  // ------------------------------------------------- operand registers, ROM
  // R0..R3 hold the unit's inputs: b a / g1 f1 g0 f0 / c c'. zeta_r holds the
  // unit's zeta' (NTT) or ROM' entry for gamma' (basemul), looked up when its
  // first input is taken; gamma_r = gamma' (negated for odd pieces), one clock
  // later, ready long before m2 starts at offset 5. Timing closure: the lookup
  // runs from registered mode flags, and the negation is off the lookup path.
  reg  [11:0] R0, R1, R2, R3, zeta_r, gamma_r;
  reg         neg_r, tw_inv_r, tw_bm_r;
  wire [11:0] zeta_lu;
  wire        neg_lu;
  wire        tw_inv = job_start ? (op_q == OP_INV) : tw_inv_r;
  wire        tw_bm  = job_start ? (op_q == OP_BM)  : tw_bm_r;

  ntt_twiddle u_twiddle (
      .inv (tw_inv),
      .bm  (tw_bm),
      .idx (idx),
      .zeta(zeta_lu),
      .neg (neg_lu)
  );

  always @(posedge clk) begin
    if (!rst_n) begin
      R0 <= 12'd0; R1 <= 12'd0; R2 <= 12'd0; R3 <= 12'd0;
      zeta_r <= 12'd0; neg_r <= 1'b0; gamma_r <= 12'd0;
      tw_inv_r <= 1'b0; tw_bm_r <= 1'b0;
    end else begin
      tw_inv_r <= (op_now == OP_INV);    // op of the next clock (op latches op_now)
      tw_bm_r  <= (op_now == OP_BM);
      gamma_r  <= neg_r ? 12'd3329 - zeta_r : zeta_r;   // no ROM' entry is 0
      if (take) begin
        case (slot)
          2'd0: begin R0 <= coeff_q; zeta_r <= zeta_lu; neg_r <= neg_lu; end
          2'd1: R1 <= coeff_q;
          2'd2: R2 <= coeff_q;
          default: R3 <= coeff_q;
        endcase
      end
    end
  end

  // ---------------------------------------------------- Pre +/- (inverse)
  wire [11:0] pre_sum, pre_diff;   // a + b, b - a
  modq_addsub #(.REG_OUT(PRE_LAT)) u_pre (
      .clk  (clk),
      .rst_n(rst_n),
      .x    (R0),
      .y    (R1),
      .sum  (pre_sum),
      .diff (pre_diff)
  );

  // ------------------------------------------------------------ multiplier
  // What is coming out of the multiplier this clock (from the tag line)
  wire is_m2 = res_v && res_kind == K_M2;
  wire is_m3 = res_v && res_kind == K_M3;
  wire is_m4 = res_v && res_kind == K_M4;
  wire is_m5 = res_v && res_kind == K_M5;
  wire is_sc = res_v && res_kind == K_SC;
  wire is_bf = res_v && res_kind == K_BF;

  // Operand mux: one AND-OR level, selects straight from flip-flops (ntt_sched).
  // m2 = m1 * gamma takes m1 from p_out on the clock it comes out.
  reg  [11:0] hold;                // scale: first result of the pair
  wire [11:0] p_out;
  wire        p_valid, mul_ready;
  wire [11:0] mul_a = ({12{mul_sel[0]}} & R0)    | ({12{mul_sel[1]}} & R1) |
                      ({12{mul_sel[2]}} & p_out) | ({12{mul_sel[3]}} & R3) |
                      ({12{mul_sel[4]}} & pre_diff);
  wire [11:0] mul_b = ({12{mul_sel[5]}} & C_SCALE) | ({12{mul_sel[6]}} & R0) |
                      ({12{mul_sel[7]}} & zeta_r)  | ({12{mul_sel[8]}} & R2) |
                      ({12{mul_sel[9]}} & gamma_r);

  modq_mul u_mul (
      .clk    (clk),
      .rst_n  (rst_n),
      .start  (mul_start),
      .a_in   (mul_a),
      .b_in   (mul_b),
      .flush  (cancel),
      .p_out  (p_out),
      .p_valid(p_valid),
      .ready  (mul_ready)
  );

  // ------------------------------------------------- bypass delay line
  // Forward: a (on coeff_q at the start clock). Inverse: a + b (at the start
  // clock). Both meet the product L_MUL clocks later.
  wire [11:0] byp_in  = !busy ? 12'd0 : (op == OP_INV) ? pre_sum : coeff_q;
  wire [11:0] byp_out;
  ntt_delay #(.W(12), .N(L_MUL)) u_byp (
      .clk  (clk),
      .rst_n(rst_n),
      .clr  (1'b0),
      .d    (byp_in),
      .q    (byp_out)
  );

  // ---------------------------------------------------- Post +/- (forward)
  wire [11:0] post_sum, post_diff;   // a + t, a - t
  modq_addsub #(.REG_OUT(POST_LAT)) u_post (
      .clk  (clk),
      .rst_n(rst_n),
      .x    (byp_out),
      .y    (p_out),
      .sum  (post_sum),
      .diff (post_diff)
  );

  // ------------------------------------------------ basemul running sums
  wire [11:0] acc0, acc1;            // c0 = m2 + m3, c1 = m5 + m4
  modq_acc u_acc0 (
      .clk  (clk),
      .rst_n(rst_n),
      .ld   (is_m2),
      .add  (is_m3),
      .y    (p_out),
      .acc  (acc0)
  );
  modq_acc u_acc1 (
      .clk  (clk),
      .rst_n(rst_n),
      .ld   (is_m5),
      .add  (is_m4),
      .y    (p_out),
      .acc  (acc1)
  );

  // ------------------------------------------------ scale pairing, pairs
  reg sc_half;    // 1: hold has the first result of a scale pair
  reg pv_bm;      // c0 / c1 complete (the clock after m3, the last result)
  reg pv_fwd_d;   // forward pair, one clock late (POST_LAT = 1)

  always @(posedge clk) begin
    if (!rst_n) begin
      hold     <= 12'd0;
      sc_half  <= 1'b0;
      pv_bm    <= 1'b0;
      pv_fwd_d <= 1'b0;
    end else begin
      if (is_sc && !sc_half) hold <= p_out;
      if (job_start)  sc_half <= 1'b0;
      else if (is_sc) sc_half <= ~sc_half;
      pv_bm    <= is_m3 & ~cancel;
      pv_fwd_d <= is_bf & (op == OP_FWD) & ~cancel;
    end
  end

  wire pv_fwd = (POST_LAT != 0) ? pv_fwd_d : (is_bf && op == OP_FWD);
  wire pv_inv = is_bf && op == OP_INV;
  wire pv_sc  = is_sc && sc_half;

  assign pair_valid = (pv_fwd | pv_inv | pv_sc | pv_bm) & ~cancel;

  always @(*) begin
    case (op)
      OP_FWD:  begin r0 = post_sum; r1 = post_diff; end  // a', b'
      OP_INV:  begin r0 = byp_out;  r1 = p_out;     end  // a + b, (b - a) * zeta
      OP_BM:   begin r0 = acc0;     r1 = acc1;      end  // c0, c1
      default: begin r0 = hold;     r1 = p_out;     end  // c * 304 (x2197), two at a time
    endcase
  end

  wire _unused = &{mul_ready, p_valid, mul_kind, 1'b0};

  // --------------------------------------------- simulation assertions
`ifdef NTT_ASSERT
  // Protocol and schedule checks (Spec 3, docs/schedule.md): units no closer
  // than their period, the inputs of a unit on consecutive clocks, results
  // where the schedule expects them, pairs at least 3 clocks apart.
  integer assert_errors = 0;
  integer since_pair    = 99;   // clocks since the last pair_valid
  integer since_unit    = 99;   // clocks since the last unit's first input
  integer since_take    = 99;   // clocks since the last input taken
  integer period;

  always @(*) period = (op_now == OP_BM) ? 5 : 3;

  always @(posedge clk) begin
    if (rst_n) begin
      if (pair_valid && since_pair < 3) begin
        $display("%t ntt_core ASSERT: pair_valid %0d clocks after the previous one", $time, since_pair);
        assert_errors = assert_errors + 1;
      end
      if (p_valid !== res_v) begin
        $display("%t ntt_core ASSERT: p_valid=%b but the schedule expects %b (L_MUL wrong?)", $time, p_valid, res_v);
        assert_errors = assert_errors + 1;
      end
      if (pair_valid && (r0 >= 12'd3329 || r1 >= 12'd3329)) begin
        $display("%t ntt_core ASSERT: result out of range r0=%0d r1=%0d", $time, r0, r1);
        assert_errors = assert_errors + 1;
      end
      if (take && coeff_q >= 12'd3329) begin
        $display("%t ntt_core ASSERT: input coefficient %0d >= 3329", $time, coeff_q);
        assert_errors = assert_errors + 1;
      end
      if (take && !job_start && slot == 2'd0 && since_unit < period) begin
        $display("%t ntt_core ASSERT: unit started %0d clocks after the previous one (period %0d)", $time, since_unit, period);
        assert_errors = assert_errors + 1;
      end
      if (take && !job_start && slot != 2'd0 && since_take != 1) begin
        $display("%t ntt_core ASSERT: input %0d of a unit came %0d clocks after the previous input (need 1)", $time, slot, since_take);
        assert_errors = assert_errors + 1;
      end
      since_pair = pair_valid ? 1 : (since_pair < 99 ? since_pair + 1 : 99);
      since_unit = (take && slot == 2'd0) ? 1 : (since_unit < 99 ? since_unit + 1 : 99);
      since_take = take ? 1 : (since_take < 99 ? since_take + 1 : 99);
    end
  end

  initial begin
    if (L_MUL != 3) begin
      $display("ntt_core ASSERT: L_MUL = %0d, the basemul schedule needs 3", L_MUL);
      assert_errors = assert_errors + 1;
    end
  end
`endif

endmodule
