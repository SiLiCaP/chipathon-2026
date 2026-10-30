/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tiny Tapeout shell: the hash-only chip (branch sha3-only). The NTT core is gone; the chip is a Keccak-f[1600]
 * sponge engine for ML-KEM's SHA3-256/512 and SHAKE128/256 calls, with SamplePolyCBD on the way out.
 *
 *   ui_in, uio_in -> io_in_capture -> hash_io (byte adapter, bursts, CBD) <-> keccak_ring -> uo_out
 *
 * Pins (unchanged from sha3-4x2, so the same host code runs): ui_in[7:0] = data byte, uio_in[3:0] = 0xF marks
 * a hash word, uio_in[4] = valid, {uio_in[5], uio_in[7:6]} = command (hash_io.v lists them). uo_out = the
 * squeezed byte (or its CBD coefficients) two clocks after the word that asked for it, 0 otherwise.
 * Every uio pin is an input. Constant time: every command has a fixed length.
 */

`default_nettype none

module tt_um_NTT_Core #(
    parameter HASH_K = 8               // Keccak core step: 8 (368 clocks / permutation) or 16 (172)
) (
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    input  wire [7:0] uio_in,
    output wire [7:0] uio_out,
    output wire [7:0] uio_oe,
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n
);
  wire [11:0] coeff_q;
  wire        valid_q, sof_q;
  wire [1:0]  op_q;

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

  // hash-word flag, registered beside io_in_capture (equals valid_q & coeff_q[11:8] == 4'hF)
  wire hash_in = uio_in[4] & (uio_in[3:0] == 4'hF);
  reg  hash_w;
  always @(posedge clk) begin
    if (!rst_n) hash_w <= 1'b0;
    else        hash_w <= hash_in;
  end

  wire              c_valid;
  wire [2:0]        c_cmd;
  wire [HASH_K-1:0] c_din, c_dout;

  hash_io #(.K(HASH_K)) u_io (
      .clk      (clk),
      .rst_n    (rst_n),
      .cmd_valid(hash_w),
      .cmd      ({sof_q, op_q}),
      .din      (coeff_q[7:0]),
      .dout     (uo_out),
      .c_valid  (c_valid),
      .c_cmd    (c_cmd),
      .c_din    (c_din),
      .c_dout   (c_dout)
  );

  keccak_ring #(.K(HASH_K)) u_hash (
      .clk      (clk),
      .rst_n    (rst_n),
      .cmd_valid(c_valid),
      .cmd      (c_cmd),
      .din      (c_din),
      .dout     (c_dout),
      .busy     ()
  );

  assign uio_out = 8'h00;
  assign uio_oe  = 8'h00;

  wire _unused = &{ena, valid_q, coeff_q[11:8], 1'b0};

endmodule
