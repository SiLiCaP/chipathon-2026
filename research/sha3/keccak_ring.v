/*
 * keccak_ring: Keccak-f[1600] sponge core with the state in clock-gated shift rings.
 *
 * Research prototype (not part of the specs). Model and proof of the schedule: ring_model.py.
 *
 * State: 25 lanes x K sub-rings x M = 64/K flip-flops. Sub-ring b of lane L (L = x + 5y) holds the lane
 * bits z = K*i + b; bit 0 of a ring is its exit, bit M-1 takes the closure. Every state flip-flop is a
 * plain shift stage (no enable, no reset, no mux). Rings are only clocked or not, through one clock gate
 * per group (at most 2 per lane): that is how rho (a per-lane rotation) costs M-1 clocks instead of a
 * mux per bit.
 *
 * Permutation: theta-only pass (M clocks), then 24 x [rho phase (M-1) + pass (M)] clocks:
 * K = 8: 368, K = 4: 760, K = 2: 1544. A pass reads K slices per clock (pi and the rho sub-ring
 * permutation are wiring), applies chi, iota and the next round's theta, and writes the result back
 * into the rings (in-loop).
 *
 * Host interface (one command per clock, only while !busy). A lane step moves K bits:
 *   ABSORB  lane[ptr/M] ^= din[K-1:0] at bits K*(ptr%M).., ptr++     (lanes 0..20)
 *   SQUEEZE dout[K-1:0] <= the same K bits of the lane, next clock, ptr++
 *   PERMUTE Keccak-f[1600], busy for the permutation, ptr = 0
 *   CLEAR   state = 0, busy for M clocks, ptr = 0
 *   SKIP    ptr jumps to the next lane, which stays untouched (only at a lane boundary)
 * The host pads, and absorbs and squeezes whole lanes between permutations; SKIP passes over the
 * all-zero lanes of a padded block in one clock each.
 */

