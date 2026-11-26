# Handoff: the hash-only chip (branch `sha3-only`), Oct 18, 2026

For the team. A short map of everything about the NTT-free chip; the detail is in the linked docs.

## What it is

- **Branch `sha3-only`:** the NTT core removed. Inside:
  - `io_in_capture` → `hash_io` (new: byte adapter, BURST and BURST_CBD commands) → `keccak_ring` (K = 8,
    368 clocks per permutation).
  - The pins and hash words are as on `sha3-4x2` (`uio[3:0]` = 0xF marks a command), so existing host code
    runs.
- **Commands:**

  | Command | Does |
  |---|---|
  | 0 absorb, 1 squeeze, 2 permute, 3 clear, 4 skip | as on `sha3-4x2` |
  | 5 burst n | streams 8n bytes, one per clock |
  | 6 burst CBD | the same bytes as FIPS 203 SamplePolyCBD coefficients, η = 2 or 3 |

  The datasheet is `docs/info.md` on the branch.
- **Size:** 3×2 (`info.yaml`). Config: the research branch's relaxed recipe in `src/config.json` (0.14 ns
  hold SDC, small cells, no `clkdlybuf`). Top module still `tt_um_NTT_Core`.

## Status

| | Result | Where |
|---|---|---|
| RTL tests | 4/4 at K = 8 and K = 16: all SHA-3 / SHAKE, bursts, CBD vs FIPS 203, quiet output | `test/test_hash_only.py` on the branch |
| Harden 3×2 | **clean**: 55.5 %, 0 DRC / LVS / antenna, +8.17 ns setup at the slow corner, 3.75 mW | [sha3_only_3x2.md](sha3_only_3x2.md) |
| Other sizes | 2×2 and K = 16 in 3×2 don't route | [tile_routes.md](tile_routes.md), [sha3_only_3x2.md](sha3_only_3x2.md) |
| On hardware | Pico 2 W + Tang Nano rig: wiring verified, 42/42 jobs up to 13.5 MHz, a full ML-KEM-512 exchange with all 32 hash calls on the chip, equal to FIPS 203 | tt-viktor-chiptor `docs/PICO_TANG_RIG.md`, `docs/FPGA_DEMO.md` |
| Speed (model) | one ML-KEM-512 exchange: RP2350 alone 8.08 ms → **3.18 ms with the chip (2.54×)**; hashing 5.81 → 1.09 ms | [tile_routes.md](tile_routes.md) section 4 |
| Firmware | `rp2350/` from `main`, plus `core/sha3_sw.c` (software SHA-3 reference, not yet called) | branch `rp2350/` |

## Next

1. **Decide** between the NTT chip (2×2, `main`) and the hash chip (3×2, `sha3-only`).
2. **Ask Tiny Tapeout** whether the custom hold SDC is acceptable.
3. **Precheck and gate-level test**, review the slow-corner slew warnings, and pick a top-module name.
4. **RP2350 firmware hash path** (PIO feed, burst reads, CBD output). It decides the real speed-up: 1.77×
   written naively, 2.54× with PIO + DMA (model).

Demo: `python3 tt-viktor-chiptor/host/sha3_demo.py` (loads the FPGA itself). Viktor's full handoff:
tt-viktor-chiptor `docs/HANDOFF.md`.
