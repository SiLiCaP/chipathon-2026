# Host CI findings: tile sizes, pin placement, the FPGA rig (Oct 30–31, 2026)

For the team, as a head start. Everything here was measured on the d0b0 host (Ryzen 7 255, 16 threads)
with this repo's own `local-ci` and the Tiny Tapeout flow (LibreLane 3.0.14, sky130A), on the hash-only
chip that is `main` since Oct 30 (K = 8 ring Keccak + `hash_io` + on-chip CBD, `tt_um_NTT_Core`, 3x2).
How the host CI works and how to ask it for extra runs: [host-ci.md](host-ci.md).

> **Scope (owner, Oct 31):** 3x2 (6 tiles) is the size we have. 4x2 (8 tiles) is the next plausible step
> and the one worth exploring further (it needs hold tuning first, section 2). Anything larger (3x4 and up,
> 12+ tiles) is out of budget and need not be looked at; the 3x4 numbers below are kept only because the run
> already existed. Research dies (2x3, 3x3, 2x4) are for curiosity: TT cannot take them.

## 1. Summary

- **3x2 (today, 6 tiles) is the best shape we measured.** It signs off clean with the most timing margin
  of every size and pin layout tried: 55.5 % utilization, +8.17 ns setup at the slow corner.
- **Moving the I/O pins does not help.** Spreading them along the edge, or putting them next to the logic
  that uses them, made wiring longer and timing worse (section 3). The stock clustered pins win.
- **4x2 (8 tiles, +2) is not a free upgrade.** It routes, but it fails sign-off with hold violations under
  the current hold settings (the 0.14 ns SDC was tuned for 3x2); one earlier run also stopped at LVS.
  It needs its own hold tuning before it is a real option.
- **A taller die is not better than a wider one.** The research-only 2x3 (two across, three high, a shape TT
  cannot take) signed off clean but with *less* margin than 3x2 (+7.57 ns vs +8.17 ns); even the roomy 3x3 research die (9 tiles, 35 % full) only reaches +8.04 ns, and the tall 2x4 (8 tiles) +7.36 ns.
- **K = 16 (the faster Keccak core) still does not fit at +2 tiles.** At 4x2 detailed routing was still at
  6,265 violations after 29 iterations and 10 hours; it was stopped.
