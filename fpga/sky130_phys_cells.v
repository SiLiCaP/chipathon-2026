// Physical-only sky130 cells (fillers, well taps) that appear in a hardened netlist but carry
// no logic, so they have no function in the liberty file. Empty modules let yosys read the netlist.
module sky130_fd_sc_hd__fill_1 (); endmodule
module sky130_fd_sc_hd__fill_2 (); endmodule
module sky130_fd_sc_hd__fill_4 (); endmodule
module sky130_fd_sc_hd__fill_8 (); endmodule
module sky130_fd_sc_hd__tapvpwrvgnd_1 (); endmodule
