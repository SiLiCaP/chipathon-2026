/*
 * Copyright (c) 2024 Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

/*
 * Sends two 12-bit values b' and a' out as three bytes, one byte per clock:
 *   byte 1 = b'[7:0]
 *   byte 2 = {a'[3:0], b'[11:8]}
 *   byte 3 = a'[11:4]
 * This is the 24-bit word {a', b'} cut into bytes, lowest byte first.
 *
 * b' and a' are sampled on the clock edge that puts byte 1 out, so all three
 * bytes always belong to the same pair. The sequence repeats forever and
 * starts again at byte 1 on the first clock edge after reset.
 */
module byte_serializer (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [11:0] b_prime,
    input  wire [11:0] a_prime,
    output wire [7:0]  data_out
);

  reg [23:0] shift;  // byte now on data_out is in [7:0], the rest wait above it
  reg [1:0]  index;  // which byte is on data_out: 0 = byte 1, 1 = byte 2, 2 = byte 3

  always @(posedge clk) begin
    if (!rst_n) begin
      shift <= 24'd0;
      index <= 2'd2;            // so the first edge after reset loads a new pair
    end else if (index == 2'd2) begin
      shift <= {a_prime, b_prime};  // load the next pair, byte 1 goes out
      index <= 2'd0;
    end else begin
      shift <= shift >> 8;      // move the next byte down into [7:0]
      index <= index + 2'd1;
    end
  end

  assign data_out = shift[7:0];

endmodule
