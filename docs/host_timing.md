# Host timing

Owner: Control (Spec 3), for the Pi owner's `timing.h` / `timing.py`. The source of
these numbers is `test/timing.py`. The pin-level tests measure D from the waveform and
fail if it differs (`test/test.py` `test_output_delay`).

Valid for `L_MUL` = 3 (pipelined K-RED multiplier, fixed by the basemul schedule),
`PRE_LAT` = 1, `POST_LAT` = 0, 1-clock input capture and 1-clock packer. Update if a block's
latency changes. `rp2350/gen_timing.py` writes the same numbers into the C harness.

## Clock convention

In host clock k the host drives input word k on the pins and samples `uo_out`. The sample
is taken in the same clock period, before the rising edge that captures word k. D is
counted from the clock of a unit's first input word to the clock of its first result byte.
The second and third bytes follow on the next two clocks.

## Output delays

| Constant | Meaning | Value | Formula |
| --- | --- | --- | --- |
| `D_F` | Forward: b of a butterfly → first byte of (a', b') | **6** | L_CAP + 1 + L_MUL + POST_LAT + L_PACK |
| `D_I` | Inverse: b → first byte of (a + b, (b − a)ζ) | **8** | L_CAP + 2 + PRE_LAT + L_MUL + L_PACK |
| `D_B` | Basemul: g1 of a piece → first byte of (c0, c1) | **12** | L_CAP + 6 + L_MUL + 1 + L_PACK |
| `D_S` | Scale: c of a pair → first byte of the pair | **7** | L_CAP + 2 + L_MUL + L_PACK |

The handoff assumed D_F = 5 and D_I = 7. Forward is 6, one clock over, because the
multiplier takes b from a register one clock after it arrives. Inverse is 8: it needs both
b and a before Pre ±, and the Pre ± result is registered for timing closure.

## Periods and offsets

| Constant | Value |
| --- | --- |
| `P_F`, `P_I` | 3 clocks per butterfly |
| `P_B` | 5 clocks per basemul piece |
| `P_S` | 3 clocks per scale pair |
| Input offsets, forward / inverse | b at 0, a at 1 |
| Input offsets, basemul | g1 at 0, f1 at 1, g0 at 2, f0 at 3 |
| Input offsets, scale | c at 0, c′ at 1 |

Units may be spaced further apart than the period, never closer. Back-to-back NTT and
scale units give a continuous byte stream with no gaps; basemul has a 2-clock gap per
piece.

## Result order

| Mode | Pair (r0, r1) | Value at the pins (K-RED, Spec 3) |
| --- | --- | --- |
| Forward | (new f[j], new f[j + len]) of the butterfly sent | Plain FIPS 203 |
| Inverse | (new f[j], new f[j + len]), before the scale | Plain FIPS 203 (2197 × plain if the input carries 2197, as basemul output does) |
| Basemul | (h[2i], h[2i + 1]) of piece i | 2197 × the plain basemul |
| Scale | (c[2n], c[2n + 1]) scaled | c × 304 × 2197 = c × 3303 × 2197⁻¹ |

The flow forward, forward, basemul, inverse, scale gives the exact product (tested
against schoolbook). Compare basemul or scale output on its own with the chip-level model
(`test/ntt_ref.py` `chip_*`), not with the plain one.

## Layer waits

The chip stores nothing. Within one NTT job (one sof, 1792 inputs), the host sends a layer
of 128 butterflies, waits for the last result, then sends the next layer without sof,
using the results. The chip keeps its counters while `valid` is low, for any number of
clocks.

Layer by layer, each layer takes 128 × 3 = 384 clocks of input plus D + 3 clocks to drain:

| Job | Clocks, layer by layer |
| --- | --- |
| Forward NTT | 7 × (384 + 9) = 2751 |
| Inverse NTT | 7 × (384 + 11) = 2765 |
| Basemul | 640 + 15 = 655 |
| Scale | 384 + 10 = 394 |
| Demo (2 forward + basemul + inverse + scale) | 9,316 (streaming estimate 9,088) |

The pin-level test `test_full_flow` measures these counts.

## Faults

Any unpacked result ≥ 3329 is a hardware fault. Abort with a new sof, or reset
(`rst_n` low for several clocks).

## Block mode (NTT cache)

From `test/timing.py` and `test/blk_order.py`; see `docs/block_mode.md`.

| Constant | Meaning | Value |
| --- | --- | --- |
| `BLK_N` | Multiplier clocks per block = minimum header spacing after it | 32 (FA, IB), 24 (FB, IA) |
| `blk_delay` | Header → first result byte | N + 16: 48 (FA, IB), 40 (FB, IA) |
| `BLK_WORDS` | Header + entries, consecutive clocks | 17 |
| `BLK_PAIRS` | Result pairs per block, 3 clocks apart (24 bytes, no gaps) | 8 |

| Job | Clocks at the minimum spacing |
| --- | --- |
| One pass | 15 × N + D + 24: 552 (FA, IB), 424 (FB, IA) |
| Forward or inverse NTT | 976 + the host turnaround between the passes (1,010 in `test_blk.py`) |
| Demo (2 forward + basemul + inverse + scale) | 4,079 in `test_blk_full_flow` (streaming: 9,316) |
