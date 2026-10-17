`default_nettype none
`timescale 1ns / 1ps

// Whole-core testbench top: ntt_core with the arithmetic blocks, driven at its
// own ports (no pin capture / packer), plus a spare zeta_rom to read all 128.
module tb_core #(
    parameter L_MUL    = 3,
    parameter PRE_LAT  = 1,
    parameter POST_LAT = 0
) ();

  initial begin
    $dumpfile("tb_core.fst");
    $dumpvars(0, tb_core);
  end

  reg         clk;
  reg         rst_n;
  reg  [11:0] coeff_q;
  reg         valid_q;
  reg         sof_q;
  reg  [1:0]  op_q;
  wire [11:0] r0, r1;
  wire        pair_valid, flush;

  ntt_core #(.L_MUL(L_MUL), .PRE_LAT(PRE_LAT), .POST_LAT(POST_LAT)) dut (
      .clk       (clk),
      .rst_n     (rst_n),
      .coeff_q   (coeff_q),
      .valid_q   (valid_q),
      .sof_q     (sof_q),
      .op_q      (op_q),
      .r0        (r0),
      .r1        (r1),
      .pair_valid(pair_valid),
      .flush     (flush)
  );

  reg  [6:0]  rom_addr;
  wire [11:0] rom_data;
  zeta_rom rom (
      .addr(rom_addr),
      .data(rom_data)
  );

endmodule
