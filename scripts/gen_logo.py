#!/usr/bin/env python3
"""The logo on the chip: src/logo/logo.png -> a pin-less metal 4 block (GDS + LEF) the flow places as a macro.

    python3 scripts/gen_logo.py            (needs gdstk; the PNG is read with the standard library)

Built like Tiny Tapeout's own logos (tt/logo.py): met4.drawing rectangles inside a prBoundary, and a LEF that
blocks met4 over the whole block. It is a COVER cell, not a BLOCK: standard cells may sit underneath (as a BLOCK it
took 1,568 um2 of placement away, and at 74 % utilization the design then no longer routed). One image pixel is one PITCH x PITCH um square, drawn DOT um wide, so two
neighbours keep met4's minimum spacing (0.3 um) and no pixel only touches another at a corner. A pixel is
metal when it is opaque and light (the logo is white on transparent).

The block has to fit between two pairs of the vertical met4 power straps (33.97 um free at PDN pitch 38.87);
src/config.json places it and keeps the PDN and placement halos small enough not to reach the straps.
"""
import os
import struct
import zlib

import gdstk

NAME = "tt_um_NTT_Core_logo"
PITCH = 1.4          # um per pixel
DOT = 1.1            # um of metal per pixel: 0.3 um to the next one
MET4 = (71, 20)      # met4.drawing
BOUNDARY = (235, 4)  # prBoundary.boundary
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "logo")


def read_png(path):
    """8-bit RGBA / RGB / grey PNG -> rows of booleans (True = metal)."""
    b = open(path, "rb").read()
    assert b[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    i, idat = 8, b""
    while i < len(b):
        n, t = struct.unpack(">I4s", b[i:i + 8])
        d = b[i + 8:i + 8 + n]
        i += 12 + n
        if t == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", d)
        elif t == b"IDAT":
            idat += d
    assert depth == 8 and interlace == 0 and ctype in (0, 2, 4, 6), "8-bit non-interlaced grey / RGB (A) only"
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    stride, raw, rows, prev, pos = w * ch, zlib.decompress(idat), [], bytearray(w * ch), 0
    for _ in range(h):
        f, line = raw[pos], bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            up = prev[x]
            ul = prev[x - ch] if x >= ch else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + up) & 255
            elif f == 3:
                line[x] = (line[x] + (a + up) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(up - ul), abs(a - ul), abs(a + up - 2 * ul)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else up if pb <= pc else ul)) & 255
        prev = line
        px = [line[x * ch:(x + 1) * ch] for x in range(w)]
        colour = [p[:ch - 1] if ch in (2, 4) else p for p in px]
        alpha = [p[-1] if ch in (2, 4) else 255 for p in px]
        rows.append([al > 127 and sum(c) / len(c) > 127 for c, al in zip(colour, alpha)])
    return rows


def main():
    rows = read_png(os.path.join(OUT, "logo.png"))
    h, w = len(rows), len(rows[0])
    width, height = w * PITCH, h * PITCH
    lib = gdstk.Library()
    cell = lib.new_cell(NAME)
    cell.add(gdstk.rectangle((0, 0), (width, height), layer=BOUNDARY[0], datatype=BOUNDARY[1]))
    off = (PITCH - DOT) / 2
    for y, row in enumerate(rows):
        for x, on in enumerate(row):
            if on:
                x0, y0 = x * PITCH + off, (h - 1 - y) * PITCH + off      # image row 0 is the top
                cell.add(gdstk.rectangle((x0, y0), (x0 + DOT, y0 + DOT), layer=MET4[0], datatype=MET4[1]))
    lib.write_gds(os.path.join(OUT, NAME + ".gds"))
    with open(os.path.join(OUT, NAME + ".lef"), "w", newline="\n") as f:
        f.write("\n".join([
            "VERSION 5.7 ;", "  NOWIREEXTENSIONATPIN ON ;", '  DIVIDERCHAR "/" ;', '  BUSBITCHARS "[]" ;',
            f"MACRO {NAME}", "  CLASS COVER ;", f"  FOREIGN {NAME} ;", "  ORIGIN 0.000 0.000 ;",
            f"  SIZE {width:.3f} BY {height:.3f} ;", "  OBS", "      LAYER met4 ;",
            f"        RECT 0.000 0.000 {width:.3f} {height:.3f} ;", "  END", f"END {NAME}", "END LIBRARY", ""]))
    with open(os.path.join(OUT, "logo.txt"), "w", newline="\n") as f:
        f.write("\n".join("".join("#" if on else "." for on in row) for row in rows) + "\n")
    print(f"{NAME}: {w} x {h} pixels, {sum(map(sum, rows))} of metal, {width:.2f} x {height:.2f} um")


if __name__ == "__main__":
    main()
