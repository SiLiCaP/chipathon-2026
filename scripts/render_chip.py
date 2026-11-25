"""Render the chip's GDS with KLayout: the full die, or a close-up of the team's chip art (the metal 4 logo).

    pip install klayout                         # the KLayout Python module (0.29 or newer)
    python3 scripts/render_chip.py runs/wokwi/final/gds/tt_um_NTT_Core.gds docs/img/full.png full
    python3 scripts/render_chip.py runs/wokwi/final/gds/tt_um_NTT_Core.gds docs/img/art.png logo

Tiny Tapeout's own render (the gds workflow, gds_render.png) draws the cell layers, so the chip art is hidden there:
this one draws the routing layers muted and metal 4 in gold on top. docs/img/chip_render.jpg and chip_art.png come
from it (scaled down to stay under 512 KB).
"""
import sys

import klayout.db as db
import klayout.lay as lay

gds, out, mode = sys.argv[1], sys.argv[2], sys.argv[3]
lv = lay.LayoutView()
lv.set_config("background-color", "#0b0d12")
lv.set_config("grid-visible", "false")
lv.set_config("text-visible", "false")
lv.load_layout(gds, True)
lv.max_hier()
# sky130 drawing layers: (layer, datatype) -> (fill, frame, dither, z order by insertion)
STYLE = {
    (65, 20): ("#2b3a2b", 1),   # diff
    (66, 20): ("#5a3030", 1),   # poly
    (67, 20): ("#3c4f73", 1),   # li1
    (68, 20): ("#456b8c", 1),   # met1
    (69, 20): ("#7a5a9a", 1),   # met2
    (70, 20): ("#2f8f83", 1),   # met3
    (71, 20): ("#f2c230", 0),   # met4 (the logo)
}
it = lv.begin_layers()
while not it.at_end():
    lp = it.current()
    key = (lp.source_layer, lp.source_datatype)
    if key in STYLE:
        col, dither = STYLE[key]
        c = int(col[1:], 16)
        lp.fill_color = c
        lp.frame_color = c
        lp.dither_pattern = dither          # 0 solid, 1 hollow-ish pattern
        lp.visible = True
        lp.transparent = False
    else:
        lp.visible = False
    lv.set_layer_properties(it, lp)
    it.next()
ly = lv.cellview(0).layout()
top = ly.top_cell()
bb = top.dbbox()
print("die", bb)
if mode == "full":
    w = 3200
    h = int(w * bb.height() / bb.width())
    lv.save_image_with_options(out, w, h, 0, 0, 0, bb, False)
else:
    # the logo: 28 x 56 um at (454.02, 10.88), with a margin
    box = db.DBox(454.02 - 6, 10.88 - 6, 454.02 + 28 + 6, 10.88 + 56 + 6)
    h = 1200
    w = int(h * box.width() / box.height())
    lv.save_image_with_options(out, w, h, 0, 0, 0, box, False)
