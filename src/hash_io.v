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
 *   5 BURST        data = n (1..21): stream n lanes = 8n bytes, one per clock, from this one word.
 *                  The host sends idle clocks (valid low) meanwhile; hash words during a burst are ignored.
 *   6 BURST_CBD    data = {eta3, 2'b0, n}: like BURST, but each byte goes through SamplePolyCBD (FIPS 203
 *                  Alg. 8) on the way out, as 4-bit two's complement coefficients:
 *                    eta = 2: byte -> {f[2i+1], f[2i]}               (two coefficients per byte)
 *                    eta = 3: bytes 3j, 3j+1, 3j+2 -> {0, f[4j]}, {0, f[4j+1]}, {f[4j+3], f[4j+2]}
 *   7 (reserved)   no-op
 * The eta = 3 triple position restarts at CLEAR only: SHAKE256's 136-byte rate splits a triple across the
 * PERMUTE between two BURST_CBD words, and the next burst carries on where the last one stopped.
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
    output reg          c_valid,        // to the core
    output reg  [2:0]   c_cmd,
    output reg  [K-1:0] c_din,
    input  wire [K-1:0] c_dout
);
  localparam ABSORB = 3'd0, SQUEEZE = 3'd1, PERMUTE = 3'd2, CLEAR = 3'd3, SKIP = 3'd4,
             BURST = 3'd5, BURST_CBD = 3'd6;

  reg  [7:0] burst_left;               // bytes still to stream after this clock
  reg        cbd_on, eta3;             // mode of the running burst
  wire       bursting = (burst_left != 8'd0);
  wire       start    = cmd_valid & ~bursting & ((cmd == BURST) | (cmd == BURST_CBD));
  wire       host_sq  = cmd_valid & ~bursting & (cmd == SQUEEZE);
  wire       sreq     = host_sq | start | bursting;            // a byte is asked for this clock
  wire       cbd_req  = bursting ? cbd_on : (start & (cmd == BURST_CBD));
  wire       eta_req  = bursting ? eta3 : din[7];

  reg        bph;                      // K = 16: byte phase within the core step
  reg  [7:0] lo, hi;                   // K = 16: absorb low byte / squeeze high byte
  reg        sq_q, hi_q;               // a byte is due this clock: from c_dout / from hi

  // ---------------- requests to the core (same clock as the host word) ----------------
  always @(*) begin
    c_valid = 1'b0;
    c_cmd   = cmd;
    c_din   = {K{1'b0}};
    if (sreq) begin
      c_cmd   = SQUEEZE;
      c_valid = (K == 8) ? 1'b1 : ~bph;
    end else if (cmd_valid & ~bursting) begin
      case (cmd)
        ABSORB: begin
          c_valid = (K == 8) ? 1'b1 : bph;
          if (K == 8) c_din = din;
          else        c_din = {din, lo};
        end
        PERMUTE, CLEAR, SKIP: c_valid = 1'b1;
        default: c_valid = 1'b0;
      endcase
    end
  end

  // ---------------- control ----------------
  always @(posedge clk) begin
    if (!rst_n) begin
      burst_left <= 8'd0;
      cbd_on <= 1'b0;
      eta3 <= 1'b0;
      bph <= 1'b0;
      sq_q <= 1'b0;
      hi_q <= 1'b0;
    end else begin
      if (start) begin
        burst_left <= {din[4:0], 3'b000} - 8'd1;
        cbd_on <= (cmd == BURST_CBD);
        eta3 <= din[7];
      end else if (bursting) begin
        burst_left <= burst_left - 8'd1;
      end
      if (K == 8) begin
        sq_q <= sreq;
        hi_q <= 1'b0;
      end else begin
        sq_q <= sreq & ~bph;
        hi_q <= sreq & bph;
        if (sreq) bph <= ~bph;
        else if (cmd_valid & ~bursting & (cmd == ABSORB)) bph <= ~bph;
        else if (cmd_valid & ~bursting & ((cmd == PERMUTE) | (cmd == CLEAR) | (cmd == SKIP))) bph <= 1'b0;
      end
    end
  end

  generate
    if (K != 8) begin : g_pair
      always @(posedge clk) begin
        if (cmd_valid & ~bursting & (cmd == ABSORB) & ~bph) lo <= din;
        if (sq_q) hi <= c_dout[K-1:8];
      end
    end else begin : g_byte
      always @(*) begin
        lo = 8'h00;
        hi = 8'h00;
      end
    end
  endgenerate

  // ---------------- output: raw byte or CBD coefficients ----------------
  reg       cbd_q, eta_q;              // mode of the byte due this clock
  reg [1:0] p3;                        // eta = 3: byte position in its triple
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
      eta_q <= 1'b0;
      p3 <= 2'd0;
    end else begin
      cbd_q <= sreq & cbd_req;
      eta_q <= eta_req;
      if (cmd_valid & ~bursting & (cmd == CLEAR)) p3 <= 2'd0;   // a triple may straddle a PERMUTE
      else if (due & cbd_q & eta_q) p3 <= (p3 == 2'd2) ? 2'd0 : p3 + 2'd1;
      if (due & cbd_q & eta_q & (p3 == 2'd0)) left2 <= raw[7:6];
      if (due & cbd_q & eta_q & (p3 == 2'd1)) left4 <= raw[7:4];
    end
  end

  always @(*) begin
    if (!due)            dout = 8'h00;
    else if (!cbd_q)     dout = raw;
    else if (!eta_q)     dout = {cbd2(raw[7:4]), cbd2(raw[3:0])};
    else case (p3)
      2'd0:    dout = {4'h0, cbd3(raw[5:0])};
      2'd1:    dout = {4'h0, cbd3({raw[3:0], left2})};
      default: dout = {cbd3(raw[7:2]), cbd3({raw[1:0], left4})};
    endcase
  end
endmodule
