/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_delay: free-running delay line. q(t) = d(t - N). clr empties it.
 */

`default_nettype none

module ntt_delay #(
    parameter W = 12,
    parameter N = 4
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         clr,
    input  wire [W-1:0] d,
    output wire [W-1:0] q
);

  reg [W*N-1:0] sr;

  generate
    if (N == 1) begin : g_one
      always @(posedge clk) begin
        if (!rst_n || clr) sr <= {W{1'b0}};
        else               sr <= d;
      end
    end else begin : g_many
      always @(posedge clk) begin
        if (!rst_n || clr) sr <= {(W*N){1'b0}};
        else               sr <= {sr[W*(N-1)-1:0], d};
      end
    end
  endgenerate

  assign q = sr[W*N-1 -: W];

endmodule
