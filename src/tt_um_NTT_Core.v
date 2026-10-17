/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tiny Tapeout shell (Spec 5).
 *
 *   ui_in, uio_in -> io_in_capture -> ntt_core -> io_out_pack -> uo_out
 *                                  \-> keccak_ring (hash words) ----/
 *
 * Hash words: a captured word with valid and coeff[11:8] = 0xF (never a coefficient) is a hash
 * command for keccak_ring: {sof, op} = command (0 absorb, 1 squeeze, 2 permute, 3 clear, 4 skip lane),
 * coeff[K-1:0] = data. The NTT core never sees it.
 *
 * Pins: ui_in[7:0] = coefficient bits 7:0, uio_in[3:0] = bits 11:8,
 * uio_in[4] = valid, uio_in[5] = sof, uio_in[7:6] = op. uo_out = result bytes
 * at a fixed delay, no output-valid flag. Every uio pin is an input.
 */

`default_nettype none

module tt_um_NTT_Core #(
    parameter HASH_K = 8
) (
    input  wire [7:0] ui_in,    // Dedicated inputs
    output wire [7:0] uo_out,   // Dedicated outputs
    input  wire [7:0] uio_in,   // IOs: Input path
    output wire [7:0] uio_out,  // IOs: Output path
    output wire [7:0] uio_oe,   // IOs: Enable path (active high: 0=input, 1=output)
    input  wire       ena,      // always 1 when the design is powered, so you can ignore it
    input  wire       clk,      // clock
    input  wire       rst_n     // reset_n - low to reset
);

  wire [11:0] coeff_q;
  wire        valid_q, sof_q;
  wire [1:0]  op_q;
  wire [11:0] r0, r1;
  wire        pair_valid, flush;

  io_in_capture u_in (
      .clk    (clk),
      .rst_n  (rst_n),
      .ui_in  (ui_in),
      .uio_in (uio_in),
      .coeff_q(coeff_q),
      .valid_q(valid_q),
      .sof_q  (sof_q),
      .op_q   (op_q)
  );

  wire hash_w = valid_q & (coeff_q[11:8] == 4'hF);
  wire blk_w  = valid_q & (coeff_q[11:8] == 4'hE);   // block-mode header (NTT cache)
  wire [HASH_K-1:0] hash_dout;
  wire [7:0] ntt_byte;

  keccak_ring #(.K(HASH_K)) u_hash (
      .clk      (clk),
      .rst_n    (rst_n),
      .cmd_valid(hash_w),
      .cmd      ({sof_q, op_q}),   // sof is free in hash words: bit 2 selects SKIP
      .din      (coeff_q[HASH_K-1:0]),
      .dout     (hash_dout),
      .busy     ()
  );

  ntt_core u_core (
      .clk       (clk),
      .rst_n     (rst_n),
      .coeff_q   (coeff_q),
      .valid_q   (valid_q & ~hash_w & ~blk_w),
      .sof_q     (sof_q & ~hash_w & ~blk_w),
      .op_q      (op_q),
      .blk_q     (blk_w),
      .r0        (r0),
      .r1        (r1),
      .pair_valid(pair_valid),
      .flush     (flush)
  );

  io_out_pack u_out (
      .clk       (clk),
      .rst_n     (rst_n),
      .pair_valid(pair_valid),
      .r0        (r0),
      .r1        (r1),
      .flush     (flush),
      .uo_byte   (ntt_byte)
  );

  // the idle side outputs 0
  wire [7:0] hash_byte = hash_dout;   // zero-extended
  assign uo_out = ntt_byte | hash_byte;

  // Every uio pin is an input
  assign uio_out = 8'h00;
  assign uio_oe  = 8'h00;

  // List all unused inputs to prevent warnings
  wire _unused = &{ena, 1'b0};

endmodule
