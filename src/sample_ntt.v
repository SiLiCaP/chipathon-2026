/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sample_ntt: FIPS 203 SampleNTT (Algorithm 7) on the squeeze stream, with the accepted coefficients packed
 * like ByteEncode_12.
 *
 * In: the XOF bytes, one per `v` clock. Every three bytes C0, C1, C2 give two candidates
 *   d1 = C0 + 256 (C1 mod 16),  d2 = C1 / 16 + 16 C2,
 * and a candidate below q = 3329 is accepted, d1 before d2, until 256 are accepted.
 * Out: each pair of accepted coefficients (x, y) leaves as three bytes x[7:0], {y[3:0], x[11:8]}, y[11:4] on
 * the three clocks after the byte that completed the pair (`ov` high). A polynomial is 384 such bytes, exactly
 * ByteEncode_12 of it. After the 256th coefficient the block stays quiet until `clear`.
 *
 * The output rate depends on the data, which is why the chip has a valid pin for it (hash_io.v). A triple's
 * three output clocks end before the next triple can complete, so nothing is ever dropped. The byte position
 * in a triple and a pending odd coefficient carry on across bursts and PERMUTE; only `clear` restarts them.
 */

`default_nettype none

module sample_ntt (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       clear,          // restart: a new polynomial
    input  wire       v,              // b is the next XOF byte
    input  wire [7:0] b,
    output wire [7:0] o,              // packed byte, valid when ov
    output wire       ov,
    output wire       done            // 256 coefficients accepted
);
  reg [1:0]  ph;                      // byte position in the triple
  reg [7:0]  c0, c1;
  reg [11:0] acc;                     // an accepted coefficient waiting for its pair
  reg        na;                      // acc is in use
  reg [8:0]  cnt;                     // coefficients accepted, 0..256
  reg [23:0] ob;                      // bytes still to send, low byte first
  reg [1:0]  oc;                      // how many

  wire [11:0] d1 = {c1[3:0], c0};
  wire [11:0] d2 = {b, c1[7:4]};
  wire        a1 = (d1 < 12'd3329);
  wire        a2 = (d2 < 12'd3329);
  wire        full = cnt[8];
  wire        trip = v & ~full & (ph == 2'd2);       // the triple is complete this clock
  wire [11:0] first = a1 ? d1 : d2;                  // the first accepted candidate
  wire        any  = a1 | a2;
  wire        both = a1 & a2;
  wire        one_left = (cnt == 9'd255);            // then na is set: only one more coefficient is taken

  assign o    = ob[7:0];
  assign ov   = (oc != 2'd0);
  assign done = full;

  always @(posedge clk) begin
    if (!rst_n || clear) begin
      ph  <= 2'd0;
      na  <= 1'b0;
      cnt <= 9'd0;
      oc  <= 2'd0;
    end else begin
      if (oc != 2'd0) begin
        oc <= oc - 2'd1;
        ob <= {8'h00, ob[23:8]};
      end
      if (v & ~full) begin
        ph <= (ph == 2'd2) ? 2'd0 : ph + 2'd1;
        if (ph == 2'd0) c0 <= b;
        if (ph == 2'd1) c1 <= b;
      end
      if (trip & any) begin
        if (na) begin                                // the waiting coefficient gets its pair
          ob <= {first, acc};
          oc <= 2'd3;
          if (both & ~one_left) begin                // d2 is the next one waiting
            acc <= d2;
            cnt <= cnt + 9'd2;
          end else begin
            na  <= 1'b0;
            cnt <= cnt + 9'd1;
          end
        end else if (both) begin
          ob  <= {d2, d1};
          oc  <= 2'd3;
          cnt <= cnt + 9'd2;
        end else begin
          acc <= first;
          na  <= 1'b1;
          cnt <= cnt + 9'd1;
        end
      end
    end
  end
endmodule
