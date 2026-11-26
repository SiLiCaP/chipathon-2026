// two 256-bit seed registers (rho, sigma/r) loaded bytewise, replayed bytewise into the state, plus a nonce counter
module seed_reg(input clk, input rst_n, input ld, input sel, input [7:0] din, input rd, input nreset, input ninc,
                output [7:0] dout, output [7:0] nonce);
  reg [255:0] r0, r1; reg [7:0] n;
  always @(posedge clk) begin
    if (ld & ~sel) r0 <= {din, r0[255:8]};
    else if (rd & ~sel) r0 <= {r0[7:0], r0[255:8]};
    if (ld & sel) r1 <= {din, r1[255:8]};
    else if (rd & sel) r1 <= {r1[7:0], r1[255:8]};
    if (!rst_n | nreset) n <= 0; else if (ninc) n <= n + 1;
  end
  assign dout = sel ? r1[7:0] : r0[7:0]; assign nonce = n;
endmodule
