// FINISH: from the byte pointer, absorb domain byte, zero fill, 0x80 at rate-1; sequences core commands
module autopad(input clk, input rst_n, input go, input [1:0] alg, input dom1f, input [7:0] ptr_in,
               output reg busy, output reg [2:0] c_cmd, output reg c_v, output reg [7:0] c_din);
  reg [8:0] p; reg d;
  wire [8:0] rate = alg == 0 ? 9'd136 : alg == 1 ? 9'd72 : alg == 2 ? 9'd168 : 9'd136;
  always @(posedge clk) begin
    if (!rst_n) begin busy <= 0; c_v <= 0; end
    else if (go & ~busy) begin busy <= 1; p <= {1'b0, ptr_in}; d <= 0; end
    else if (busy) begin
      c_v <= 1; c_cmd <= 0;
      c_din <= (!d) ? (dom1f ? 8'h1F : 8'h06) : (p == rate - 1) ? 8'h80 : 8'h00;
      if (!d) begin d <= 1; if (p == rate - 1) c_din <= (dom1f ? 8'h9F : 8'h86); end
      p <= p + 1;
      if (p == rate - 1) begin busy <= 0; end
    end else c_v <= 0;
  end
endmodule
