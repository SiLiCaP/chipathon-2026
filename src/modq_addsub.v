/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PLACEHOLDER for the Adders owner's block (Spec 4). Same ports and parameter
 * as the spec; replace this file with the owner's modq_addsub.v.
 *
 * sum = (x + y) mod 3329, diff = (x - y) mod 3329. REG_OUT = 0: combinational,
 * REG_OUT = 1: both outputs registered (latency 1).
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

  wire [12:0] s  = {1'b0, x} + {1'b0, y};
  wire [12:0] sc = s - 13'd3329;
  wire [12:0] d  = {1'b0, x} - {1'b0, y};
  wire [12:0] dc = d + 13'd3329;
  wire [11:0] sum_c  = sc[12] ? s[11:0] : sc[11:0];   // s < 3329 -> s - 3329 negative
  wire [11:0] diff_c = d[12] ? dc[11:0] : d[11:0];    // x < y -> add 3329
  wire        _unused_dc = dc[12];

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
