// Area estimate for candidate (b): a 128 x 12 working store with two read ports, two
// result write ports and a load port (pins), like ntt_blk's C but 8x deeper. Storage
// and ports only; control and the shared datapath are the same as the block engine's.
module est_halfpoly (
    input  wire        clk,
    input  wire        ld,
    input  wire [6:0]  ld_a,
    input  wire [11:0] ld_d,
    input  wire        we,
    input  wire [6:0]  wa0, wa1,
    input  wire [11:0] w0, w1,
    input  wire [6:0]  ra0, ra1,
    output wire [11:0] rd0, rd1
);
  reg [11:0] m [0:127];
  integer i;
  always @(posedge clk)
    for (i = 0; i < 128; i = i + 1)
      if (ld && ld_a == i[6:0]) m[i] <= ld_d;
      else if (we && wa0 == i[6:0]) m[i] <= w0;
      else if (we && wa1 == i[6:0]) m[i] <= w1;
  assign rd0 = m[ra0];
  assign rd1 = m[ra1];
endmodule
