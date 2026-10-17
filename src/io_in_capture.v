/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * io_in_capture (Spec 5): one register stage from the pins, latency 1 clock.
 * The host drives clk and the pins together, so no synchronizer.
 */

`default_nettype none

module io_in_capture (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [7:0]  ui_in,
    input  wire [7:0]  uio_in,
    output reg  [11:0] coeff_q,   // {uio_in[3:0], ui_in}
    output reg         valid_q,   // uio_in[4]
    output reg         sof_q,     // uio_in[5] & uio_in[4]
    output reg  [1:0]  op_q       // uio_in[7:6], read by control on the sof clock only
);

  always @(posedge clk) begin
    if (!rst_n) begin
      coeff_q <= 12'd0;
      valid_q <= 1'b0;
      sof_q   <= 1'b0;
      op_q    <= 2'd0;
    end else begin
      coeff_q <= {uio_in[3:0], ui_in};
      valid_q <= uio_in[4];
      sof_q   <= uio_in[5] & uio_in[4];
      op_q    <= uio_in[7:6];
    end
  end

endmodule
