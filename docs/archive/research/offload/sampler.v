// SampleNTT rejection sampler + 12-bit packer: bytes in (3 per 2 candidates), packed accepted coefficients out
module sampler(input clk, input rst_n, input start, input v, input [7:0] b, output reg [7:0] o, output reg ov,
               output reg [8:0] cnt, output done);
  reg [1:0] ph; reg [7:0] c0, c1; reg [23:0] acc; reg [1:0] na; reg [1:0] oc; reg [23:0] ob;
  wire [11:0] d1 = {c1[3:0], c0}; wire [11:0] d2 = {b, c1[7:4]};
  wire a1 = d1 < 12'd3329, a2 = d2 < 12'd3329;
  assign done = cnt >= 9'd256;
  always @(posedge clk) begin
    if (!rst_n | start) begin ph <= 0; cnt <= 0; na <= 0; oc <= 0; ov <= 0; end
    else begin
      ov <= 0;
      if (oc != 0) begin o <= ob[7:0]; ob <= {8'b0, ob[23:8]}; ov <= 1; oc <= oc - 1; end
      if (v & ~done) case (ph)
        0: begin c0 <= b; ph <= 1; end
        1: begin c1 <= b; ph <= 2; end
        2: begin ph <= 0;
             // accepted candidates go into a 12/24-bit staging buffer; every 2 accepted -> 3 bytes
             if (a1 & a2) begin ob <= {d2, d1}; oc <= 3; cnt <= cnt + 2; end
             else if (a1 | a2) begin
               if (na == 0) begin acc[11:0] <= a1 ? d1 : d2; na <= 1; cnt <= cnt + 1; end
               else begin ob <= {a1 ? d1 : d2, acc[11:0]}; oc <= 3; na <= 0; cnt <= cnt + 1; end
             end
           end
      endcase
    end
  end
endmodule
