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
 * Sending starts on a clock edge where start is high: b' and a' are sampled
 * and byte 1 goes out on that edge. last is high during the clock before
 * byte 3 goes out. When no bytes are being sent, data_out is 0.
 */
module byte_serializer (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire [11:0] b_prime,
    input  wire [11:0] a_prime,
    output wire [7:0]  data_out,
    output wire        last
);

  reg [23:0] shift;  // byte now on data_out is in [7:0], the rest wait above it
  reg [1:0]  left;   // how many bytes are still waiting after the one on data_out

  always @(posedge clk) begin
    if (!rst_n) begin
      shift <= 24'd0;
      left  <= 2'd0;
    end else if (start) begin
      shift <= {a_prime, b_prime};  // load the pair, byte 1 goes out
      left  <= 2'd2;
    end else begin
      shift <= shift >> 8;      // move the next byte down into [7:0], 0 once all 3 are out
      if (left != 2'd0) begin
        left <= left - 2'd1;
      end
    end
  end

  assign data_out = shift[7:0];
  assign last     = (left == 2'd1);

endmodule
