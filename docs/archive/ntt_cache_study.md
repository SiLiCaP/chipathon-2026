# NTT cache for the 4x2 build: study

Oct 18, 2026. Branch `sha3-4x2` (this note, the models and scripts in `research/ntt_cache/`);
the block-engine RTL and its tests are on branch `sha3-4x2-cache`. Proposal for the Control
owner (Spec 3); nothing here changes `sha3-4x2`'s RTL.

## Summary

- **No NTT cache fits in 4x2 next to the NTT chip and the K = 8 Keccak core.** The 4x2
  build without a cache already fails with the default constraints: 1,590 hold buffers on
  the Keccak ring hops take it to 81 % utilization, global routing overflows (met1 137 %,
  met3 121 %) and the antenna repair finds no room for its diodes (DPL-0036). Both
  block-engine versions I hardened fail earlier, in placement: the full one at CTS (90.7 %
  after detailed placement), the smallest one (C only) when its 1,814 hold buffers go in.
  With the 0.14 ns hold-uncertainty SDC and smaller hold margins (no hold buffers), the
  build without a cache routes (70.4 % utilization, 0 DRC / LVS / antenna errors, precheck and the gate-level test pass, though setup misses 50 MHz at the slow corner by 1.55 ns); the full block engine still fails at CTS,
  and the C-only one places (85.6 %) but global routing overflows (met1 105 %, 8.5k
  overflow) and detailed routing starts at 39k violations.
- **The block engine works** (branch `sha3-4x2-cache`, RTL tested against `test/ntt_ref.py`):
  a forward or inverse NTT in **1,010 clocks (20.2 µs at 50 MHz)** instead of 2,751 (55 µs),
  2× faster than the RP2350's software NTT (about 40 µs). It costs **+30.4k µm²** in plain
  synthesis (+35.6k in the TT flow).
- **For ML-KEM-512 as a whole it is worth little:** one exchange takes 2.56 ms instead of
  3.09 ms with one RP2350 core, but hashing on the chip with the NTT in software already
  gives 2.82 ms (1 core) and 1.87 ms (2 cores); with 2 cores the cached chip NTT (1.98 ms)
  doesn't beat software.
- **Recommendation:** keep `sha3-4x2` as it is. If the NTT core must be fast in its own
  right, the block engine needs more room than 4x2 gives, and with the default constraints
  even 6x2 is not enough (measured: 75.5 % after 2,337 hold buffers, global routing overflow,
  antenna repair fails). It would need 6x2 with the relaxed hold constraints (not measured), or
  a chip without the hasher. The half-polynomial buffer and the Keccak-state reuse are not worth building.

## The candidates

All candidates keep today's streaming modes and add a faster way to run an NTT. The pin
limits are the same for all: one 12-bit coefficient in and one byte out per clock, so a
pass over the polynomial costs at least 256 clocks in and 384 out; the single multiplier
needs 896 clocks per NTT.

**(a) Block engine (B3).** Built and tested (`src/ntt_blk.v`, `docs/block_mode.md` on
`sha3-4x2-cache`). 16 coefficients stay on chip for 4 layers (len 128..16, entries f[j + 16e])
then 3 (len 8..2, entries f[16j + e]); inverse 3 + 4. A block is a header word (bits 11:8 =
0xE, op = pass, bits 3:0 = block) and 16 entries; it returns 8 pairs. One butterfly per
clock with no stalls: the butterfly order in each layer is chosen so no entry is read
before its new value is written back (5 clocks). Storage: L (15 × 12, load buffer), C
(16 × 12, working set with 2 reads and 2 writes per clock), F (8 × 24, last-layer pairs
waiting for the pins). Smaller variants drop L or F and run slower with the same results:

| Variant | Bits | Spacing (FA/FB) | NTT clocks | ntt_core plain synth | Whole chip, TT-flow synth |
| --- | --- | --- | --- | --- | --- |
| streaming (today) | 0 | – | 2,751 | 21.7k µm² | 88.7k µm² |
| LF: L + C + F | 564 | 32 / 24 | **1,010** | 52.0k (+30.4k) | 124.3k (+35.6k) |
| L: L + C | 372 | 46 / 38 | 1,428 | 44.1k (+22.4k) | – |
| F: C + F | 384 | 48 / 40 | 1,490 | 46.7k (+25.0k) | – |
| C: C only | 192 | 62 / 54 | 1,908 | 39.1k (+17.4k) | 109.0k (+20.4k) |

NTT clocks are measured at the pins in RTL simulation (`test_blk.py`, `NTT_BLK=LF|L|F|C`),
one NTT with both passes and the host's turnaround. The inverse NTT takes the same; the
scale stays a streaming job (394 clocks). Basemul is unchanged (655 clocks).

**(b) Half-polynomial buffer** (128 × 12 = 1,536 bits). Layer 0 streams through the pins
(2 clocks per butterfly, one result stays on chip, the other goes out), then each half is
an independent 128-point NTT done on chip (6 layers × 64 butterflies), the halves swapped
through the pins. Model: about 1,420 clocks forward, 1,470 inverse. Storage with the same
ports as the block engine's C: **96.8k µm²** in plain synthesis
(`research/ntt_cache/est_halfpoly.v`), more than the whole chip. It is slower than the
block engine's LF variant at 3× its area. Out.

