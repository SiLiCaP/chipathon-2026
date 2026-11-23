/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * hash_io: the byte-wide host side of the Keccak core (keccak_ring) in the hash-only chip.
 *
 * Host commands (one hash word per clock; {sof, op} = cmd, coeff[7:0] = data; a byte comes back on
 * uo_out two clocks after the word that asked for it, as on branch sha3-4x2):
 *   0 ABSORB       XOR one byte into the state
 *   1 SQUEEZE      read one byte
 *   2 PERMUTE      Keccak-f[1600]
 *   3 CLEAR        zero the state
 *   4 SKIP         pass over an all-zero lane (only at a lane boundary)
 *   5 BURST        data = {x, n}: stream n (1..21) lanes = 8n bytes, one per clock, from this one word.
 *                  The host sends idle clocks (valid low) meanwhile; hash words during a burst are ignored.
 *                  x (0..7) more blocks follow by themselves: PERMUTE and another n lanes, x times.
 *   6 BURST_CBD    data = {eta3, ntt, auto, n}: like BURST, but the bytes are processed on the way out.
 *                  ntt = 0: SamplePolyCBD (FIPS 203 Alg. 8), 4-bit two's complement coefficients:
 *                    eta = 2: byte -> {f[2i+1], f[2i]}               (two coefficients per byte)
 *                    eta = 3: bytes 3j, 3j+1, 3j+2 -> {0, f[4j]}, {0, f[4j+1]}, {f[4j+3], f[4j+2]}
 *                    eta = 3 in framing mode 1: packed like eta = 2, {f[4j+1], f[4j]} with byte 3j+1 and
 *                      {f[4j+3], f[4j+2]} with byte 3j+2; nothing (dvalid low) with byte 3j
 *                    auto (eta = 3 only): after the n lanes, PERMUTE and 24 - n more lanes by themselves:
 *                      n = 17 gives all 192 bytes of PRF_3 from one word
 *                  ntt = 1: SampleNTT (FIPS 203 Alg. 7, sample_ntt.v): only the accepted 12-bit coefficients
 *                    come out, packed like ByteEncode_12, on the clocks where `dvalid` is high (the last
 *                    ones up to 3 clocks after the burst). 256 coefficients = 384 bytes, then quiet.
 *                    auto: PERMUTE and another n lanes by themselves until the 256 are out
 * A block that follows by itself starts 8n + 3 + the PERMUTE time after the one before it started.
 * `busy` is high while the chip takes no command: core busy, a burst, ABSORB_N, FINISH, the automatic blocks.
 *   7 EXT          data[7:6] picks one of:
 *                    00 no-op (reserved)
 *                    01 ABSORB_N  data[5:0] = n - 1: the next n (1..64) clocks' data bytes are absorbed as
 *                                 they are, whatever the other pins say (no valid, no command needed)
 *                    10 FINISH    data[1:0] = function: 0 SHA3-256, 1 SHA3-512, 2 SHAKE128, 3 SHAKE256.
 *                                 The chip pads the block (domain byte here, zeros, 0x80 at rate - 1) and
 *                                 permutes. Busy for the padding steps + 1 + the PERMUTE time (see below).
 *                    11 MODE      data[0] = framing mode (top level): 0 legacy, 1 = no marker on uio[3:0]
 *                                 and uio[0] is the `dvalid` output
 * The eta = 3 triple position and the SampleNTT state restart at CLEAR only: SHAKE256's 136-byte rate splits a
 * triple across the PERMUTE between two BURST_CBD words, and the next burst carries on where the last stopped.
 *
 * FINISH steps from byte pointer p (0..rate-1) in the block: the rest of p's lane byte by byte, one SKIP for
 * every whole lane up to the last, and the last lane byte by byte:
 *   p's lane is the last lane:  8 - p % 8
 *   else:                       (8 - p % 8) + (rate / 8 - 2 - p / 8) + 8
 * It must not be sent with p >= rate (send PERMUTE first when the message fills the block).
 *
 * The core moves K bits per step. K = 8: one step per byte. K = 16: the adapter pairs bytes (absorb: the
 * first byte waits in a register; squeeze: the high byte waits one clock), so the host protocol is the same
 * for every K. Only the core's busy times change (PERMUTE M + 24 (2M - 1), CLEAR M, M = 64 / K).
 */

