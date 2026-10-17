# Block mode (NTT cache)

Owner: Control (Spec 3). RTL: `src/ntt_blk.v`, inside `ntt_core`. Host constants:
`test/timing.py` (`BLK_*`, `blk_spacing`, `blk_delay`) and the butterfly order in
`test/blk_order.py`. Tests: `test/test_blk.py`.

## Why

In streaming mode the chip stores nothing, so every NTT layer goes out over the pins and
back: 3 clocks per butterfly, 2,751 clocks (55 µs at 50 MHz) per NTT. Block mode keeps 16
coefficients on chip for 3 or 4 layers, so a whole NTT is 2 passes over the pins instead of
7. One butterfly per clock, the multiplier is busy every clock, and the pins carry each
coefficient once per pass: **1,010 clocks (20.2 µs) per forward or inverse NTT**, measured
at the pins in `test_blk.py`, against about 40 µs for the RP2350 in software.

Basemul and scale stay in streaming mode (they are not pin-limited).

## Passes

A forward NTT is pass FA then pass FB; an inverse NTT is IA then IB (then the streaming
scale, as before). Each pass is 16 blocks of 16 coefficients. `len` is the butterfly
distance of FIPS 203 Algorithms 9 and 10.

| op | Pass | Layers (len) | Entry e of block j | Multiplier clocks N |
| --- | --- | --- | --- | --- |
| 00 | FA, forward A | 128, 64, 32, 16 | f[j + 16e] | 32 |
| 01 | FB, forward B | 8, 4, 2 | f[16j + e] | 24 |
| 10 | IA, inverse A | 2, 4, 8 | f[16j + e] | 24 |
| 11 | IB, inverse B | 16, 32, 64, 128 | f[j + 16e] | 32 |

After FA, block j of FB needs f[16j .. 16j + 15], which come from all 16 FA blocks, so the
host collects the whole FA output before it starts FB (it unpacks the results into place
anyway). Passes of different polynomials may be mixed freely, for example FA of g between
FA and FB of f, which hides the turnaround.

## Pin protocol

- **Header.** One word with valid high, coefficient bits 11:8 = 0xE (never a coefficient),
  bits 3:0 = block index j, op = pass. sof is ignored. The shell masks the header out of
  the streaming path.
- **Entries.** The 16 entries on the next 16 clocks, valid high, entry 0 first. They must be
  consecutive.
- **Spacing.** The next header may come `N` clocks after this one (N of this block, 24 or
  32), or later, whatever pass it is.
- **Results.** 8 pairs, 24 bytes on consecutive clocks, the first `D = N + 16` clocks after
  the header (48 for FA and IB, 40 for FB and IA). Each pair is (new f[lo], new f[hi]) of one
  butterfly of the pass's last layer, in the order `blk_order.out_pairs(pass, j)` gives
  (the last row of the order table), ByteEncode12-packed like every other result.
- Block mode and streaming jobs share the multiplier: don't start one while the other
  still has results to come. Hash commands may run alongside as long as their output
  bytes don't fall on the same clocks.

At the minimum spacing a pass takes 15 × N + D + 24 clocks: 552 for FA / IB, 424 for FB /
IA, 976 per NTT, plus the host's turnaround between the two passes.

## Inside the chip

Core clocks from the header on `coeff_q` (c = 0):

| Clock | What |
| --- | --- |
| 1 .. 16 | Entries shift into L (15 × 12). At 16, C (16 × 12) ← {entry 15, L} |
| 17 + q | Butterfly q (layer q / 8, position q % 8) reads C[lo], C[hi]; S1 ← (a, b) forward or (a + b, b − a) inverse; ζ′ from `zeta_rom` |
| 18 + q | Multiplier start: S1.y × ζ′; S1.x into the bypass line |
| 21 + q | Product out. Forward (x + t, x − t), inverse (x, t), written back to C (readable at 22 + q), or into F[position] in the last layer |
| N + 14 + 3m | `pair_valid` for pair m = 0..7, from F (8 × 24) |

A result is readable 5 clocks after its butterfly read C. `blk_order.py` picks the order of
the 8 butterflies in each layer so that no butterfly reads an entry earlier than that: no
stalls, one butterfly per clock (the table is in `ntt_blk.v`). ζ′ index for the lower
coefficient jj of a butterfly in global layer l: forward `{1, jj} >> (8 − l)`; inverse
(len = 2^s) `({1, jj} >> (s + 1)) ^ (0x7F >> s)`, the same table as streaming mode.

With the spacing rule each block uses C, F, the multiplier and the output pins in one
contiguous window no longer than the spacing, so blocks never collide, for any mix of
passes and any extra gaps (`test_blk_mixed`).

## Variants (area study)

`ntt_blk` has two parameters for smaller versions with the same results and protocol, only
slower (spacing and delay in `timing.py`, selected in simulation with `NTT_BLK=LF|L|F|C`):

| Variant | Storage | Spacing | D | NTT clocks (measured) | ntt_core, plain synthesis |
| --- | --- | --- | --- | --- | --- |
| LF (default) | L + C + F, 564 bits | N | N + 16 | 1,010 | 52.0k µm² (+30.4k) |
| L | L + C | N + 14 | N + 15 | 1,428 | 44.1k µm² (+22.4k) |
| F | C + F | N + 16 | N + 16 | 1,490 | 46.7k µm² (+25.0k) |
| C | C only, 192 bits | N + 30 | N + 15 | 1,908 | 39.1k µm² (+17.4k) |

Streaming-only `ntt_core`: 21.7k µm² (plain Yosys + ABC, sky130 hd, tt corner). See
`docs/ntt_cache_study.md` for the fit in 4x2.
