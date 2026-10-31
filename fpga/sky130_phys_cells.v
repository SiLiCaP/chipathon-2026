// Physical-only sky130 cells (fillers, well taps) that appear in a hardened netlist but carry
// no logic, so they have no function in the liberty file. Empty modules let yosys read the netlist.
module sky130_fd_sc_hd__fill_1 (); endmodule
module sky130_fd_sc_hd__fill_2 (); endmodule
module sky130_fd_sc_hd__fill_4 (); endmodule
module sky130_fd_sc_hd__fill_8 (); endmodule
module sky130_fd_sc_hd__tapvpwrvgnd_1 (); endmodule

// Integrated clock gate (the hash chip's ring Keccak gates its clocks with it). The liberty file models it
// as a statetable, which yosys's read_liberty skips, so it is spelled out here: a latch that is transparent
// while CLK is low holds GATE, and GCLK = CLK & latched GATE. On the FPGA the latch is a negative-edge
// flop, which is the same thing for a GATE that settles before CLK falls (synchronous logic, as here).
module sky130_fd_sc_hd__dlclkp_1 (input CLK, input GATE, output GCLK);
    reg en = 1'b0;
    always @(negedge CLK) en <= GATE;
    assign GCLK = CLK & en;
endmodule
module sky130_fd_sc_hd__dlclkp_2 (input CLK, input GATE, output GCLK);
    reg en = 1'b0;
    always @(negedge CLK) en <= GATE;
    assign GCLK = CLK & en;
endmodule
module sky130_fd_sc_hd__dlclkp_4 (input CLK, input GATE, output GCLK);
    reg en = 1'b0;
    always @(negedge CLK) en <= GATE;
    assign GCLK = CLK & en;
endmodule