`default_nettype none

module keccak_ring #(
    parameter K = 8,              // slices per clock = bits per I/O step: 2, 4 or 8
    parameter CLOCK_GATING = 1    // 1: clock gate per ring group; 0: enable flip-flops (comparison)
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         cmd_valid,
    input  wire [2:0]   cmd,
    input  wire [K-1:0] din,
    output reg  [K-1:0] dout,
    output wire         busy
);

  localparam M  = 64 / K;                  // flip-flops per sub-ring
  localparam MW = (M == 8) ? 3 : (M == 16) ? 4 : 5;
  localparam PW = MW + 5;                  // byte/lane-step pointer
  localparam CMD_ABSORB = 3'd0, CMD_SQUEEZE = 3'd1, CMD_PERMUTE = 3'd2, CMD_CLEAR = 3'd3, CMD_SKIP = 3'd4;
  localparam S_IDLE = 3'd0, S_THI = 3'd1, S_RHO = 3'd2, S_PASS = 3'd3, S_CLR = 3'd4;

  // ---------------- constants ----------------
  function integer rho_off(input integer L);
    integer x, y;
    begin
      x = L % 5;
      y = L / 5;
      case (x * 5 + y)
        0: rho_off = 0;   1: rho_off = 36;  2: rho_off = 3;   3: rho_off = 41;  4: rho_off = 18;
        5: rho_off = 1;   6: rho_off = 44;  7: rho_off = 10;  8: rho_off = 45;  9: rho_off = 2;
        10: rho_off = 62; 11: rho_off = 6;  12: rho_off = 43; 13: rho_off = 15; 14: rho_off = 61;
        15: rho_off = 28; 16: rho_off = 55; 17: rho_off = 25; 18: rho_off = 21; 19: rho_off = 56;
        20: rho_off = 27; 21: rho_off = 20; 22: rho_off = 39; 23: rho_off = 8;  default: rho_off = 14;
      endcase
    end
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

  // physical sub-ring of lane L that holds post-rho sub-ring q
  function integer sigma(input integer L, input integer q);
    sigma = (q - rho_off(L) + 64) % K;
  endfunction

  // rho-phase clocks of physical sub-ring b of lane L: (-floor((b + r) / K)) mod M
  function integer rho_steps(input integer L, input integer b);
    rho_steps = (64 - (b + rho_off(L)) / K) % M;
  endfunction

  // iota: is lane bit z one of the round-constant positions 0, 1, 3, 7, 15, 31, 63, and which
  function integer rc_idx(input integer z);
    case (z)
      0: rc_idx = 0; 1: rc_idx = 1; 3: rc_idx = 2; 7: rc_idx = 3;
      15: rc_idx = 4; 31: rc_idx = 5; 63: rc_idx = 6; default: rc_idx = -1;
    endcase
  endfunction

  // ---------------- control ----------------
  reg [2:0]    st;
  reg [MW-1:0] cnt;
  reg [4:0]    rnd;
  reg [PW-1:0] ptr;

  wire idle    = (st == S_IDLE);
  wire go      = idle & cmd_valid;
  wire io_step = go & ((cmd == CMD_ABSORB) | (cmd == CMD_SQUEEZE));
  wire absorb  = go & (cmd == CMD_ABSORB);
  wire in_pass = (st == S_PASS);
  wire in_thi  = (st == S_THI);
  wire in_rho  = (st == S_RHO);
  wire in_clr  = (st == S_CLR);
  wire last    = (rnd == 5'd23);
  wire cnt_end = (cnt == M - 1);
  assign busy = ~idle;

  always @(posedge clk) begin
    if (!rst_n) begin
      st  <= S_IDLE;
      cnt <= 0;
      rnd <= 5'd0;
      ptr <= 0;
    end else begin
      case (st)
        S_IDLE: begin
          cnt <= 0;
          rnd <= 5'd0;
          if (go) begin
            case (cmd)
              CMD_PERMUTE: begin st <= S_THI; ptr <= 0; end
              CMD_CLEAR:   begin st <= S_CLR; ptr <= 0; end
              CMD_ABSORB, CMD_SQUEEZE: ptr <= ptr + 1'b1;
              CMD_SKIP:    ptr <= (ptr | (M - 1)) + 1'b1;   // next lane, lane untouched
              default:     ;                                // 5..7 reserved: no-op
            endcase
          end
        end
        S_THI: begin
          cnt <= cnt + 1'b1;
          if (cnt_end) st <= S_RHO;
        end
        S_RHO: begin
          cnt <= (cnt == M - 2) ? 0 : cnt + 1'b1;
          if (cnt == M - 2) st <= S_PASS;
        end
        S_PASS: begin
          cnt <= cnt + 1'b1;
          if (cnt_end) begin
            if (last) st <= S_IDLE;
            else begin
              st  <= S_RHO;
              rnd <= rnd + 5'd1;
            end
          end
        end
        default: begin  // S_CLR
          cnt <= cnt + 1'b1;
          if (cnt_end) st <= S_IDLE;
        end
      endcase
    end
  end

  // the 7 round-constant bits (lane bits 0, 1, 3, 7, 15, 31, 63) of round rnd
  reg [6:0] rc7;
  always @(*) begin
    case (rnd)
      5'd0: rc7 = 7'h01;  5'd1: rc7 = 7'h1A;  5'd2: rc7 = 7'h5E;  5'd3: rc7 = 7'h70;
      5'd4: rc7 = 7'h1F;  5'd5: rc7 = 7'h21;  5'd6: rc7 = 7'h79;  5'd7: rc7 = 7'h55;
      5'd8: rc7 = 7'h0E;  5'd9: rc7 = 7'h0C;  5'd10: rc7 = 7'h35; 5'd11: rc7 = 7'h26;
      5'd12: rc7 = 7'h3F; 5'd13: rc7 = 7'h4F; 5'd14: rc7 = 7'h5D; 5'd15: rc7 = 7'h53;
      5'd16: rc7 = 7'h52; 5'd17: rc7 = 7'h48; 5'd18: rc7 = 7'h16; 5'd19: rc7 = 7'h66;
      5'd20: rc7 = 7'h79; 5'd21: rc7 = 7'h58; 5'd22: rc7 = 7'h21; default: rc7 = 7'h74;
    endcase
  end

  // iota bits of lane 0 for the K slices of this pass clock
  wire [K-1:0] rc_now;
  genvar gL, gb, gx, gy, gq;
  generate
    for (gq = 0; gq < K; gq = gq + 1) begin : g_rc
      wire [6:0] hit;
      for (gx = 0; gx < 7; gx = gx + 1) begin : g_h
        localparam integer Z = (1 << gx) - 1;
        if (Z % K == gq) begin : g_y
          assign hit[gx] = (cnt == Z / K) & rc7[gx];
        end else begin : g_n
          assign hit[gx] = 1'b0;
        end
      end
      assign rc_now[gq] = |hit;
    end
  endgenerate

  // ---------------- state rings ----------------
  wire [25*64-1:0] ring;             // ring[(L*K + b)*M +: M], bit 0 = exit, bit M-1 = last in
  wire [K-1:0] ex   [0:24];          // ex[L][b]   = exit bits
  wire [K-1:0] tapl [0:24];          // tapl[L][b] = position M-1 (slice 63 at a pass start)
  wire [K-1:0] cl   [0:24];          // closure bits
  wire [25*K-1:0] gck;               // gated clock of each ring

  generate
    for (gL = 0; gL < 25; gL = gL + 1) begin : g_lane
      for (gb = 0; gb < K; gb = gb + 1) begin : g_bit
        assign ex[gL][gb]   = ring[(gL*K+gb)*M];
        assign tapl[gL][gb] = ring[(gL*K+gb)*M+M-1];
      end
    end
  endgenerate

  // ---------------- round pass: pi and rho sub-ring permutation (wiring), chi, iota ----------------
  // In the theta-only pass the chi output is not used: the closure takes the exit instead.
  wire [K-1:0] bpi [0:24];           // post-pi B[D][q]
  wire [K-1:0] e   [0:24];           // chi/iota out
  wire [24:0]  bw, ew;               // post-pi slice 63 at a pass start, and its chi/iota
  generate
    for (gL = 0; gL < 25; gL = gL + 1) begin : g_pi
      for (gq = 0; gq < K; gq = gq + 1) begin : g_q
        assign bpi[gL][gq] = ex[pi_src(gL)][sigma(pi_src(gL), gq)];
      end
      assign bw[gL] = tapl[pi_src(gL)][sigma(pi_src(gL), K - 1)];
    end
    for (gy = 0; gy < 5; gy = gy + 1) begin : g_chi_y
      for (gx = 0; gx < 5; gx = gx + 1) begin : g_chi_x
        if (gx == 0 && gy == 0) begin : g_iota
          assign e[0]  = bpi[0] ^ (~bpi[1] & bpi[2]) ^ rc_now;
          assign ew[0] = bw[0] ^ (~bw[1] & bw[2]) ^ rc7[6];
        end else begin : g_plain
          assign e[gx+5*gy]  = bpi[gx+5*gy] ^ (~bpi[(gx+1)%5+5*gy] & bpi[(gx+2)%5+5*gy]);
          assign ew[gx+5*gy] = bw[gx+5*gy] ^ (~bw[(gx+1)%5+5*gy] & bw[(gx+2)%5+5*gy]);
        end
      end
    end
  endgenerate

  // ---------------- theta: column parities, D ----------------
  // theta-only pass: parities of the canonical exits and of slice 63 (position M-1 of sub-ring K-1);
  // round pass: parities of the chi output and of the chi'd slice 63.
  reg  [4:0]   creg;                 // C of slice K*i + K-1 of the previous clock
  wire [K-1:0] cc [0:4];
  wire [4:0]   c63, cprev;
  wire [K-1:0] dd [0:4];
  generate
    for (gx = 0; gx < 5; gx = gx + 1) begin : g_d
      wire [K-1:0] ci = ex[gx] ^ ex[gx+5] ^ ex[gx+10] ^ ex[gx+15] ^ ex[gx+20];
      wire [K-1:0] ce = e[gx] ^ e[gx+5] ^ e[gx+10] ^ e[gx+15] ^ e[gx+20];
      wire ci63 = tapl[gx][K-1] ^ tapl[gx+5][K-1] ^ tapl[gx+10][K-1] ^ tapl[gx+15][K-1] ^ tapl[gx+20][K-1];
      wire ce63 = ew[gx] ^ ew[gx+5] ^ ew[gx+10] ^ ew[gx+15] ^ ew[gx+20];
      assign cc[gx]    = in_thi ? ci : ce;
      assign c63[gx]   = in_thi ? ci63 : ce63;
      assign cprev[gx] = (cnt != 0) ? creg[gx] : c63[gx];
    end
    for (gx = 0; gx < 5; gx = gx + 1) begin : g_dd
      // D[x][z] = C[x-1][z] ^ C[x+1][z-1]
      if (K > 1) begin : g_k
        assign dd[gx] = cc[(gx+4)%5] ^ {cc[(gx+1)%5][K-2:0], cprev[(gx+1)%5]};
      end
    end
  endgenerate

  always @(posedge clk) begin
    creg <= {cc[4][K-1], cc[3][K-1], cc[2][K-1], cc[1][K-1], cc[0][K-1]};
  end

  // ---------------- closures ----------------
  // closure = (pass ? chi : exit, zeroed by CLEAR) ^ (theta D | absorbed data)
  wire         theta_on = in_thi | (in_pass & ~last);
  wire [K-1:0] din_x    = absorb ? din : {K{1'b0}};
  generate
    for (gx = 0; gx < 5; gx = gx + 1) begin : g_x
      wire [K-1:0] xd = theta_on ? dd[gx] : {K{1'b0}};
      wire [K-1:0] xa = xd ^ din_x;   // lanes 0..20 (absorb never overlaps theta)
      for (gy = 0; gy < 5; gy = gy + 1) begin : g_y
        localparam integer L = gx + 5 * gy;
        wire [K-1:0] base = in_pass ? e[L] : (ex[L] & {K{~in_clr}});
        assign cl[L] = base ^ ((L < 21) ? xa : xd);
      end
    end
  endgenerate

  // ---------------- ring clocks ----------------
  wire [4:0] lane_sel = ptr[PW-1:MW];
  generate
    for (gL = 0; gL < 25; gL = gL + 1) begin : g_ck
      for (gb = 0; gb < K; gb = gb + 1) begin : g_ring
        localparam integer STEPS = rho_steps(gL, gb);
        wire en = in_pass | in_thi | in_clr | (in_rho & (cnt < STEPS)) |
                  ((gL < 21) & io_step & (lane_sel == gL));
        reg [M-1:0] r;
        assign ring[(gL*K+gb)*M +: M] = r;
        if (CLOCK_GATING) begin : g_cg
          // rings of a lane with the same rho step count share one gate
          if (gb == 0 || rho_steps(gL, gb) != rho_steps(gL, gb - 1)) begin : g_gate
            kc_icg u_icg (.clk(clk), .en(en), .gclk(gck[gL*K+gb]));
          end else begin : g_share
            assign gck[gL*K+gb] = gck[gL*K+gb-1];
          end
          always @(posedge gck[gL*K+gb]) r <= {cl[gL][gb], r[M-1:1]};
        end else begin : g_en
          assign gck[gL*K+gb] = 1'b0;
          always @(posedge clk) if (en) r <= {cl[gL][gb], r[M-1:1]};
        end
      end
    end
  endgenerate

  // ---------------- squeeze ----------------
  reg [K-1:0] sq;
  integer l;
  always @(*) begin
    sq = {K{1'b0}};
    for (l = 0; l < 21; l = l + 1)
      if (lane_sel == l) sq = ex[l];
  end
  always @(posedge clk) begin
    if (!rst_n) dout <= {K{1'b0}};
    else if (go && cmd == CMD_SQUEEZE) dout <= sq;
    else dout <= {K{1'b0}};
  end

  wire _unused = &{1'b0, gck};

endmodule

// Integrated clock gate: sky130 hd dlclkp (latch on CLK low + AND). Behavioral model for RTL sim.
module kc_icg (
    input  wire clk,
    input  wire en,
    output wire gclk
);
`ifdef KC_BEHAV_ICG
  reg en_l;
  always @(clk or en) if (!clk) en_l = en;
  assign gclk = clk & en_l;
`else
  sky130_fd_sc_hd__dlclkp_1 u_cg (.CLK(clk), .GATE(en), .GCLK(gclk));
`endif
endmodule
