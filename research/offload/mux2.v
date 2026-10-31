// second read port: 21 lanes x 8 positions of 8 bits, selects one extra byte (wide squeeze)
module mux2(input [1343:0] st, input [7:0] sel, output [7:0] o);
  assign o = st[sel*8 +: 8];
endmodule
