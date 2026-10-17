/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ntt_twiddle: the zeta' / gamma' for the unit whose first coefficient is idx.
 * Primes are the scaled constants (x 2197^-1) that zeta_rom stores; the
 * multiplier's factor 2197 cancels them.
 *
 * Forward, layer l (len = 128 >> l), butterfly n in the layer:
 *   ROM index k = (128 + n) >> (7 - l)        -> 1, 2..3, 4..7, ..., 64..127
 *   (k starts at 1 and steps by one after every block of len butterflies)
 * Inverse, layer l (len = 2 << l): the forward table read bottom up,
 *   x = (128 + n) >> (l + 1),  k = x ^ (0x7F >> (l + 1))  -> 127..64, ..., 1
 * Basemul piece i: gamma' = ROM'[64 + (i >> 1)], or 3329 minus it for odd i.
 * Scale: unused (the core wires 304).
 */

`default_nettype none

module ntt_twiddle (
    input  wire [1:0]  op,
    input  wire [10:0] idx,
    output wire [11:0] zeta
);

  localparam [1:0] OP_INV = 2'b01, OP_BM = 2'b10;

  wire [2:0] layer = idx[10:8];
  wire [6:0] bf    = idx[7:1];
  wire [6:0] piece = idx[8:2];

  wire       inv = (op == OP_INV);
  wire [2:0] sh  = inv ? (layer + 3'd1) : (3'd7 - layer);
  wire [7:0] x   = {1'b1, bf} >> sh;
  wire [6:0] k_ntt = x[6:0] ^ (inv ? (7'h7F >> sh) : 7'h00);

  wire       bm   = (op == OP_BM);
  wire [6:0] addr = bm ? {1'b1, piece[6:1]} : k_ntt;
  wire       neg  = bm & piece[0];

  wire [11:0] rom_data;
  zeta_rom u_rom (
      .addr(addr),
      .data(rom_data)
  );

  wire [11:0] negd = 12'd3329 - rom_data;   // no ROM' entry is 0
  assign zeta = neg ? negd : rom_data;

  wire _unused = &{x[7], idx[0], 1'b0};

endmodule
