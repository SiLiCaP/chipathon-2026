// Wiring check for the Pico 2 W + Tang Nano rig (docs/PICO_TANG_RIG.md): a stand-in "chip" with the same
// pin directions as the NTT chip, whose outputs show exactly what arrived on its inputs.
//
// On every rising clock it latches one input byte, alternating ui_in and uio_in, and drives it on uo_out.
// rst_n low clears it and restarts the alternation with ui_in. So with the Pico sampling uo_out just before
// each rising edge (the PIO convention), sample k+1 shows ui_in of word k for even k and uio_in for odd k.
// tools/rig_pincheck.py drives walking ones and zeros through it and names any bad wire.
//
//   make -C fpga prog TARGET=pico GL=0 TT_TOP=tt_um_rig_pinchk EXTRA_SRCS=$PWD/fpga/rig_pinchk.v

`default_nettype none

module tt_um_rig_pinchk (
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    input  wire [7:0] uio_in,
    output wire [7:0] uio_out,
    output wire [7:0] uio_oe,
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n
);
    reg       phase;
    reg [7:0] q;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            phase <= 1'b0;
            q <= 8'h00;
        end else begin
            phase <= ~phase;
            q <= phase ? uio_in : ui_in;
        end
    end
    assign uo_out = q;
    assign uio_out = 8'h00;
    assign uio_oe = 8'h00;
    wire _unused = ena;
endmodule