- **TT tile heights are 1, 2 or 4 rows only** (tt-support-tools accepts only the sizes listed in its `tech/sky130A/tile_sizes.yaml`): there is no 3x3,
  2x3, 2x4 or 2x6. The smallest tall size is 3x4 (12 tiles, double today's allotment).

## 2. Tile sizes (sky130, width x height)

Legal sizes in tt-support-tools: 1x1, 1x2, 2x2, 3x2, 3x4, 4x2, 4x4, 5x4, 6x2, 6x4, 8x2, 8x4.
One tile is 161.00 x 111.52 µm; 3x2 = 508.76 x 225.76 µm.

| Size | Tiles | Legal | Result | Util. | Setup (slow) | Hold (fast) | Wirelength |
|---|---|---|---|---|---|---|---|
| **3x2** (main) | 6 | yes | **clean** | 55.5 % | **+8.17 ns** | +0.025 ns | 182,191 µm |
| 4x2 | 8 | yes | **fails sign-off** (hold; one run LVS) | 42.4 % | +6.46 ns | see note | 208,393 µm |
| 2x3 research | 6* | **no** | clean | 51.9 % | +7.57 ns | +0.025 ns | 184,346 µm |
| 3x3 research | 9* | **no** | clean (12.6 min) | 34.7 % | +8.04 ns | +0.026 ns | 192,183 µm |
| 2x4 research | 8* | **no** | clean (11 min) | 37.6 % | +7.36 ns | +0.016 ns | 190,723 µm |
| 3x4 | 12 | yes, but double the budget | clean | 25.0 % | +7.80 ns | +0.024 ns | 178,730 µm |
| 4x2, K = 16 | 8 | yes | **does not route** (6,265 violations after 29 iterations, 10 h) | – | – | – | – |

*Research dies are hardened on a stretched pin template and skip precheck; they can never be taped out.
The 3-row height (368.56 µm, used by 2x3 and 3x3) is interpolated between TT's 2- and 4-row sizes.
Note on 4x2: the metrics of a finished harden looked clean (hold +0.022 ns), but the CI's full flow stopped
on hold violations (`Checker.HoldViolations`) and one experiment stopped at the LVS checker. Treat 4x2 as
"needs work", not as "clean".

**Reading:** the design is not short of area at 3x2 (55 %), it is short of *routing* in its densest spot.
More area spreads the cells but lengthens the long wires, so timing margin drops (3x4 has half the
utilization and still less margin than 3x2). If more speed is the goal, the lever is the core (K) and the
hold recipe, not the tile count. If +2 tiles are ever on the table, 4x2 needs a hold-uncertainty / hold
margin sweep first.

## 3. Pin placement

Question: TT's pins sit clustered on the top edge of the block. Would placing `ui_in`, `uo_out` and `uio`
where they are used, or spreading them out, shorten routing and improve timing?

**TT rule first: project pins come from TT's fixed DEF template and are checked by precheck, so moving them
is research only.** The experiment still answers whether it would be worth asking for.

| 3x2, same everything else | Util. | Setup (slow) | Hold (fast) | Wirelength |
|---|---|---|---|---|
| a: stock pins (baseline) | 55.5 % | **+8.17 ns** | +0.025 ns | **182,191 µm** |
| b: pins spread evenly along the top edge | 55.8 % | +7.81 ns | +0.023 ns | 190,231 µm (+4 %) |
| c: pins next to the logic that uses them | 56.4 % | +6.61 ns | +0.023 ns | 203,777 µm (+12 %) |
| d: stock pins, placement density 60 % instead of 75 % (TT-legal) | 56.1 % | +7.74 ns | **−0.011 ns: fails** | 208,241 µm (+14 %) |

a, b and c signed off clean (0 DRC / LVS / antenna); d, the one setting TT allows us to change, spread the cells out and failed
on hold at the fast corner. **Neither moving pins nor loosening placement helped**: the
critical paths are inside the Keccak ring, not at the pins, and pulling pins toward one block stretches the
wires of every other block. The stock template is the right choice.

## 4. The FPGA rig (Pico 2 W + Tang Nano 20K)

The host CI runs every passing GDS on a real rig: the gate-level netlist on the Tang Nano 20K, driven by the
Pico 2 W running `rp2350/` (contract and vectors: [ci-emulator.md](ci-emulator.md)). What it took:

- **sky130 clock-gate cell:** the hash chip uses `sky130_fd_sc_hd__dlclkp_1` (47 instances). Its liberty
  entry is a statetable that yosys skips, so the GL bitstream needs an explicit model
  (`fpga/sky130_phys_cells.v`, branch `fpga-clock-gate`): a negative-edge enable flop ANDed with CLK.
- **`host/tt_emu.py`:** the pin check (`tools/rig_pincheck.py`) needs the emulator's PC driver from
  tt-viktor-chiptor; it now lives in this repo next to `fpga/`.
- **Pico flashing:** after `picotool -f` reboots the Pico into BOOTSEL, its USB node is root-owned for a
  moment until udev hands it to the right group; retrying the load for a few seconds fixes it.
- **Tang loading:** a 4.6 MB GL bitstream once hit openFPGALoader's "Erase SRAM TIMEOUT" and wedged the JTAG
  link until a `--detect`; the rig now allows 300 s, re-detects and retries once.
- **Pin check:** all 26 wires (8 `ui_in`, 8 `uio`, 8 `uo_out`, clk, rst_n) pass at 13.5 MHz.
- **LEDs:** the Tang loads an all-LEDs-off design after every run (and in flash), so the board is dark when idle.

## 5. CI behaviour worth knowing

- A full `main` build (test, docs, gds, precheck, gl_test) takes about 26 minutes on the host; GitHub's
  `gds.yaml` and `test.yaml` skip themselves when the host acknowledges a push within 10 minutes. On GitHub
  the cocotb run of this chip alone takes about 3.5 hours.
- A new commit on a branch cancels the build of the older one; merging a PR therefore cancels the PR
  branch's build (the merge commit on `main` is built instead).
- Extra runs are opt-in via `host-ci.yaml` (`tile_variants`, `research_dies`); they are informational and
  never block a merge.

## 6. Moving more math onto the chip (short version)

Full study: [onchip_offload.md](onchip_offload.md) (branch `onchip-offload`). Per ML-KEM-512 exchange the
hash chip computes for ~0.58 ms; what costs the time is getting data on and off it. With the firmware as
written, pins + the RP2350's per-byte work are 3–6x the compute in viktor's route model (41 % of a 4.57 ms
exchange), and about 10x in viktor's live demo on the real rig (being reconciled in that doc).

| Option | Time saved (firmware as written / optimized feed) | Area | Fits 3x2 | Needs |
|---|---|---|---|---|
| PIO/DMA feed in firmware | ~1.4 ms / – | 0 | – | firmware only |
| Cache matrix Â in firmware | 0.84 / 0.33 ms | 0 | – | keygen+encaps+decaps on one device |
| On-chip SampleNTT rejection + packing | 0.45 / 0.15 ms | 3.5k µm² | yes | a `valid` output pin (uio) |
| FINISH command (chip does the padding) | 0.15 / 0.03 ms | 0.7k µm² | yes | – |
| Seeds + nonce counter on chip | 0.17 / 0.03 ms | 16k µm² | yes | – |
| K = 16 core | 0.31 ms | +12.2k µm² | no | 4x2, and it does not route there today |

Order of work: firmware first (PIO/DMA + Â cache, ~1.5 ms of 4.57 ms, no silicon change); then, if a uio pin can
become a `valid` output (or the uio command framing changes, see the study), the on-chip sampler.
