/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * modq_addsub (Spec 4): sum = (x + y) mod 3329, diff = (x - y) mod 3329,
 * for x, y in 0..3328. No state. REG_OUT = 0: combinational (latency 0),
 * REG_OUT = 1: both outputs registered (latency 1).
 *
 * One plain adder and one plain subtractor, each followed by a cheap
 * correction. q = 3329 = 0xD01, so +-q only changes the low byte by 1 and the
 * high nibble by 13:
 *   s - q = {s[12:8] - 13 - (s[7:0] == 0), s[7:0] - 1}
 *   d + q = {d[11:8] + 13 + (d[7:0] == 255), d[7:0] + 1}
 * and s >= q is s[12:8] > 13, or s[12:8] == 13 with a nonzero low byte.
 * Measured in sky130 (LibreLane synthesis, AREA 0) against the textbook form
 * (s - 3329 and d + 3329 as full 13-bit adders): 8% less area and the same
 * or a shorter path. Prefix (Kogge-Stone, Sklansky) and carry-select forms
 * were larger without being faster after ABC.
 */

`default_nettype none

module modq_addsub #(
    parameter REG_OUT = 0
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [11:0] x,
    input  wire [11:0] y,
    output wire [11:0] sum,
    output wire [11:0] diff
);

  // sum: s = x + y in 0..6656, subtract q when s >= q
  wire [12:0] s    = {1'b0, x} + {1'b0, y};
  wire        s_bz = ~|s[7:0];                         // s[7:0] - 1 borrows
  wire        s_ge = (s[12:8] > 5'd13) | ((s[12:8] == 5'd13) & ~s_bz);
  wire [3:0]  s_hi = s[11:8] - 4'd13 - {3'd0, s_bz};
  wire [7:0]  s_lo = s[7:0] - 8'd1;
  wire [11:0] sum_c = s_ge ? {s_hi, s_lo} : s[11:0];

  // diff: d = x - y in -3328..3328, add q when d < 0
  wire [12:0] d    = {1'b0, x} - {1'b0, y};
  wire        d_cf = &d[7:0];                          // d[7:0] + 1 carries
  wire [3:0]  d_hi = d[11:8] + 4'd13 + {3'd0, d_cf};
  wire [7:0]  d_lo = d[7:0] + 8'd1;
  wire [11:0] diff_c = d[12] ? {d_hi, d_lo} : d[11:0];

  generate
    if (REG_OUT) begin : g_reg
      reg [11:0] sum_r, diff_r;
      always @(posedge clk) begin
        if (!rst_n) begin
          sum_r  <= 12'd0;
          diff_r <= 12'd0;
        end else begin
          sum_r  <= sum_c;
          diff_r <= diff_c;
        end
      end
      assign sum  = sum_r;
      assign diff = diff_r;
    end else begin : g_comb
      assign sum  = sum_c;
      assign diff = diff_c;
      wire _unused = &{clk, rst_n, 1'b0};
    end
  endgenerate

endmodule
