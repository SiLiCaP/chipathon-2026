/*
 * keccak_lat: Keccak-f[1600] sponge core with the state in a latch array, lane-serial datapath.
 *
 * Research prototype (branch sha3-latch, not part of the specs). Same ports and commands as keccak_ring,
 * so it can replace it in the top. Goal: no flip-flop to flip-flop shift hops, so the design should
 * harden with Tiny Tapeout's default hold uncertainty (0.25 ns).
 *
 * State: 25 words x 64 dlxtp latches. Word s is written through its own clock gate: the gate opens in
 * the high phase of the clock after the write was decided, while the write register wdata holds the
 * data. A latch closes half a clock after its data launched, so the write path has about half a period
 * of hold slack. One read port (25:1 mux) reads any word; a word written this clock reads back its new
 * value (the latch is transparent), so commands can follow each other on the same lane.
 *
 * pi without moving data: the output lane D of a round is written into the word that held its source
 * lane pi_src(D). So at the start of round r, logical lane L sits in word f_r(L) = pi_src^r(L). The
 * 24 non-zero lanes form one pi cycle CYC, so f_r(CYC[i]) = CYC[(i + r) mod 24] and f_24 = identity:
 * after a permutation the state is back in natural order for ABSORB and SQUEEZE.
 *
 * Round r (61 clocks):
 *   PAR  25 clocks: read every lane, C[x] = column parities (5 x 64 flip-flops)
 *   ROW  5 rows x 7 clocks: read the row's 5 source lanes, B[k] = rot(lane ^ D[col], rho) (5 x 64),
 *        then chi + iota for k = 0..4 into wdata, written 1 clock later into the source's word
 *   TAIL 1 clock: chi of the last lane
 * PERMUTE: 24 x 61 = 1,464 clocks. CLEAR: 1 clock.
 *
 * Host interface (one command per clock, only while !busy), as keccak_ring with K = 8:
 *   ABSORB  lane[ptr/8] ^= din << 8*(ptr%8), ptr++     (lanes 0..20)
 *   SQUEEZE dout <= the same byte, next clock, ptr++
 *   PERMUTE Keccak-f[1600], ptr = 0
 *   CLEAR   state = 0, ptr = 0
 *   SKIP    ptr jumps to the next lane, which stays untouched (only at a lane boundary)
 */

