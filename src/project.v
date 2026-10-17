/*
 * Copyright (c) 2024 Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

module tt_um_NTT_Core (
    input  wire [7:0] ui_in,    // Dedicated inputs
    output wire [7:0] uo_out,   // Dedicated outputs
    input  wire [7:0] uio_in,   // IOs: Input path
    output wire [7:0] uio_out,  // IOs: Output path
    output wire [7:0] uio_oe,   // IOs: Enable path (active high: 0=input, 1=output)
    input  wire       ena,      // always 1 when the design is powered, so you can ignore it
    input  wire       clk,      // clock
    input  wire       rst_n     // reset_n - low to reset
);

  // 12-bit input word: ui_in is bits [7:0], uio_in[3:0] is bits [11:8]
  wire [11:0] Data_in = ui_in + (uio_in [3:0] << 8);

  // Which input register Data_in goes into: 0 = a, 1 = b
  wire sel_b = uio_in[4];

  // Input registers: a and b, the inputs of the NTT core
  reg [11:0] a;
  reg [11:0] b;

  always @(posedge clk) begin
    if (!rst_n) begin
      a <= 12'd0;
      b <= 12'd0;
    end else if (sel_b) begin
      b <= Data_in;
    end else begin
      a <= Data_in;
    end
  end

  // Output registers: a' and b', the outputs of the NTT core.
  // Placeholder: a' = a and b' = b until the NTT core computes them.
  reg [11:0] a_prime;
  reg [11:0] b_prime;

  always @(posedge clk) begin
    if (!rst_n) begin
      a_prime <= 12'd0;
      b_prime <= 12'd0;
    end else begin
      a_prime <= a;
      b_prime <= b;
    end
  end

  // Send b' and a' out on uo_out as 3 bytes, one byte per clock, b' first
  byte_serializer serializer (
      .clk     (clk),
      .rst_n   (rst_n),
      .b_prime (b_prime),
      .a_prime (a_prime),
      .data_out(uo_out)
  );

  // All output pins must be assigned. If not used, assign to 0.
  assign uio_out = 0;
  assign uio_oe  = 0;

  // List all unused inputs to prevent warnings
  wire _unused = &{ena, uio_in[7:5], 1'b0};

endmodule
