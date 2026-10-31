// Option P (packet framing): one strobe pin uio[4]; header byte on ui = {cmd[2:0], len[4:0]}; the next len+1
// bytes on ui are raw ABSORB data with no strobe; everything else idle
module packet_frame(input clk, input rst_n, input strobe, input [7:0] ui,
                    output reg cmd_valid, output reg [2:0] cmd, output reg [7:0] data, output reg [4:0] left);
  always @(posedge clk) begin
    cmd_valid <= 0;
    if (!rst_n) left <= 0;
    else if (left != 0) begin cmd_valid <= 1; cmd <= 3'd0; data <= ui; left <= left - 1; end
    else if (strobe) begin
      cmd_valid <= (ui[7:5] != 3'd0); cmd <= ui[7:5]; data <= {3'b0, ui[4:0]};
      if (ui[7:5] == 3'd0) left <= ui[4:0] + 1;
    end
  end
endmodule