**(c) Keccak state as coefficient storage** (1,600 ≥ 1,536 bits while the hasher is idle).
The state lives in clock-gated 8-bit shift rings without muxes, so random access is
impossible. The only way to use it is a 128-deep × 12-bit circulating chain with
butterfly taps at distance len: a chain mux on each of the 200 ring inputs, a 7-way tap
read and write-back, and a layer counter. That adds about **4.1k µm²** (plain synthesis of
the extra muxes, `research/ntt_cache/est_kchain.v`) plus control, roughly 6k in all, but:
one butterfly per 2 clocks at best (each layer rotates the whole half through the taps),
so about 2,000 clocks per NTT (an estimate, not designed); the sponge state is destroyed,
so no hash may be in progress during an NTT (ML-KEM interleaves XOF squeezing with NTTs);
and it rebuilds `keccak_ring`, the block with the hardest timing and routing. It is no
faster than the block engine's C variant and much riskier. Out.

### Clocks and host work per job

| Candidate | NTT | Inverse NTT (+ scale 394) | Basemul | CPU packing per NTT (today / PIO flags) |
| --- | --- | --- | --- | --- |
| streaming (today) | 2,751 | 2,765 | 655 | 3,584 coefficient moves: 10,752 / 2,688 cycles |
| (a) LF | 1,010 | 1,010 | 655 | 1,024 moves + 32 headers: 3,072 / 768 cycles |
| (a) C | 1,908 | 1,908 | 655 | 3,072 / 768 cycles |
| (b) | ≈1,420 | ≈1,470 | 655 | ≈640 moves: 1,920 / 480 cycles |
| (c) | ≈2,000 | ≈2,050 | 655 | ≈640 moves: 1,920 / 480 cycles |

A "move" is one coefficient packed into an input word or unpacked from the result bytes.
Today's 12 CPU cycles per streamed butterfly (2 coefficients in, 2 out; Viktor's PIO + DMA
numbers) is 3 cycles per move, the "PIO flags" assumption 0.75. Block mode also needs the
host to place pass-A results where pass B reads them; the unpack loop writes each
coefficient to its final index, so that costs nothing extra.

## Fit in 4x2

Measured in the TT flow (LibreLane 3.0.14, `local-ci`), core area 149,183 µm² (die
682.64 × 225.76 µm), `PL_TARGET_DENSITY_PCT` 60. Stdcell area / utilization after each step:

| Design, constraints | Synth | Global placement | Detailed placement | CTS | Hold repair (buffers) | Final |
| --- | --- | --- | --- | --- | --- | --- |
| NTT + K = 8 Keccak, default | 88.7k | 92.2k / 61.8 % | 96.1k / 64.4 % | 104.8k / 70.2 % | 121.1k / 81.2 % (1,590) | fails: GRT overflow 26.7k (met1 137 %, met3 121 %), then DPL-0036 in antenna repair (335 nets) |
| same, hold-uncertainty 0.14 SDC only | 88.7k | 92.2k / 61.8 % | 96.1k / 64.4 % | 104.8k / 70.2 % | 118.6k / 79.5 % (1,380) | fails: GRT usage 107 %, overflow 30.0k, then DPL-0036 in antenna repair (350 nets) |
| same, 0.14 SDC + hold margins 0.03 / 0.02 | 88.7k | 92.2k / 61.8 % | 96.1k / 64.4 % | 104.8k / 70.2 % | 104.8k / 70.2 % (0) | routes: 105.0k / 70.4 %, 0 DRC / LVS / antenna, precheck and GL test pass; setup −1.55 ns at max_ss_100C_1v60 (20 endpoints) |
| + block engine LF, default | 124.3k | 127.8k / 85.7 % | 135.2k / 90.7 % | fails (DPL-0036 in CTS) | – | – |
| + block engine C only, default | 109.0k | 112.6k / 75.4 % | 119.0k / 79.7 % | 127.6k / 85.6 % | fails (DPL-0036, 1,814 buffers) | – |
| + block engine LF, 0.14 SDC + margins | 124.3k | 127.8k / 85.7 % | 135.2k / 90.7 % | fails (DPL-0036 in CTS) | – | – |
| + block engine C only, 0.14 SDC + margins | 109.0k | 112.6k / 75.4 % | 119.0k / 79.7 % | 127.6k / 85.6 % | 127.6k / 85.6 % (0) | GRT overflow 8.5k (met1 105 %), detailed routing starts at 39k violations |
| + block engine LF in **6x2**, default (core 225.8k µm²) | 124.3k | 129.2k / 57.2 % | – | 147.0k / 65.1 % | 170.4k / 75.5 % (2,337) | fails: GRT overflow 19.7k (met1 97 %), then DPL-0036 in antenna repair |

