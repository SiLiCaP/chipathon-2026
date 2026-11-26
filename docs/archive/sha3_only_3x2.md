# The hash-only chip in 3×2: harden results (Oct 18, 2026)

For the team, before the tile decision. The NTT-free chip on branch `sha3-only`: a Keccak-f[1600] sponge
(`keccak_ring`, K = 8) behind `hash_io`, with burst squeeze and on-chip SamplePolyCBD. This page covers what
it takes in silicon, and why the answer is **3×2 at K = 8**. Every number is from a LibreLane harden run on
Viktor's machine (tt-support-tools, the TT flow), unless marked **model**. How the route was chosen:
[tile_routes.md](tile_routes.md).

## 1. Result

**K = 8 + CBD in 3×2 signs off clean.** The tests pass in the harden tree (4/4), and:
- route DRC, magic DRC, LVS and antenna are all 0;
- there are no setup or hold violations at any corner;
- setup slack at 50 MHz is +8.17 ns at the slow corner.

Nothing faster or smaller fits:

| Variant | Tile | Synth | After CTS | Result |
|---|---|---|---|---|
| **Hash-only K = 8 + CBD** | **3×2** | 48.6k µm² | 55.5 % | **clean**, 44.8 min harden |
| Hash-only K = 16 + CBD | 3×2 | 60.8k µm² | 69.2 % | **does not route**: detailed routing still at 32,578 violations after 1 h 40 min (37,325 → 35,356 → 32,578); stopped |
| Hash-only K = 8 + CBD | 2×2 | 48.6k µm² | 83.6 % | **does not route**: global routing needed overflow passes, and detailed routing started at 23,058 violations; stopped |
| SHA-only K = 8 (no CBD, no bursts) | 2×2 | 47.0k µm² | – | does not route: 9,660 violations after an hour |
| SHA-only K = 4 | 2×2 | 41.1k µm² | 71.2 % | clean, but 760 clocks per permutation and an 85 min harden |
| SHA-only K = 2 | 2×2 | 38.3k µm² | – | clean, but 1,544 clocks per permutation |

- **K = 16 would be faster** (172 clocks per permutation instead of 368; **model** 2.87 ms instead of 3.18 ms
  per exchange). But its round logic doubles, and 3×2 can't route it. It would need 4×2.
- **Bursts and CBD cost 1.6k µm²** (48.6k against 47.0k for the plain SHA-only K = 8 top). That is under 2 % of
  3×2, so the helper is effectively free here.

## 2. Signoff detail (K = 8 + CBD, 3×2)

Core area 110,874 µm² (die 114,858 µm²). Standard cells 61,588 µm², **55.5 %**; the rest is fill and tap cells.

| Cell class | Area | Count |
|---|---|---|
| sequential (state rings, counters, I/O) | 33,512 µm² | 1,674 |
| combinational | 15,080 µm² | 2,238 |
| clock buffers | 8,194 µm² | 349 |
| tap cells | 1,973 µm² | 1,577 |
| timing-repair buffers | 1,836 µm² | 354 |
| clock gates (`keccak_ring` sub-rings) | 823 µm² | 47 |
| antenna diodes | 73 µm² | 29 |

| Corner | Setup slack (20 ns) | Hold slack |
|---|---|---|
| slow, 100 °C, 1.60 V | **+8.17 ns** | +0.418 ns |
| typical, 25 °C, 1.80 V | +11.82 ns | +0.130 ns |
| fast, −40 °C, 1.95 V | +13.30 ns | **+0.018 ns** (worst) |

- **Clock:** 50 MHz is closed with margin. The worst path needs about 11.8 ns, so roughly 80 MHz at the slow
  corner (estimate from the slack; the board's own limit is lower).
- **Hold:** one hold buffer in total. The worst hold slack is in the fast corner, as expected for the
  ring's flip-flop-to-flip-flop hops.
- **Power:** 3.75 mW total (OpenROAD estimate at default switching activity).
- **Routing:** 182k µm of wire. The router converged in 21 iterations; the worst iteration had 180 violations.
- **Warnings** (none stops the flow):
  - 1,739 max-slew warnings at the slow corner (73 typical, 0 fast);
  - 4 max-cap warnings at the slow corner;
  - 1 max-fanout warning;
  - 17 lint warnings.

  As for `main` (1,242 slew warnings), these are worth a look before tapeout.
- **Flow settings:** the team's relaxed recipe, committed in the branch's `src/config.json`:
  - a 0.14 ns hold-uncertainty SDC (`src/custom.sdc`);
  - hold margins 0.03 / 0.02;
  - small cells allowed;
  - `clkdlybuf4s` delay cells banned;
  - placement density 75.

## 3. Verified

- **RTL simulation** (`test/test_hash_only.py`): 4/4 tests at K = 8 and at K = 16:
  - every SHA-3 / SHAKE function at the rate edges, with byte and burst squeeze;
  - CBD for η = 2 and 3 against FIPS 203 SamplePolyCBD;
  - a quiet `uo_out`.
- **On the Pico 2 W + Tang Nano rig** (the chip's RTL in the FPGA, every pin over real wires):
  - 42/42 jobs bit-exact at 1, 3.4, 6.75 and 13.5 MHz;
  - a full ML-KEM-512 key exchange with all 32 hash calls on the chip: keys identical to pure-software
    FIPS 203, every call equal to hashlib.
- **Not run yet:** gate-level simulation of this netlist, and the Tiny Tapeout precheck.

## 4. What it buys

ML-KEM-512, one key exchange on the Tiny Tapeout board (**model**: RP2350 at 150 MHz with pqm4's optimized code,
chip at 50 MHz with its exact clock counts and the RP2350's PIO feed included; `sim/route_model.py`):

| | Exchange | |
|---|---|---|
| RP2350 alone | 8.08 ms | |
| RP2350 + this chip | **3.18 ms** | **2.54× faster** |
| of which hashing: RP2350 alone → chip | 5.81 ms → 1.09 ms | 5.3× |

## 5. Before tapeout

1. **Ask Tiny Tapeout about the custom SDC** (0.14 ns hold uncertainty instead of 0.25 ns). The ring core
   needs it.
2. **Run the TT precheck and a gate-level test** on this netlist.
3. **Review the slow-corner slew warnings.**
4. **Choose a top-module name.** It is still `tt_um_NTT_Core`.
5. **Write the RP2350 firmware's hash path**: PIO feed and burst reads. The speed-up depends on it (route
   model: 1.77× with the firmware written naively, 2.54× with PIO + DMA).

## Reproduce

In tt-viktor-chiptor:

```bash
python3 tools/tile_exp.py run hash_k8_3x2      # build from sha3-only, test, harden (tools/harden_times.py: times)
python3 host/sha3_demo.py                       # the chip on the rig, ML-KEM-512 live
```
In `sha3-only`: `cd test && make` (K = 8) or `HASH_K=16 make`.
