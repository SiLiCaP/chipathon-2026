/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * modq_mul (Spec 2): p_out = (2197 * a_in * b_in) mod 3329, 2197 = 13^3.
 *
 * The datapath is Maksims Latkovskis' modmul_kred (branch feature/multiplier),
 * fitted to the Spec 2 ports: start / p_valid instead of in_valid / out_valid,
 * flush, ready, synchronous reset, p_out held between results.
 *
 * Three pipeline stages, one result per clock, latency start -> p_valid = 3:
 *   S1: C = a * b                           24 bits, C < 3329^2 < 2^24
 *   S2: two K-RED rounds on the bytes of C:
 *       t = 169 * C0 - 13 * C1 + C2         = 13^2 * C (mod q), -3315..43264
 *   S3: one more K-RED round and the correction:
 *       u = 13 * t0 - t1                    = 13^3 * C (mod q), -169..3328
 *       p = u < 0 ? u + q : u
 * K-RED works because 3329 = 13 * 2^8 + 1, so 13 * 2^8 = -1 (mod q).
 */

`default_nettype none

module modq_mul (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire [11:0] a_in,
    input  wire [11:0] b_in,
    input  wire        flush,
    output reg  [11:0] p_out,
    output reg         p_valid,
    output wire        ready
);

  // S1: plain 12 x 12 product
  reg  [23:0] c_s1;
  reg         v_s1;

  // S2: double K-RED, t = 169*C0 - 13*C1 + C2 (17-bit two's complement)
  wire [7:0]  c0      = c_s1[7:0];
  wire [7:0]  c1      = c_s1[15:8];
  wire [7:0]  c2      = c_s1[23:16];                              // 0..169
  wire [15:0] c0_x169 = {c0, 7'd0} + {3'd0, c0, 5'd0} + {5'd0, c0, 3'd0} + {8'd0, c0};
  wire [11:0] c1_x13  = {1'b0, c1, 3'd0} + {2'd0, c1, 2'd0} + {4'd0, c1};
  wire [16:0] t_w     = {1'b0, c0_x169} + {9'd0, c2} - {5'd0, c1_x13};
  reg  [16:0] t_s2;
  reg         v_s2;

  // S3: third K-RED, u = 13*t0 - t1 (t1 signed), then +q if negative
  wire [7:0]  t0      = t_s2[7:0];
  wire [8:0]  t1      = t_s2[16:8];                               // signed -13..169
  wire [11:0] t0_x13  = {1'b0, t0, 3'd0} + {2'd0, t0, 2'd0} + {4'd0, t0};
  wire [13:0] u       = {2'b00, t0_x13} - {{5{t1[8]}}, t1};
  wire [13:0] u_fix   = u + 14'd3329;
  wire [11:0] p_w     = u[13] ? u_fix[11:0] : u[11:0];

  assign ready = 1'b1;   // a start is accepted on every clock

  always @(posedge clk) begin
    if (!rst_n || flush) begin
      v_s1    <= 1'b0;
      v_s2    <= 1'b0;
      p_valid <= 1'b0;
      if (!rst_n) begin
        c_s1  <= 24'd0;
        t_s2  <= 17'd0;
        p_out <= 12'd0;
      end
    end else begin
      c_s1    <= a_in * b_in;
      v_s1    <= start;
      t_s2    <= t_w;
      v_s2    <= v_s1;
      p_valid <= v_s2;
      if (v_s2) p_out <= p_w;
    end
  end

  wire _unused = &{u_fix[13:12], 1'b0};

endmodule