`default_nettype none

module keccak_lat #(
    parameter K = 8               // bits per I/O step (fixed at 8, kept for keccak_ring's ports)
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         cmd_valid,
    input  wire [2:0]   cmd,
    input  wire [K-1:0] din,
    output reg  [K-1:0] dout,
    output wire         busy
);

  localparam CMD_ABSORB = 3'd0, CMD_SQUEEZE = 3'd1, CMD_PERMUTE = 3'd2, CMD_CLEAR = 3'd3, CMD_SKIP = 3'd4;
  localparam S_IDLE = 3'd0, S_PAR = 3'd1, S_ROW = 3'd2, S_TAIL = 3'd3, S_CLR = 3'd4;

  // ---------------- constants ----------------
  function [5:0] rho_off(input integer L);
    case (L)
      0: rho_off = 0;   1: rho_off = 1;   2: rho_off = 62;  3: rho_off = 28;  4: rho_off = 27;
      5: rho_off = 36;  6: rho_off = 44;  7: rho_off = 6;   8: rho_off = 55;  9: rho_off = 20;
      10: rho_off = 3;  11: rho_off = 10; 12: rho_off = 43; 13: rho_off = 25; 14: rho_off = 39;
      15: rho_off = 41; 16: rho_off = 45; 17: rho_off = 15; 18: rho_off = 21; 19: rho_off = 8;
      20: rho_off = 18; 21: rho_off = 2;  22: rho_off = 61; 23: rho_off = 56; default: rho_off = 14;
    endcase
  endfunction

  // pi: post-pi lane D = (x', y') comes from lane (x, y) with x' = y, y' = 2x + 3y
  function integer pi_src(input integer D);
    integer x, y;
    begin
      pi_src = 0;
      for (x = 0; x < 5; x = x + 1)
        for (y = 0; y < 5; y = y + 1)
          if (D == y + 5 * ((2 * x + 3 * y) % 5)) pi_src = x + 5 * y;
    end
  endfunction

  // the pi cycle over lanes 1..24: CYC[i+1] = pi_src(CYC[i])
  function [4:0] cyc(input [4:0] i);
    case (i)
      0: cyc = 1;   1: cyc = 6;   2: cyc = 9;   3: cyc = 22;  4: cyc = 14;  5: cyc = 20;
      6: cyc = 2;   7: cyc = 12;  8: cyc = 13;  9: cyc = 19;  10: cyc = 23; 11: cyc = 15;
      12: cyc = 4;  13: cyc = 24; 14: cyc = 21; 15: cyc = 8;  16: cyc = 16; 17: cyc = 5;
      18: cyc = 3;  19: cyc = 18; 20: cyc = 17; 21: cyc = 11; 22: cyc = 7;  default: cyc = 10;
    endcase
  endfunction

  // position of lane L (1..24) in CYC
  function [4:0] cyc_idx(input [4:0] L);
    case (L)
      1: cyc_idx = 0;   6: cyc_idx = 1;   9: cyc_idx = 2;   22: cyc_idx = 3;  14: cyc_idx = 4;
      20: cyc_idx = 5;  2: cyc_idx = 6;   12: cyc_idx = 7;  13: cyc_idx = 8;  19: cyc_idx = 9;
      23: cyc_idx = 10; 15: cyc_idx = 11; 4: cyc_idx = 12;  24: cyc_idx = 13; 21: cyc_idx = 14;
      8: cyc_idx = 15;  16: cyc_idx = 16; 5: cyc_idx = 17;  3: cyc_idx = 18;  18: cyc_idx = 19;
      17: cyc_idx = 20; 11: cyc_idx = 21; 7: cyc_idx = 22;  default: cyc_idx = 23;  // 10
    endcase
  endfunction

  // word of logical lane L at round offset off (0..24): f_off(L)
  function [4:0] word_of(input [4:0] L, input [4:0] off);
    reg [5:0] s;
    begin
      s = cyc_idx(L) + off;
      if (s >= 24) s = s - 24;
      word_of = (L == 0) ? 5'd0 : cyc(s[4:0]);
    end
  endfunction

  // ---------------- control ----------------
  reg [2:0] st;
  reg [4:0] cnt;        // PAR: lane 0..24
  reg [2:0] row, j;     // ROW: row 0..4, clock in row 0..6
  reg [4:0] rnd;
  reg [7:0] ptr;

  wire idle    = (st == S_IDLE);
  wire go      = idle & cmd_valid;
  wire [4:0] lane_sel = ptr[7:3];
  wire io_lane = (lane_sel < 5'd21);
  wire absorb  = go & (cmd == CMD_ABSORB) & io_lane;
  wire squeeze = go & (cmd == CMD_SQUEEZE);
  wire in_par  = (st == S_PAR);
  wire in_row  = (st == S_ROW);
  wire in_tail = (st == S_TAIL);
  wire in_clr  = (st == S_CLR);
  assign busy = ~idle;

  always @(posedge clk) begin
    if (!rst_n) begin
      st  <= S_IDLE;
      cnt <= 5'd0;
      row <= 3'd0;
      j   <= 3'd0;
      rnd <= 5'd0;
      ptr <= 8'd0;
    end else begin
      case (st)
        S_IDLE: begin
          cnt <= 5'd0;
          rnd <= 5'd0;
          if (go) begin
            case (cmd)
              CMD_PERMUTE: begin st <= S_PAR; ptr <= 8'd0; end
              CMD_CLEAR:   begin st <= S_CLR; ptr <= 8'd0; end
              CMD_ABSORB, CMD_SQUEEZE: ptr <= ptr + 8'd1;
              CMD_SKIP:    ptr <= (ptr | 8'd7) + 8'd1;
              default:     ;
            endcase
          end
        end
        S_PAR: begin
          cnt <= cnt + 5'd1;
          if (cnt == 5'd24) begin
            st  <= S_ROW;
            row <= 3'd0;
            j   <= 3'd0;
          end
        end
        S_ROW: begin
          if (j == 3'd6) begin
            j <= 3'd0;
            if (row == 3'd4) st <= S_TAIL;
            else row <= row + 3'd1;
          end else begin
            j <= j + 3'd1;
          end
        end
        S_TAIL: begin
          cnt <= 5'd0;
          if (rnd == 5'd23) st <= S_IDLE;
          else begin
            st  <= S_PAR;
            rnd <= rnd + 5'd1;
          end
        end
        default: st <= S_IDLE;  // S_CLR: the write happens this clock
      endcase
    end
  end

  // ---------------- addresses ----------------
  // ROW read: lane D = j + 5*row of the output row (j < 5), its source sits in word f_{r+1}(D)
  wire [4:0] d_rd = {2'b00, j} + {row, 2'b00} + {2'b00, row};
  wire [4:0] rnd1 = rnd + 5'd1;
  reg  [4:0] raddr;
  always @(*) begin
    if (in_par)      raddr = word_of(cnt, rnd);
    else if (in_row) raddr = word_of(d_rd, rnd1);
    else             raddr = lane_sel;
  end

  // chi/write stage: lane k of row row_w, one clock behind the read of B[(k+2)%5]
  wire       wr_now = (in_row & (j >= 3'd3)) | (in_row & (j == 3'd0) & (row != 3'd0)) | in_tail;
  wire [2:0] wk     = (in_row & (j >= 3'd3)) ? j - 3'd3 : 3'd4;
  wire [2:0] row_w  = (in_row & (j >= 3'd3)) ? row : (in_tail ? 3'd4 : row - 3'd1);
  wire [4:0] d_wr   = {2'b00, wk} + {row_w, 2'b00} + {2'b00, row_w};
  wire [4:0] waddr  = word_of(d_wr, rnd1);

  // per-read constants of the source lane pi_src(D): theta column and rho offset
  reg [2:0] col_rd;
  reg [5:0] rho_rd;
  integer di;
  always @(*) begin
    col_rd = 3'd0;
    rho_rd = 6'd0;
    for (di = 0; di < 25; di = di + 1)
      if (d_rd == di) begin
        col_rd = pi_src(di) % 5;
        rho_rd = rho_off(pi_src(di));
      end
  end

  // ---------------- state latches and read port ----------------
  reg  [63:0] wdata;
  wire [63:0] q [0:24];
  wire [24:0] wen;
  wire [24:0] gw;
  genvar gs, gb, gx;
  generate
    for (gs = 0; gs < 25; gs = gs + 1) begin : g_word
      assign wen[gs] = in_clr | (absorb & (lane_sel == gs)) | (wr_now & (waddr == gs));
      kl_icg u_icg (.clk(clk), .en(wen[gs]), .gclk(gw[gs]));
      for (gb = 0; gb < 64; gb = gb + 1) begin : g_bit
        kl_lat u_lat (.g(gw[gs]), .d(wdata[gb]), .q(q[gs][gb]));
      end
    end
  endgenerate

  reg [63:0] rdata;
  integer si;
  always @(*) begin
    rdata = 64'd0;
    for (si = 0; si < 25; si = si + 1)
      if (raddr == si) rdata = q[si];
  end

  // ---------------- theta ----------------
  // C[x] accumulates in PAR (its gate opens on the clocks that read column x)
  reg  [2:0]  col_par;          // cnt % 5
  always @(posedge clk) col_par <= (in_par && col_par != 3'd4) ? col_par + 3'd1 : 3'd0;

  wire [63:0] c [0:4];
  wire [4:0]  gc;
  wire        first = (cnt < 5'd5);
  generate
    for (gx = 0; gx < 5; gx = gx + 1) begin : g_c
      reg [63:0] cr;
      assign c[gx] = cr;
      kl_icg u_icg (.clk(clk), .en(in_par & (col_par == gx)), .gclk(gc[gx]));
      always @(posedge gc[gx]) cr <= (first ? 64'd0 : cr) ^ rdata;
    end
  endgenerate

  // D[x] = C[x-1] ^ rot(C[x+1], 1), for the column of the lane read now
  reg [63:0] dsel;
  always @(*) begin
    case (col_rd)
      3'd0: dsel = c[4] ^ {c[1][62:0], c[1][63]};
      3'd1: dsel = c[0] ^ {c[2][62:0], c[2][63]};
      3'd2: dsel = c[1] ^ {c[3][62:0], c[3][63]};
      3'd3: dsel = c[2] ^ {c[4][62:0], c[4][63]};
      default: dsel = c[3] ^ {c[0][62:0], c[0][63]};
    endcase
  end

  // ---------------- rho, B registers ----------------
  wire [63:0] th = rdata ^ dsel;
  wire [127:0] th2 = {th, th} << rho_rd;
  wire [63:0] rot_out = th2[127:64];

  wire [63:0] b [0:4];
  wire [4:0]  gbk;
  generate
    for (gx = 0; gx < 5; gx = gx + 1) begin : g_b
      reg [63:0] br;
      assign b[gx] = br;
      kl_icg u_icg (.clk(clk), .en(in_row & (j == gx)), .gclk(gbk[gx]));
      always @(posedge gbk[gx]) br <= rot_out;
    end
  endgenerate

  // ---------------- chi, iota, write register ----------------
  reg [63:0] b0, b1, b2;
  always @(*) begin
    case (wk)
      3'd0: begin b0 = b[0]; b1 = b[1]; b2 = b[2]; end
      3'd1: begin b0 = b[1]; b1 = b[2]; b2 = b[3]; end
      3'd2: begin b0 = b[2]; b1 = b[3]; b2 = b[4]; end
      3'd3: begin b0 = b[3]; b1 = b[4]; b2 = b[0]; end
      default: begin b0 = b[4]; b1 = b[0]; b2 = b[1]; end
    endcase
  end

  reg [63:0] rc;
  always @(*) begin
    case (rnd)
      5'd0:  rc = 64'h0000000000000001; 5'd1:  rc = 64'h0000000000008082;
      5'd2:  rc = 64'h800000000000808A; 5'd3:  rc = 64'h8000000080008000;
      5'd4:  rc = 64'h000000000000808B; 5'd5:  rc = 64'h0000000080000001;
      5'd6:  rc = 64'h8000000080008081; 5'd7:  rc = 64'h8000000000008009;
      5'd8:  rc = 64'h000000000000008A; 5'd9:  rc = 64'h0000000000000088;
      5'd10: rc = 64'h0000000080008009; 5'd11: rc = 64'h000000008000000A;
      5'd12: rc = 64'h000000008000808B; 5'd13: rc = 64'h800000000000008B;
      5'd14: rc = 64'h8000000000008089; 5'd15: rc = 64'h8000000000008003;
      5'd16: rc = 64'h8000000000008002; 5'd17: rc = 64'h8000000000000080;
      5'd18: rc = 64'h000000000000800A; 5'd19: rc = 64'h800000008000000A;
      5'd20: rc = 64'h8000000080008081; 5'd21: rc = 64'h8000000000008080;
      5'd22: rc = 64'h0000000080000001; default: rc = 64'h8000000080008008;
    endcase
  end

  wire [63:0] chi    = b0 ^ (~b1 & b2) ^ ((d_wr == 5'd0) ? rc : 64'd0);
  wire [63:0] din_sh = {56'd0, din} << {ptr[2:0], 3'b000};

  always @(posedge clk) begin
    if (wr_now)      wdata <= chi;
    else if (absorb) wdata <= rdata ^ din_sh;
    else             wdata <= 64'd0;    // CLEAR (and idle)
  end

  // ---------------- squeeze ----------------
  wire [63:0] sq_sh = rdata >> {ptr[2:0], 3'b000};
  always @(posedge clk) begin
    if (!rst_n) dout <= {K{1'b0}};
    else if (squeeze && io_lane) dout <= sq_sh[K-1:0];
    else dout <= {K{1'b0}};
  end

  wire _unused = &{1'b0, gw, gc, gbk};

endmodule

// Integrated clock gate: sky130 hd dlclkp (latch on CLK low + AND). Behavioral model for RTL sim.
module kl_icg (
    input  wire clk,
    input  wire en,
    output wire gclk
);
`ifdef KL_BEHAV
  reg en_l;
  always @(clk or en) if (!clk) en_l = en;
  assign gclk = clk & en_l;
`else
  sky130_fd_sc_hd__dlclkp_1 u_cg (.CLK(clk), .GATE(en), .GCLK(gclk));
`endif
endmodule

// State latch: sky130 hd dlxtp (transparent while G is high). Behavioral model for RTL sim.
module kl_lat (
    input  wire g,
    input  wire d,
    output wire q
);
`ifdef KL_BEHAV
  reg l;
  always @(g or d) if (g) l = d;
  assign q = l;
`else
  sky130_fd_sc_hd__dlxtp_1 u_l (.GATE(g), .D(d), .Q(q));
`endif
endmodule
