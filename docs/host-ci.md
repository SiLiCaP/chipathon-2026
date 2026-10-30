# Host CI: trying other tile sizes

The host CI (`host-ci/gds`, `host-ci/rig`) builds the design at the size in `info.yaml` (`tiles: "2x2"`).
To also see whether it routes at another size, list the sizes in `host-ci.yaml` at the repo root:

```yaml
tile_variants: ["3x2"]
```

Each size gets its own commit status `host-ci/gds-<tiles>` (for example `host-ci/gds-3x2`), posted after the
main build, whether that passed or not. These are informational only: nothing waits for them, and a newer
push cancels the ones still pending. The value is read from the commit being built, so changing it needs a push.

Sizes are width x height in tiles: 1x1, 1x2, 2x2, 3x2, 4x2, 6x2, 8x2. Tiny Tapeout allows at most two
rows, so a "vertical 3" (2x3) does not exist; 3x2 is three tiles across, two high. The design's own size and
invalid values are skipped (the main status text says so).

## Research dies: shapes Tiny Tapeout cannot take

```yaml
research_dies: ["2x3"]
```

`2x3` is two tiles wide and three tall, the vertical layout. The host hardens the design at that die size
only to **measure** utilization, congestion and timing, for example whether more vertical space helps a
vertically stuffed design. **It is research only and can never be taped out**: Tiny Tapeout slots are at
most two rows tall, the pin template is a stretched copy of the stock one, and no precheck runs. The result is the
informational status `host-ci/gds-2x3-research`, for example
`2x3 research (never taped out): util 48.1% DRC 0 LVS 0 ant 0 setup +7.90 hold +0.021 ns 52m`, or
`failed at: <step>` when the flow itself dies. The 2x3 die is 334.88 x 368.56 um, 7 % more area than 3x2
(114,858 um2 in `docs/sha3_only_3x2.md`). Same rules as the tile variants (after the main build, never
blocks a newer push).

Ad hoc, without a commit (the host's owner): `docker exec d0b0-chipci python -m chipci.ctl variant <sha|branch> 3x2`.
Details: `services/chipci/README.md` in the d0b0-domain repo. Background on the sizes: `docs/tile_routes.md`.