`default_nettype none

module hash_io #(
    parameter K = 8                     // core step width: 8 or 16
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         cmd_valid,      // a hash word this clock
    input  wire [2:0]   cmd,
    input  wire [7:0]   din,
    output reg  [7:0]   dout,           // to uo_out, 0 when no byte is due
    output wire         dvalid,         // a byte is on dout this clock
    output wire         busy,           // the chip takes no command this clock
    output reg          mode,           // framing mode (MODE command), 0 after reset
    output reg          c_valid,        // to the core
    output reg  [2:0]   c_cmd,
    output reg  [K-1:0] c_din,
    input  wire [K-1:0] c_dout,
    input  wire         c_busy
);
  localparam ABSORB = 3'd0, SQUEEZE = 3'd1, PERMUTE = 3'd2, CLEAR = 3'd3, SKIP = 3'd4,
             BURST = 3'd5, BURST_CBD = 3'd6, EXT = 3'd7;

  reg  [7:0] burst_left;               // bytes still to stream after this clock
  reg        cbd_on, ntt_on, eta3;     // mode of the running burst
  reg  [6:0] raw_left;                 // ABSORB_N: data bytes still to take
  reg        fin_on, fin_dom, fin_end; // FINISH: running, domain byte done, PERMUTE is next
  reg  [1:0] fin_alg;
  reg  [7:0] bptr;                     // byte pointer in the block (the core's ptr, in bytes)

  // blocks that follow a burst by themselves: 3 clocks after its last byte (the sampler has counted it by
  // then) decide; if another block is due, PERMUTE, wait for the core, burst ap_n lanes again
  reg  [1:0] ap_gap;                   // clocks to the decision, 0 = none pending
  reg        ap_wait;                  // PERMUTE sent, waiting for the core
  reg        ap_inf;                   // SampleNTT: until the polynomial is complete
  reg  [2:0] ap_cnt;                   // else: blocks still to follow
  reg  [4:0] ap_n;                     // their length in lanes
  wire       s_done;                   // sample_ntt has its 256 coefficients

  wire       bursting = (burst_left != 8'd0);
  wire       raw_on   = (raw_left != 7'd0);
  wire       ap_on    = (ap_gap != 2'd0) | ap_wait;
  wire       ap_more  = ap_inf ? ~s_done : (ap_cnt != 3'd0);
  wire       ap_perm  = (ap_gap == 2'd1) & ap_more;            // the PERMUTE goes to the core this clock
  wire       restart  = ap_wait & ~c_busy;                     // the next block starts this clock
  wire       hv       = cmd_valid & ~bursting & ~raw_on & ~fin_on & ~ap_on;   // a host command this clock
  wire       start    = hv & ((cmd == BURST) | (cmd == BURST_CBD));
  wire       host_sq  = hv & (cmd == SQUEEZE);
  wire       sreq     = host_sq | start | restart | bursting;  // a byte is asked for this clock
  wire       st_cbd   = (cmd == BURST_CBD) & ~din[6];
  wire       st_ntt   = (cmd == BURST_CBD) & din[6];
  wire       in_burst = bursting | restart;
  wire       cbd_req  = start ? st_cbd : (in_burst & cbd_on);
  wire       ntt_req  = start ? st_ntt : (in_burst & ntt_on);
  wire       eta_req  = start ? din[7] : eta3;
  wire       ext      = hv & (cmd == EXT);
  wire       ext_raw  = ext & (din[7:6] == 2'b01);
  wire       ext_fin  = ext & (din[7:6] == 2'b10);
  wire       ext_mode = ext & (din[7:6] == 2'b11);

  // ---------------- FINISH: the padding steps, then PERMUTE ----------------
  wire [4:0] fin_last = (fin_alg == 2'd1) ? 5'd8 : (fin_alg == 2'd2) ? 5'd20 : 5'd16;   // last lane of the rate
  wire       fin_at   = (bptr >= {fin_last, 3'b111});          // at (or, misused, past) byte rate - 1
  wire       fin_skip = fin_dom & (bptr[2:0] == 3'd0) & (bptr[7:3] < fin_last);
  wire [2:0] f_cmd    = fin_end ? PERMUTE : fin_skip ? SKIP : ABSORB;
  wire [7:0] f_din    = {fin_at, 2'b00, (fin_dom ? 5'h00 : fin_alg[1] ? 5'h1F : 5'h06)};

  // ---------------- the word that reaches the core side: host, ABSORB_N data or a FINISH step -------------
  wire       a_valid = fin_on | raw_on | ap_perm |
                       (hv & ((cmd == ABSORB) | (cmd == PERMUTE) | (cmd == CLEAR) | (cmd == SKIP)));
  wire [2:0] a_cmd   = fin_on ? f_cmd : ap_perm ? PERMUTE : raw_on ? ABSORB : cmd;
  wire [7:0] a_din   = fin_on ? f_din : din;
  wire       a_abs   = a_valid & (a_cmd == ABSORB);
  wire       a_clear = a_valid & (a_cmd == CLEAR);
  wire       a_rew   = a_valid & ((a_cmd == PERMUTE) | (a_cmd == CLEAR));
  wire       a_skip  = a_valid & (a_cmd == SKIP);

  reg        bph;                      // K = 16: byte phase within the core step
  reg  [7:0] lo, hi;                   // K = 16: absorb low byte / squeeze high byte
  reg        sq_q, hi_q;               // a byte is due this clock: from c_dout / from hi

  // ---------------- requests to the core (same clock as the host word) ----------------
  always @(*) begin
    c_valid = 1'b0;
    c_cmd   = a_cmd;
    c_din   = {K{1'b0}};
    if (sreq) begin
      c_cmd   = SQUEEZE;
      c_valid = (K == 8) ? 1'b1 : ~bph;
    end else if (a_abs) begin
      c_valid = (K == 8) ? 1'b1 : bph;
      if (K == 8) c_din = a_din;
      else        c_din = {a_din, lo};
    end else if (a_valid) begin
      c_valid = 1'b1;                  // PERMUTE, CLEAR, SKIP
    end
  end

  // ---------------- control ----------------
  always @(posedge clk) begin
    if (!rst_n) begin
      burst_left <= 8'd0;
      cbd_on <= 1'b0;
      ntt_on <= 1'b0;
      eta3 <= 1'b0;
      raw_left <= 7'd0;
      fin_on <= 1'b0;
      fin_dom <= 1'b0;
      fin_end <= 1'b0;
      fin_alg <= 2'd0;
      bptr <= 8'd0;
      mode <= 1'b0;
      ap_gap <= 2'd0;
      ap_wait <= 1'b0;
      ap_inf <= 1'b0;
      ap_cnt <= 3'd0;
      ap_n <= 5'd0;
      bph <= 1'b0;
      sq_q <= 1'b0;
      hi_q <= 1'b0;
    end else begin
      if (start) begin
        burst_left <= {din[4:0], 3'b000} - 8'd1;
        cbd_on <= st_cbd;
        ntt_on <= st_ntt;
        eta3 <= din[7];
        // what follows by itself: BURST x blocks; CBD eta 3 the rest of the 24 lanes; SampleNTT until done
        ap_inf <= st_ntt & din[5];
        ap_cnt <= (cmd == BURST) ? din[7:5] : (st_cbd & din[7] & din[5]) ? 3'd1 : 3'd0;
        ap_n   <= st_cbd ? 5'd24 - din[4:0] : din[4:0];
      end else if (restart) begin
        burst_left <= {ap_n, 3'b000} - 8'd1;
      end else if (bursting) begin
        burst_left <= burst_left - 8'd1;
      end

      if (bursting & (burst_left == 8'd1) & (ap_inf | (ap_cnt != 3'd0))) ap_gap <= 2'd3;
      else if (ap_gap != 2'd0) ap_gap <= ap_gap - 2'd1;
      if (ap_gap == 2'd1) begin                      // the decision
        if (ap_more) begin
          ap_wait <= 1'b1;
          if (~ap_inf) ap_cnt <= ap_cnt - 3'd1;
        end else ap_inf <= 1'b0;
      end else if (restart) ap_wait <= 1'b0;

      if (ext_raw) raw_left <= {1'b0, din[5:0]} + 7'd1;
      else if (raw_on) raw_left <= raw_left - 7'd1;

      if (ext_fin) begin
        fin_on  <= 1'b1;
        fin_dom <= 1'b0;
        fin_end <= 1'b0;
        fin_alg <= din[1:0];
      end else if (fin_on) begin
        if (fin_end) fin_on <= 1'b0;                 // this clock sends the PERMUTE
        else begin
          fin_dom <= 1'b1;
          if (~fin_skip & fin_at) fin_end <= 1'b1;   // the 0x80 byte goes in now
        end
      end

      if (ext_mode) mode <= din[0];

      if (a_rew) bptr <= 8'd0;
      else if (a_skip) bptr <= {bptr[7:3], 3'b111} + 8'd1;
      else if (a_abs | sreq) bptr <= bptr + 8'd1;

      if (K == 8) begin
        sq_q <= sreq;
        hi_q <= 1'b0;
      end else begin
        sq_q <= sreq & ~bph;
        hi_q <= sreq & bph;
        if (sreq | a_abs) bph <= ~bph;
        else if (a_rew | a_skip) bph <= 1'b0;
      end
    end
  end

  generate
    if (K != 8) begin : g_pair
      always @(posedge clk) begin
        if (a_abs & ~bph) lo <= a_din;
        if (sq_q) hi <= c_dout[K-1:8];
      end
    end else begin : g_byte
      always @(*) begin
        lo = 8'h00;
        hi = 8'h00;
      end
    end
  endgenerate

  // ---------------- output: raw byte, CBD coefficients or SampleNTT ----------------
  reg       cbd_q, ntt_q, eta_q;       // mode of the byte due this clock
  reg [1:0] p3;                        // eta = 3: byte position in its triple
  reg [3:0] f0;                        // eta = 3, packed: f[4j], sent with byte 3j+1
  reg [1:0] left2;                     // eta = 3: bits 7:6 of byte 3j
  reg [3:0] left4;                     // eta = 3: bits 7:4 of byte 3j+1
  wire      due = sq_q | hi_q;
  wire [7:0] raw = hi_q ? hi : c_dout[7:0];

  function [3:0] cbd2(input [3:0] x);
    cbd2 = ({3'b0, x[0]} + {3'b0, x[1]}) - ({3'b0, x[2]} + {3'b0, x[3]});
  endfunction
  function [3:0] cbd3(input [5:0] x);
    cbd3 = ({3'b0, x[0]} + {3'b0, x[1]} + {3'b0, x[2]}) - ({3'b0, x[3]} + {3'b0, x[4]} + {3'b0, x[5]});
  endfunction

  always @(posedge clk) begin
    if (!rst_n) begin
      cbd_q <= 1'b0;
      ntt_q <= 1'b0;
      eta_q <= 1'b0;
      p3 <= 2'd0;
    end else begin
      cbd_q <= sreq & cbd_req;
      ntt_q <= sreq & ntt_req;
      eta_q <= eta_req;
      if (a_clear) p3 <= 2'd0;                                  // a triple may straddle a PERMUTE
      else if (due & cbd_q & eta_q) p3 <= (p3 == 2'd2) ? 2'd0 : p3 + 2'd1;
      if (due & cbd_q & eta_q & (p3 == 2'd0)) left2 <= raw[7:6];
      if (due & cbd_q & eta_q & (p3 == 2'd0)) f0 <= cbd3(raw[5:0]);
      if (due & cbd_q & eta_q & (p3 == 2'd1)) left4 <= raw[7:4];
    end
  end

  wire [7:0] s_o;
  wire       s_ov;
  sample_ntt u_ntt (
      .clk  (clk),
      .rst_n(rst_n),
      .clear(a_clear),
      .v    (due & ntt_q),
      .b    (raw),
      .o    (s_o),
      .ov   (s_ov),
      .done (s_done)
  );

  wire pack3   = mode & cbd_q & eta_q;                         // eta = 3 in mode 1: two bytes per triple
  wire due_out = due & ~ntt_q & ~(pack3 & (p3 == 2'd0));       // a raw or CBD byte goes out this clock
  assign dvalid = due_out | s_ov;
  assign busy   = c_busy | bursting | raw_on | fin_on | ap_on | s_ov;

  always @(*) begin
    if (s_ov)            dout = s_o;
    else if (!due_out)   dout = 8'h00;
    else if (!cbd_q)     dout = raw;
    else if (!eta_q)     dout = {cbd2(raw[7:4]), cbd2(raw[3:0])};
    else case (p3)
      2'd0:    dout = {4'h0, cbd3(raw[5:0])};
      2'd1:    dout = pack3 ? {cbd3({raw[3:0], left2}), f0} : {4'h0, cbd3({raw[3:0], left2})};
      default: dout = {cbd3(raw[7:2]), cbd3({raw[1:0], left4})};
    endcase
  end
endmodule
