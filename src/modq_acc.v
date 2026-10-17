/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * modq_acc (Spec 4): running sum mod 3329. ld: acc <= y, add: acc <=
 * (acc + y) mod 3329, ld wins over add, neither: hold. Synchronous reset
 * clears acc. acc is valid 1 clock after ld / add.
 *
 * Same correction as modq_addsub: q = 0xD01, so s - q is the low byte minus 1
 * and the high nibble minus 13. In sky130 this is about as small as the
 * textbook s - 3329 form and about 1.3 ns shorter at the slow corner.
 */

`default_nettype none

module modq_acc (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        ld,
    input  wire        add,
    input  wire [11:0] y,
    output reg  [11:0] acc
);

  wire [12:0] s    = {1'b0, acc} + {1'b0, y};          // 0..6656
  wire        s_bz = ~|s[7:0];
  wire        s_ge = (s[12:8] > 5'd13) | ((s[12:8] == 5'd13) & ~s_bz);
  wire [11:0] s_m  = {s[11:8] - 4'd13 - {3'd0, s_bz}, s[7:0] - 8'd1};

  always @(posedge clk) begin
    if (!rst_n)   acc <= 12'd0;
    else if (ld)  acc <= y;
    else if (add) acc <= s_ge ? s_m : s[11:0];
  end

endmodule
