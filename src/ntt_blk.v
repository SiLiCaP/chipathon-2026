/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_blk: the block engine (NTT cache). It keeps 16 coefficients on chip for
 * 3 or 4 NTT layers, so a whole NTT is 2 passes of 16 blocks instead of 7
 * layers through the pins. Shares the multiplier, zeta_rom, the bypass line
 * and the Post +/- adder with ntt_core (they are idle in block mode).
 * See docs/block_mode.md.
 *
 * A block: one header word (on the pins: valid, coefficient bits 11:8 = 0xE,
 * op = pass, bits 3:0 = block index j), then 16 coefficients on the next 16
 * clocks. Passes (layers given by len, Algorithms 9 and 10 of FIPS 203):
 *   0 forward A  len 128, 64, 32, 16   entry e = f[j + 16e]   4 layers
 *   1 forward B  len 8, 4, 2           entry e = f[16j + e]   3 layers
 *   2 inverse A  len 2, 4, 8           entry e = f[16j + e]   3 layers
 *   3 inverse B  len 16, 32, 64, 128   entry e = f[j + 16e]   4 layers
 *
 * Storage: L (15 x 12, a shift register the pins fill), C (16 x 12, the
 * working set, 2 reads and 2 writes per clock) and F (8 x 24, the last
 * layer's pairs waiting for the pins).
 *
 * Core clocks from the header on coeff_q (c = 0); N = 8 x layers:
 *   c = 1..16         entries 0..14 shift into L; at c = 16, C <- {coeff_q, L}
 *   c = 17 + q        butterfly q (layer q / 8, position q % 8) reads C:
 *                     S1 <- (a, b) forward or (a + b, b - a) inverse, zeta'
 *   c = 18 + q        multiplier start: S1.y x zeta'; S1.x into the bypass line
 *   c = 21 + q        product out; forward (x + t, x - t), inverse (x, t):
 *                     written back to C (readable at c = 22 + q), or to F[pos]
 *                     in the last layer
 *   c = N + 14 + 3m   pair_valid for pair m = 0..7 (last-layer position m)
 * The butterfly order in each layer (table below, from test/blk_order.py)
 * never reads an entry before it is written back: no stalls.
 *
 * Spacing: the next header may come N clocks after this one (N of this
 * block), or later. Then C, F, the multiplier and the output pins are never
 * used by two blocks at once, whatever the mix of passes.
 *
 * Smaller variants (area study, docs/ntt_cache_study.md), same results:
 *   HAS_F = 0  no F: the last layer issues one butterfly per 3 clocks and its
 *              pairs go straight to the pins. Spacing N + 14, pair_valid at
 *              c = N + 13 + 3m.
 *   HAS_L = 0  no L: the pins write C directly, so the next block may only
 *              start loading after this one's last read. Spacing N + 16
 *              (N + 30 without F).
 */

`default_nettype none

`ifndef NTT_BLK_L
`define NTT_BLK_L 1
`endif
`ifndef NTT_BLK_F
`define NTT_BLK_F 1
`endif

module ntt_blk #(
    parameter HAS_L = `NTT_BLK_L,   // 0: no L, the pins load C directly (spacing N + 16, or N + 30 without F)
    parameter HAS_F = `NTT_BLK_F    // 0: no F, the last layer runs one butterfly per 3 clocks straight
                                    //    to the pins (spacing N + 14, delay N + 15)
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        hdr,         // block header on coeff_q / op_q this clock
    input  wire [1:0]  op_q,        // header: pass
    input  wire        valid_q,     // a coefficient on coeff_q (header excluded)
    input  wire [11:0] coeff_q,
    // shared datapath (ntt_core)
    output wire [6:0]  rom_addr,
    output wire        rom_use,     // rom_addr drives zeta_rom this clock
    input  wire [11:0] rom_data,
    output reg         mul_go,      // start the multiplier this clock with mul_y x mul_z
    output reg  [11:0] mul_y,
    output reg  [11:0] mul_z,
    output reg  [11:0] byp_x,       // into the bypass line this clock (with mul_go)
    input  wire [11:0] p_out,
    input  wire [11:0] byp_out,
    input  wire [11:0] post_sum,    // byp_out + p_out
    input  wire [11:0] post_diff,   // byp_out - p_out
    output wire        res_v,       // a block product is on p_out this clock
    // results
    output wire        pair_valid,
    output wire [11:0] r0,
    output wire [11:0] r1
);

  localparam [1:0] P_FA = 2'd0, P_FB = 2'd1, P_IA = 2'd2, P_IB = 2'd3;

  // ------------------------------------------------------------- load (L)
  reg        ld_act;
  reg  [3:0] ld_cnt;
  reg  [1:0] ld_pass;
  reg  [3:0] ld_j;
  wire [15*12-1:0] Lw;                // L: entry e at Lw[e*12 +: 12], e = 0..14
  wire       ld_take = ld_act & valid_q;
  wire       xfer    = ld_take & (ld_cnt == 4'd15);   // 16th entry: C <- {coeff_q, L}

  always @(posedge clk) begin
    if (!rst_n) begin
      ld_act <= 1'b0; ld_cnt <= 4'd0; ld_pass <= 2'd0; ld_j <= 4'd0;
    end else if (hdr) begin
      ld_act <= 1'b1; ld_cnt <= 4'd0; ld_pass <= op_q; ld_j <= coeff_q[3:0];
    end else if (ld_take) begin
      ld_cnt <= ld_cnt + 4'd1;
      if (xfer) ld_act <= 1'b0;
    end
  end

  genvar ge;
  generate
    if (HAS_L) begin : g_l
      reg [15*12-1:0] lr;
      always @(posedge clk) begin
        if (ld_take) lr <= {coeff_q, lr[15*12-1:12]};
      end
      assign Lw = lr;
    end else begin : g_nol
      assign Lw = {(15*12){1'b0}};
    end
  endgenerate

  // ------------------------------------------------------------ compute
  reg        cp_act;
  reg  [4:0] q;
  reg  [1:0] cp_pass;
  reg  [3:0] cp_j;
  wire       cp_4l   = (cp_pass == P_FA) | (cp_pass == P_IB);
  wire       q_last  = q == (cp_4l ? 5'd31 : 5'd23);
  wire [1:0] layer   = q[4:3];
  wire [2:0] pos     = q[2:0];
  wire       is_last = (layer == (cp_4l ? 2'd3 : 2'd2));
  wire       cp_inv  = cp_pass[1];
  // without F the last layer issues one butterfly every 3 clocks (the pins' rate)
  reg  [1:0] ph;
  wire       issue   = cp_act & ((HAS_F != 0) | ~is_last | (ph == 2'd0));

  always @(posedge clk) begin
    if (!rst_n) begin
      cp_act <= 1'b0; q <= 5'd0; cp_pass <= 2'd0; cp_j <= 4'd0; ph <= 2'd0;
    end else if (xfer) begin
      cp_act <= 1'b1; q <= 5'd0; cp_pass <= ld_pass; cp_j <= ld_j; ph <= 2'd0;
    end else if (cp_act) begin
      if (is_last) ph <= (ph == 2'd2) ? 2'd0 : ph + 2'd1;
      if (issue) begin
        q <= q + 5'd1;
        if (q_last) cp_act <= 1'b0;
      end
    end
  end

  // butterfly order: lower entry of the pair, per pass / layer / position
  reg [3:0] lo;
  always @(*) begin
    case ({cp_pass, layer, pos})
      7'b00_00_000: lo = 4'd0;
      7'b00_00_001: lo = 4'd2;
      7'b00_00_010: lo = 4'd4;
      7'b00_00_011: lo = 4'd6;
      7'b00_00_100: lo = 4'd1;
      7'b00_00_101: lo = 4'd3;
      7'b00_00_110: lo = 4'd5;
      7'b00_00_111: lo = 4'd7;
      7'b00_01_000: lo = 4'd0;
      7'b00_01_001: lo = 4'd2;
      7'b00_01_010: lo = 4'd8;
      7'b00_01_011: lo = 4'd10;
      7'b00_01_100: lo = 4'd1;
      7'b00_01_101: lo = 4'd3;
      7'b00_01_110: lo = 4'd9;
      7'b00_01_111: lo = 4'd11;
      7'b00_10_000: lo = 4'd0;
      7'b00_10_001: lo = 4'd4;
      7'b00_10_010: lo = 4'd1;
      7'b00_10_011: lo = 4'd5;
      7'b00_10_100: lo = 4'd8;
      7'b00_10_101: lo = 4'd12;
      7'b00_10_110: lo = 4'd9;
      7'b00_10_111: lo = 4'd13;
      7'b00_11_000: lo = 4'd0;
      7'b00_11_001: lo = 4'd2;
      7'b00_11_010: lo = 4'd4;
      7'b00_11_011: lo = 4'd6;
      7'b00_11_100: lo = 4'd8;
      7'b00_11_101: lo = 4'd10;
      7'b00_11_110: lo = 4'd12;
      7'b00_11_111: lo = 4'd14;
      7'b01_00_000: lo = 4'd0;
      7'b01_00_001: lo = 4'd2;
      7'b01_00_010: lo = 4'd4;
      7'b01_00_011: lo = 4'd6;
      7'b01_00_100: lo = 4'd1;
      7'b01_00_101: lo = 4'd3;
      7'b01_00_110: lo = 4'd5;
      7'b01_00_111: lo = 4'd7;
      7'b01_01_000: lo = 4'd0;
      7'b01_01_001: lo = 4'd2;
      7'b01_01_010: lo = 4'd8;
      7'b01_01_011: lo = 4'd10;
      7'b01_01_100: lo = 4'd1;
      7'b01_01_101: lo = 4'd3;
      7'b01_01_110: lo = 4'd9;
      7'b01_01_111: lo = 4'd11;
      7'b01_10_000: lo = 4'd0;
      7'b01_10_001: lo = 4'd4;
      7'b01_10_010: lo = 4'd1;
      7'b01_10_011: lo = 4'd5;
      7'b01_10_100: lo = 4'd8;
      7'b01_10_101: lo = 4'd12;
      7'b01_10_110: lo = 4'd9;
      7'b01_10_111: lo = 4'd13;
      7'b10_00_000: lo = 4'd0;
      7'b10_00_001: lo = 4'd4;
      7'b10_00_010: lo = 4'd1;
      7'b10_00_011: lo = 4'd5;
      7'b10_00_100: lo = 4'd8;
      7'b10_00_101: lo = 4'd12;
      7'b10_00_110: lo = 4'd9;
      7'b10_00_111: lo = 4'd13;
      7'b10_01_000: lo = 4'd0;
      7'b10_01_001: lo = 4'd2;
      7'b10_01_010: lo = 4'd8;
      7'b10_01_011: lo = 4'd10;
      7'b10_01_100: lo = 4'd1;
      7'b10_01_101: lo = 4'd3;
      7'b10_01_110: lo = 4'd9;
      7'b10_01_111: lo = 4'd11;
      7'b10_10_000: lo = 4'd0;
      7'b10_10_001: lo = 4'd2;
      7'b10_10_010: lo = 4'd4;
      7'b10_10_011: lo = 4'd6;
      7'b10_10_100: lo = 4'd1;
      7'b10_10_101: lo = 4'd3;
      7'b10_10_110: lo = 4'd5;
      7'b10_10_111: lo = 4'd7;
      7'b11_00_000: lo = 4'd0;
      7'b11_00_001: lo = 4'd2;
      7'b11_00_010: lo = 4'd4;
      7'b11_00_011: lo = 4'd6;
      7'b11_00_100: lo = 4'd8;
      7'b11_00_101: lo = 4'd10;
      7'b11_00_110: lo = 4'd12;
      7'b11_00_111: lo = 4'd14;
      7'b11_01_000: lo = 4'd0;
      7'b11_01_001: lo = 4'd4;
      7'b11_01_010: lo = 4'd1;
      7'b11_01_011: lo = 4'd5;
      7'b11_01_100: lo = 4'd8;
      7'b11_01_101: lo = 4'd12;
      7'b11_01_110: lo = 4'd9;
      7'b11_01_111: lo = 4'd13;
      7'b11_10_000: lo = 4'd0;
      7'b11_10_001: lo = 4'd2;
      7'b11_10_010: lo = 4'd8;
      7'b11_10_011: lo = 4'd10;
      7'b11_10_100: lo = 4'd1;
      7'b11_10_101: lo = 4'd3;
      7'b11_10_110: lo = 4'd9;
      7'b11_10_111: lo = 4'd11;
      7'b11_11_000: lo = 4'd0;
      7'b11_11_001: lo = 4'd2;
      7'b11_11_010: lo = 4'd4;
      7'b11_11_011: lo = 4'd6;
      7'b11_11_100: lo = 4'd1;
      7'b11_11_101: lo = 4'd3;
      7'b11_11_110: lo = 4'd5;
      7'b11_11_111: lo = 4'd7;
      default: lo = 4'd0;
    endcase
  end

  // paired bit: forward A 3,2,1,0; forward B 3,2,1; inverse A 1,2,3; inverse B 0,1,2,3
  reg [1:0] pbit;
  always @(*) begin
    case (cp_pass)
      P_FA, P_FB: pbit = 2'd3 - layer;
      P_IA:       pbit = 2'd1 + layer;
      default:    pbit = layer;
    endcase
  end
  wire [3:0] hi = lo | (4'd1 << pbit);

  // twiddle: global index jj of the lower coefficient, global layer
  wire       strided = (cp_pass == P_FA) | (cp_pass == P_IB);
  wire [7:0] jj      = strided ? {lo, cp_j} : {cp_j, lo};
  // forward: layer l = 0..6 (len = 128 >> l), k = {1, jj} >> (8 - l)
  // inverse: s = 1..7 (len = 2^s), x = {1, jj} >> (s + 1), k = x ^ (0x7F >> s)
  wire [2:0] l_fwd = (cp_pass == P_FA) ? {1'b0, layer} : 3'd4 + {1'b0, layer};
  wire [2:0] s_inv = (cp_pass == P_IA) ? 3'd1 + {1'b0, layer} : 3'd4 + {1'b0, layer};
  wire [3:0] sh    = cp_inv ? {1'b0, s_inv} + 4'd1 : 4'd8 - {1'b0, l_fwd};
  wire [8:0] xk    = {1'b1, jj} >> sh;
  wire [6:0] msk   = cp_inv ? (7'h7F >> s_inv) : 7'h00;
  assign rom_addr = xk[6:0] ^ msk;
  assign rom_use  = cp_act;

  // working set C: 2 reads, 2 writes per clock, or the load from L
  reg  [16*12-1:0] Cv;                // C entry e at Cv[e*12 +: 12]
  wire [11:0] a = Cv[lo*12 +: 12];
  wire [11:0] b = Cv[hi*12 +: 12];

  wire [11:0] pre_sum, pre_diff;      // a + b, b - a
  modq_addsub #(.REG_OUT(0)) u_pre (
      .clk  (clk),
      .rst_n(rst_n),
      .x    (b),
      .y    (a),
      .sum  (pre_sum),
      .diff (pre_diff)
  );

  // S1 and the tag of what comes out of the multiplier 3 clocks after the start
  reg  [3:0] s1_lo, s1_hi;
  reg  [2:0] s1_pos;
  reg        s1_last, s1_inv;
  always @(posedge clk) begin
    if (!rst_n) begin
      mul_go <= 1'b0;
      mul_y  <= 12'd0; mul_z <= 12'd0; byp_x <= 12'd0;
      s1_lo  <= 4'd0; s1_hi <= 4'd0; s1_pos <= 3'd0; s1_last <= 1'b0; s1_inv <= 1'b0;
    end else begin
      mul_go  <= issue;
      mul_y   <= cp_inv ? pre_diff : b;
      byp_x   <= cp_inv ? pre_sum  : a;
      mul_z   <= rom_data;
      s1_lo   <= lo; s1_hi <= hi; s1_pos <= pos; s1_last <= is_last; s1_inv <= cp_inv;
    end
  end

  wire [15:0] tag_q;
  ntt_delay #(.W(16), .N(3)) u_tag (
      .clk  (clk),
      .rst_n(rst_n),
      .clr  (1'b0),
      .d    ({mul_go, s1_last, s1_inv, s1_pos, s1_lo, s1_hi, 2'b00}),
      .q    (tag_q)
  );
  wire       t_v    = tag_q[15];
  wire       t_last = tag_q[14];
  wire       t_inv  = tag_q[13];
  wire [2:0] t_pos  = tag_q[12:10];
  wire [3:0] t_lo   = tag_q[9:6];
  wire [3:0] t_hi   = tag_q[5:2];
  assign res_v = t_v;

  wire [11:0] w0 = t_inv ? byp_out : post_sum;    // new f[lo]
  wire [11:0] w1 = t_inv ? p_out   : post_diff;   // new f[hi]
  wire        wb = t_v & ~t_last;

  generate
    for (ge = 0; ge < 16; ge = ge + 1) begin : g_c
      always @(posedge clk) begin
        if (HAS_L && xfer)
          Cv[ge*12 +: 12] <= (ge == 15) ? coeff_q : Lw[((ge == 15) ? 0 : ge)*12 +: 12];
        else if (!HAS_L && ld_take && ld_cnt == ge)
          Cv[ge*12 +: 12] <= coeff_q;
        else if (wb && t_lo == ge)
          Cv[ge*12 +: 12] <= w0;
        else if (wb && t_hi == ge)
          Cv[ge*12 +: 12] <= w1;
      end
    end
  endgenerate

  // ---------------------------------------------- F and the drain to the pins
  generate
    if (HAS_F) begin : g_f
      reg [23:0] F [0:7];
      always @(posedge clk) begin
        if (t_v && t_last) F[t_pos] <= {w1, w0};
      end

      reg        dr_act;
      reg  [2:0] dr_m;
      reg  [1:0] dr_ph;
      wire       dr_start = t_v & t_last & (t_pos == 3'd0);
      always @(posedge clk) begin
        if (!rst_n) begin
          dr_act <= 1'b0; dr_m <= 3'd0; dr_ph <= 2'd0;
        end else if (dr_start) begin
          dr_act <= 1'b1; dr_m <= 3'd0; dr_ph <= 2'd0;
        end else if (dr_act) begin
          if (dr_ph == 2'd2) begin
            dr_ph <= 2'd0;
            dr_m  <= dr_m + 3'd1;
            if (dr_m == 3'd7) dr_act <= 1'b0;
          end else begin
            dr_ph <= dr_ph + 2'd1;
          end
        end
      end

      assign pair_valid = dr_act & (dr_ph == 2'd0);
      assign r0 = F[dr_m][11:0];
      assign r1 = F[dr_m][23:12];
    end else begin : g_nof
      assign pair_valid = t_v & t_last;
      assign r0 = w0;
      assign r1 = w1;
    end
  endgenerate

  wire _unused = &{tag_q[1:0], xk[8:7], t_pos, Lw, 1'b0};

endmodule
