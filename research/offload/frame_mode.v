// Option M (mode register) + O (uio direction): cmd 7 with data[7]=1 sets mode; in mode 1 the marker pins uio[3:0]
// are outputs (wide read path) and the command is {valid, cmd[2:0]} on uio[7:4]
module frame_mode(input clk, input rst_n, input [7:0] ui, input [7:0] uio_in, input [3:0] wide_hi,
                  output [7:0] uio_oe, output [7:0] uio_out, output cmd_valid, output [2:0] cmd, output mode);
  reg m;
  wire legacy_v = uio_in[4] & (uio_in[3:0] == 4'hF);
  wire [2:0] legacy_c = {uio_in[5], uio_in[7:6]};
  wire new_v = uio_in[4]; wire [2:0] new_c = {uio_in[5], uio_in[7:6]};
  assign cmd_valid = m ? new_v : legacy_v;
  assign cmd = m ? new_c : legacy_c;
  always @(posedge clk) if (!rst_n) m <= 0; else if (legacy_v & legacy_c == 3'd7 & ui[7]) m <= ui[6];
  assign mode = m; assign uio_oe = {4'b0, {4{m}}}; assign uio_out = {4'b0, wide_hi};
endmodule
