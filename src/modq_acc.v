/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PLACEHOLDER for the Adders owner's block (Spec 4). Same ports as the spec;
 * replace this file with the owner's modq_acc.v.
 *
 * Running sum mod 3329: ld -> acc = y, add -> acc = (acc + y) mod 3329,
 * ld wins over add, neither -> hold. acc is valid 1 clock after ld / add.
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

  wire [12:0] s  = {1'b0, acc} + {1'b0, y};
  wire [12:0] sc = s - 13'd3329;

  always @(posedge clk) begin
    if (!rst_n)   acc <= 12'd0;
    else if (ld)  acc <= y;
    else if (add) acc <= sc[12] ? s[11:0] : sc[11:0];
  end

endmodule
