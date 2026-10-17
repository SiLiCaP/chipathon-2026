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

  // Which input register Data_in goes into: 1 = b, 0 = a.
  // The words go b, a, b, a, ... starting with b after reset.
  reg load_b;

  // busy: high from the clock edge that loads a until the clock edge that puts
  // byte 3 of that pair out on uo_out. While it is high no words are accepted,
  // so a and b keep their values. It is sent out on uio_out[4] (BUSY).
  reg busy;

  reg  pair_in;   // high for one clock after a loads: a and b hold a new pair
  reg  pair_out;  // high for one clock after a' and b' load: start sending them
  wire last;      // the serializer puts byte 3 out on this clock edge

  // Input registers: a and b, the inputs of the NTT core
  reg [11:0] a;
  reg [11:0] b;

  always @(posedge clk) begin
    if (!rst_n) begin
      load_b  <= 1'b1;
      busy    <= 1'b0;
      pair_in <= 1'b0;
      a       <= 12'd0;
      b       <= 12'd0;
    end else begin
      pair_in <= 1'b0;
      if (!busy) begin
        load_b <= !load_b;
        if (load_b) begin
          b <= Data_in;
        end else begin
          a       <= Data_in;
          busy    <= 1'b1;           // b and a are both in, stop accepting words
          pair_in <= 1'b1;
        end
      end else if (last) begin
        busy <= 1'b0;                // byte 3 is out, ready for the next b
      end
    end
  end

  // Output registers: a' and b', the outputs of the NTT core.
  // Placeholder: a' = a and b' = b one clock later until the NTT core computes them.
  reg [11:0] a_prime;
  reg [11:0] b_prime;

  always @(posedge clk) begin
    if (!rst_n) begin
      a_prime  <= 12'd0;
      b_prime  <= 12'd0;
      pair_out <= 1'b0;
    end else begin
      pair_out <= pair_in;
      if (pair_in) begin
        a_prime <= a;
        b_prime <= b;
      end
    end
  end

  // Send b' and a' out on uo_out as 3 bytes, one byte per clock, b' first
  byte_serializer serializer (
      .clk     (clk),
      .rst_n   (rst_n),
      .start   (pair_out),
      .b_prime (b_prime),
      .a_prime (a_prime),
      .data_out(uo_out),
      .last    (last)
  );

  // All output pins must be assigned. If not used, assign to 0.
  // uio[4] is the BUSY output, all other uio pins are inputs.
  assign uio_out = {3'b000, busy, 4'b0000};
  assign uio_oe  = 8'b0001_0000;

  // List all unused inputs to prevent warnings
  wire _unused = &{ena, uio_in[7:4], 1'b0};

endmodule
