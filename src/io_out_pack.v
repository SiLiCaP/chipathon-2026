/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * io_out_pack (Spec 5): sends a result pair r0, r1 out as three bytes, one
 * byte per clock, in ML-KEM ByteEncode12 order:
 *   clock +1 after pair_valid : r0[7:0]
 *   clock +2                  : {r1[3:0], r0[11:8]}
 *   clock +3                  : r1[11:4]
 * This is the 24-bit word {r1, r0} cut into bytes, lowest byte first.
 * uo_byte is 0x00 when no bytes are being sent. flush drops any bytes still
 * to be sent; uo_byte is 0x00 from the next clock. Latency 1 clock.
 */

`default_nettype none

module io_out_pack (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        pair_valid,
    input  wire [11:0] r0,
    input  wire [11:0] r1,
    input  wire        flush,
    output wire [7:0]  uo_byte
);

  reg [23:0] shift;  // byte now on uo_byte is in [7:0], the rest wait above it

  always @(posedge clk) begin
    if (!rst_n || flush) begin
      shift <= 24'd0;
    end else if (pair_valid) begin
      shift <= {r1, r0};        // load the pair, byte 1 goes out
    end else begin
      shift <= shift >> 8;      // next byte down into [7:0], 0 once all 3 are out
    end
  end

  assign uo_byte = shift[7:0];

`ifdef NTT_ASSERT
  // Control sends pair_valid at least 3 clocks apart (Spec 5)
  integer since_pair = 99;
  always @(posedge clk) begin
    if (rst_n && pair_valid && since_pair < 3)
      $display("%t io_out_pack ASSERT: pair_valid %0d clocks after the previous one", $time, since_pair);
    since_pair = pair_valid ? 1 : (since_pair < 99 ? since_pair + 1 : 99);
  end
`endif

endmodule
