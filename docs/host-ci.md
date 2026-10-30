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

Ad hoc, without a commit (the host's owner): `docker exec d0b0-chipci python -m chipci.ctl variant <sha|branch> 3x2`.
Details: `services/chipci/README.md` in the d0b0-domain repo. Background on the sizes: `docs/tile_routes.md`.
