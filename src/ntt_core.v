/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STUB of ntt_core (Spec 3) with the frozen ports, so the I/O shell builds and
 * lints on its own (Spec 5). Replaced by the Control owner's ntt_core.v.
 */

`default_nettype none

module ntt_core (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [11:0] coeff_q,
    input  wire        valid_q,
    input  wire        sof_q,
    input  wire [1:0]  op_q,
    output wire [11:0] r0,
    output wire [11:0] r1,
    output wire        pair_valid,
    output wire        flush
);

  assign r0         = 12'd0;
  assign r1         = 12'd0;
  assign pair_valid = 1'b0;
  assign flush      = 1'b0;

  wire _unused = &{clk, rst_n, coeff_q, valid_q, sof_q, op_q, 1'b0};

endmodule
