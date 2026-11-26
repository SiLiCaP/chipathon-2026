# Host CI: what the owner's server builds

The host CI builds every push at the size in `info.yaml` (`tiles: "3x2"`) with `local-ci/` and posts the commit
statuses `host-ci/gds` (tests, GDS, precheck, gate-level test) and `host-ci/rig` (the FPGA emulator check below).
A newer push cancels a build that is still running, and the cancelled commit keeps the status
`cancelled: superseded by <sha>`.

## Extra tile sizes and research dies: switched off

`host-ci.yaml` at the repo root can ask the host for extra builds. Both lists are empty on purpose since
`60b31b8` (Nov 25, 2026): the design is fixed at 3x2.

```yaml
tile_variants: []     # e.g. ["4x2"]: also harden at that size, status host-ci/gds-<tiles>
research_dies: []     # e.g. ["2x3"]: a shape Tiny Tapeout cannot take, measured only, status host-ci/gds-<die>-research
```

How they work if they are ever needed again:

- **Tile variants.** Each size gets its own status, posted after the main build. They are informational: nothing
  waits for them, and a newer push cancels the ones still pending. The value is read from the commit being built.
  Sizes are width x height in tiles: 1x1, 1x2, 2x2, 3x2, 3x4, 4x2, 4x4, 5x4, 6x2, 6x4, 8x2, 8x4. Block heights are
  1, 2 or 4 rows, so 3 and 6 rows do not exist and there is no 2x4. The design's own size and invalid values are
  skipped.
- **Research dies.** The host hardens the design at a die size Tiny Tapeout does not offer, only to measure
  utilization, congestion and timing. Such a die can never be taped out, and no precheck runs.

Details: `services/chipci/README.md` in the d0b0-domain repo. What the earlier variant and research runs found:
[archive/host_ci_findings.md](archive/host_ci_findings.md) and [archive/tile_routes.md](archive/tile_routes.md).

## The FPGA emulator check (`host-ci/rig`)

Every push is also run on the Tang Nano 20K emulator (the gate netlist) driven by the Pico 2 W running `rp2350/`:
the chip does the hash sequence of an ML-KEM-512 exchange built from the commit's own sha. `fpga/` and `rp2350/` are
required and must change together with the chip's pins or commands; the contract is in
[ci-emulator.md](ci-emulator.md).
