// Sim-only DUT for checking the emulator's cycle accuracy (model: host/dut_models.py SelftestModel).
// uo_out  = ui_in ^ uio_in registered (one-cycle pipeline)
// uio_out = cycles since reset
// uio_oe  = ui_in (combinational echo)

`default_nettype none

module tt_um_emu_selftest (
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    input  wire [7:0] uio_in,
    output wire [7:0] uio_out,
    output wire [7:0] uio_oe,
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n
);
  reg [7:0] r, cnt;
  always @(posedge clk) begin
    if (!rst_n) begin
      r <= 0; cnt <= 0;
    end else begin
      r <= ui_in ^ uio_in;
      cnt <= cnt + 8'd1;
    end
  end
  assign uo_out  = r;
  assign uio_out = cnt;
  assign uio_oe  = ui_in;
  wire _unused = ena;
endmodule
