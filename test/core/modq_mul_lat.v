/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TEST ONLY: behavioral modq_mul with the ports and behavior of Spec 2 and a
 * latency set by `MUL_LAT (default 3), so ntt_core can be tested with other
 * multiplier latencies. Used by test/core instead of src/modq_mul.v.
 */

`default_nettype none

`ifndef MUL_LAT
`define MUL_LAT 3
`endif

module modq_mul (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire [11:0] a_in,
    input  wire [11:0] b_in,
    input  wire        flush,
    output reg  [11:0] p_out,
    output reg         p_valid,
    output wire        ready
);

  localparam N = `MUL_LAT;

  reg [11:0] res [0:N-1];
  reg        vld [0:N-1];
  integer    i;

  wire [31:0] prod = (2197 * a_in % 3329) * b_in % 3329;

  assign ready = 1'b1;

  always @(posedge clk) begin
    if (!rst_n || flush) begin
      for (i = 0; i < N; i = i + 1) vld[i] <= 1'b0;
      p_valid <= 1'b0;
      if (!rst_n) p_out <= 12'd0;
    end else begin
      res[0] <= prod[11:0];
      vld[0] <= start;
      for (i = 1; i < N; i = i + 1) begin
        res[i] <= res[i-1];
        vld[i] <= vld[i-1];
      end
      // stage N-1 is reached N-1 clocks after start; p_valid is the next clock
      p_valid <= vld[N-2];
      if (vld[N-2]) p_out <= res[N-2];
    end
  end

endmodule
