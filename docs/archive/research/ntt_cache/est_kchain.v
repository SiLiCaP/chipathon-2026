// Area estimate for candidate (c): what reusing keccak_ring's 1,600 state flip-flops as a
// 128-deep x 12-bit circulating coefficient store would add (the flip-flops themselves exist).
//  - a chain-mode mux on each of the 200 ring closures (ring inputs come from the previous ring)
//  - the butterfly taps: entry 0 and entry 0 + len (len = 64..1, 7 taps) read, 12 bits each
//  - the write-back of the two results at the tap points (7 x 12 insertion muxes) and the exit
//  - a layer / position counter
module est_kchain (
    input  wire          clk,
    input  wire          chain,
    input  wire [199:0]  closure,      // keccak_ring's normal closures
    input  wire [199:0]  prev_exit,    // exit bit of the ring before in the chain
    output wire [199:0]  ring_in,
    input  wire [1535:0] st,           // the state bits as 128 x 12
    input  wire [2:0]    tap,
    input  wire [11:0]   w0, w1,
    input  wire          wv,
    output reg  [11:0]   rd_lo, rd_hi,
    output wire [83:0]   tap_in
);
  assign ring_in = chain ? prev_exit : closure;
  always @(posedge clk) begin
    rd_lo <= st[11:0];
    case (tap)
      3'd0: rd_hi <= st[64*12 +: 12];
      3'd1: rd_hi <= st[32*12 +: 12];
      3'd2: rd_hi <= st[16*12 +: 12];
      3'd3: rd_hi <= st[8*12 +: 12];
      3'd4: rd_hi <= st[4*12 +: 12];
      3'd5: rd_hi <= st[2*12 +: 12];
      default: rd_hi <= st[1*12 +: 12];
    endcase
  end
  genvar g;
  generate for (g = 0; g < 7; g = g + 1) begin : g_t
    assign tap_in[g*12 +: 12] = (wv && tap == g) ? w1 : st[((64 >> g) + 1) * 12 +: 12];
  end endgenerate
endmodule
