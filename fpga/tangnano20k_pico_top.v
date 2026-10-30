// Tang Nano 20K as a stand-in for the Tiny Tapeout chip, driven pin for pin by a Raspberry Pi Pico 2 W
// (RP2350, the same chip as on the TT demo board) through jumper wires. Wiring: docs/PICO_TANG_RIG.md.
//
// Nothing between the header pins and the design: ui_in, uio_in, clk and rst_n come straight from the
// Pico, uo_out goes straight back. The Pico runs the team's RP2350 firmware (rp2350/,
// TT_TARGET=pico2w_tang) with the same PIO lockstep it uses on the demo board. The chip clock arrives on
// GCLKT_1 (pin 77) and drives the design through a global buffer. The 27 MHz oscillator only runs the LEDs.
//
// FPGA-only code: lives in fpga/, never listed in info.yaml.

`default_nettype none

`ifndef TT_TOP
`define TT_TOP tt_um_NTT_Core
`endif

module tangnano20k_pico_top (
    input  wire       clk,        // 27 MHz board oscillator: LEDs only
    input  wire       tt_clk,     // chip clock from the Pico (GPIO 21)
    input  wire       tt_rst_n,   // from the Pico (GPIO 22); pulled down: in reset while unwired
    input  wire [7:0] tt_ui,      // ui_in[7:0] from the Pico (GPIO 0-7)
    input  wire [7:0] tt_uio,     // uio[7:0] from the Pico (GPIO 8-12, 26-28): the chip only reads them
    output wire [7:0] tt_uo,      // uo_out[7:0] to the Pico (GPIO 13-20)
    output wire [5:0] led         // active low
);
    wire tt_clk_g;
`ifdef SYNTHESIS
    BUFG u_tt_clk_bufg (.I(tt_clk), .O(tt_clk_g));
`else
    assign tt_clk_g = tt_clk;
`endif

    wire [7:0] uio_out, uio_oe;

    `TT_TOP dut (
        .ui_in  (tt_ui),
        .uo_out (tt_uo),
        .uio_in (tt_uio),
        .uio_out(uio_out),
        .uio_oe (uio_oe),
        .ena    (1'b1),
        .clk    (tt_clk_g),
        .rst_n  (tt_rst_n)
    );

    // ---------------- status LEDs (active low) ----------------
    // LED0 heartbeat (bitstream loaded), LED1 chip clock running, LED2 chip in reset,
    // LED3 valid inputs arriving, LED4 result bytes leaving, LED5 the design drives a uio pin (never expected:
    // the rig wires uio as inputs only, like the demo board with this chip)
    reg [24:0] hb = 25'd0;
    always @(posedge clk) hb <= hb + 1'b1;

    // Every counter bit is used (the top one drives an LED): unused upper bits made yosys' Gowin mapping
    // leave an unplaceable $buf behind.
    reg [17:0] clk_cnt = 18'd0;
    reg [13:0] valid_cnt = 14'd0, out_cnt = 14'd0;
    reg        oe_seen = 1'b0;
    always @(posedge tt_clk_g) begin
        clk_cnt <= clk_cnt + 1'b1;
        if (tt_uio[4]) valid_cnt <= valid_cnt + 1'b1;
        if (tt_uo != 8'd0) out_cnt <= out_cnt + 1'b1;
        if (!tt_rst_n) oe_seen <= 1'b0;
        else if (uio_oe != 8'd0) oe_seen <= 1'b1;
    end

    assign led = ~{oe_seen, out_cnt[13], valid_cnt[13], ~tt_rst_n, clk_cnt[17], hb[24]};
endmodule
