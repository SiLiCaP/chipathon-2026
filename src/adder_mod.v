// =============================

// File adds just the adder for mod

// =============================


`default_nettype none
module ks_adder #(parameter W = 8) (
    input  [W-1:0] a, b,
    output [W-1:0] s,
    output         cout
);
  localparam L = $clog2(W);
  wire [W-1:0] g [0:L];
  wire [W-1:0] p [0:L];
  assign g[0] = a & b;
  assign p[0] = a ^ b;

  genvar l, i;
  generate
    for (l = 0; l < L; l = l + 1) begin : lvl
      for (i = 0; i < W; i = i + 1) begin : bitl
        if (i >= (1 << l)) begin
          assign g[l+1][i] = g[l][i] | (p[l][i] & g[l][i-(1<<l)]);
          assign p[l+1][i] = p[l][i] & p[l][i-(1<<l)];
        end else begin
          assign g[l+1][i] = g[l][i];
          assign p[l+1][i] = p[l][i];
        end
      end
    end
  endgenerate

  assign s    = p[0] ^ {g[L][W-2:0], 1'b0};  // carry into bit i is g[L][i-1]
  assign cout = g[L][W-1];
endmodule

module adder_mod #(parameter W = 8, parameter [W-1:0] N = 200) (
  input  [W-1:0] a, b,      // both below N
  output [W-1:0] r
);
  localparam V = W + 1;     // one extra bit for the sign

  // Path 1: s = a + b
  wire [V-1:0] s;
  ks_adder #(V) add1 (.a({1'b0,a}), .b({1'b0,b}), .s(s), .cout());

  // Path 2: CSA row, then KS adder
  wire [V-1:0] x = {1'b0, a};
  wire [V-1:0] y = {1'b0, b};
  wire [V-1:0] z = ~{1'b0, N};              // ~N, V bits wide
  wire [V-1:0] cs = x ^ y ^ z;              // CSA sum word
  wire [V-1:0] cc = (x&y) | (x&z) | (y&z);  // CSA carry word
  wire [V-1:0] cw = {cc[V-2:0], 1'b1};      // shift left, put the +1 in bit 0

  wire [V-1:0] t;
  ks_adder #(V) add2 (.a(cs), .b(cw), .s(t), .cout());

  assign r = t[W] ? s[W-1:0] : t[W-1:0];    // t[W]=1 means negative
endmodule