Global routing of the 4x2 build without a cache (total usage / overflow): default
constraints 108 % / 26.7k (met1 137 %, met3 121 %); 0.14 SDC only 107 % / 30.0k; 0.14 SDC with
the smaller margins 59 % / 124. The hold buffers, not the logic, are what the routing can't take.

The Keccak core is what fills the die: 1,600 state flip-flops behind ~200 clock gates
(403 clock nets after CTS), and with the default constraints one hold buffer on most ring
hops. The 0.14 ns SDC alone still inserts 1,380 hold buffers because the resizer adds its
0.1 ns hold margin on top; only with the smaller margins does the hold repair go to zero.

## ML-KEM-512 exchange time

`research/ntt_cache/overlap_cache.py`: the list scheduler from `research/sha3/overlap_model.py`
(copied, ties broken deterministically) with each candidate's chip clocks and packing cost.
NTT, inverse NTT and basemul always run on the chip, hashing on the Keccak core (K = 8,
368 clocks per permutation), the chip at 50 MHz, the RP2350 at 150 MHz. RP2350 alone:
8.08 ms per exchange (keygen + encaps + decaps).

| Candidate | NTT | 1 core, packing today | 1 core, PIO flags | 2 cores, PIO flags | 2 cores, NTT during permutations |
| --- | --- | --- | --- | --- | --- |
| streaming (today) | 55.0 µs | 3.49 ms | 3.09 ms | 2.50 ms | 2.41 ms |
| (a) LF | 20.2 µs | 2.63 ms | 2.56 ms | 1.98 ms | 1.90 ms |
| (a) L | 28.6 µs | 2.74 ms | 2.68 ms | 2.07 ms | 1.99 ms |
| (a) F | 29.8 µs | 2.76 ms | 2.70 ms | 2.09 ms | 2.01 ms |
| (a) C | 38.2 µs | 2.88 ms | 2.81 ms | 2.21 ms | 2.13 ms |
| (b) half polynomial | ≈28.4 µs | 2.72 ms | 2.68 ms | 2.07 ms | 2.00 ms |
| (c) Keccak state | ≈40 µs | 2.88 ms | 2.84 ms | 2.25 ms | 2.25 ms (no overlap possible) |
| for reference: hash on chip, NTT in software | 40 µs (SW) | 2.82 ms | – | 1.87 ms | – |

Hashing is three quarters of ML-KEM's time, so the NTT speed moves the exchange by at most
about 15 % with one core, and with two cores the software NTT on the second core is as good
as any chip NTT. The block engine is worth having only if the NTT itself is the point (the
demo, or a faster NTT for a protocol with fewer hashes).

## Recommendation

- **4x2 (this build): no cache.** None of the candidates fits with the default constraints;
  even the smallest block engine fails placement, and the build without a cache fails
  routing (see `sha3-4x2`'s harden). With the relaxed hold constraints only the build
  without a cache routes.
- **If the team wants a fast NTT core:** the block engine, full version (L + C + F):
  20.2 µs per NTT, 2× the RP2350's software speed, +30.4k µm² plain / +35.6k TT-flow
  synthesis. Next to the Keccak core it would need 6x2 with the relaxed hold constraints (6x2
  with the defaults fails like 4x2; the relaxed 6x2 run was not made), or 4x2 without the
  hasher. The RTL, tests and spec text are
  ready on `sha3-4x2-cache`.
- **Risks** of the block engine: area and routing (above); the multiplier operand mux gains
  one 2:1 level (its select is a flip-flop, but re-check the slow-corner slack); the host
  must keep the block cadence (the PIO clocks the chip, so it does by construction) and
  must not overlap block mode with a streaming job; GL simulation of block mode is not run
  yet.

### Spec changes the block engine needs (made on `sha3-4x2-cache`)

- **Spec 3:** new section "Block mode" (the `ntt_blk` module, header, schedule, spacing,
  exclusivity with streaming jobs, area), the `blk_q` port on `ntt_core`, and the
  "layer-by-layer round trips" risk updated. `docs/schedule.md`, `docs/host_timing.md`,
  `docs/interface.md` get the block-mode tables; `docs/block_mode.md` is the full description.
- **Spec 5:** coefficient bits 11:8 = 0xE is a block header; the shell drives `blk_q` and
  masks it from `valid_q` / `sof_q`, like the 0xF hash words.
- **Spec 1:** the host's block-mode procedure (passes, entry order, spacing, delays, the
  result-pair order from `test/blk_order.py`), constants in `timing.h` / `timing.py`. The
  RP2350 firmware is not changed yet.

## Reproduce

- Block-engine tests: on `sha3-4x2-cache`, `cd test && make COCOTB_TEST_MODULES=test_blk
  NTT_BLK=LF` (or L, F, C).
- Plain synthesis: `research/ntt_cache/run_synth.sh <top> <name> <files>` (Yosys + ABC in
  the LibreLane image, sky130 hd tt corner, no cell exclusions).
- Exchange times: `python3 research/ntt_cache/overlap_cache.py`.
- Hardens: `local-ci/run gds` in clones with the config changes listed in the table.
