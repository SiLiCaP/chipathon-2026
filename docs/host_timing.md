# Host timing

Owner: Control (Spec 3), for the Pi owner's `timing.h` / `timing.py`. The source of
these numbers is `test/timing.py`. The pin-level tests measure D from the waveform and
fail if it differs (`test/test.py` `test_output_delay`).

Valid for `L_MUL` = 3 (pipelined K-RED multiplier), `PRE_LAT` = `POST_LAT` = 0, 1-clock
input capture and 1-clock packer. Update if a block's latency changes.

## Clock convention

In host clock k the host drives input word k on the pins and samples `uo_out`. The sample
is taken in the same clock period, before the rising edge that captures word k. D is
counted from the clock of a unit's first input word to the clock of its first result byte.
The second and third bytes follow on the next two clocks.

## Output delays

| Constant | Meaning | Value | Formula |
| --- | --- | --- | --- |
| `D_F` | Forward: b of a butterfly → first byte of (a', b') | **6** | L_CAP + 1 + L_MUL + POST_LAT + L_PACK |
| `D_I` | Inverse: b → first byte of (a + b, (b − a)ζ) | **7** | L_CAP + 2 + PRE_LAT + L_MUL + L_PACK |
| `D_B` | Basemul: g1 of a piece → first byte of (c0, c1) | **20** | L_CAP + 15 + L_MUL + L_PACK |
| `D_S` | Scale: first c of a pair → first byte of the pair | **9** | L_CAP + 4 + L_MUL + L_PACK |

The handoff assumed D_F = 5 and D_I = 7. Inverse meets 7. Forward is 6, one clock over,
because the multiplier takes b from a register one clock after it arrives. The gap between
them is 1 clock, not 2: the inverse starts its multiply 1 clock after the forward, once
both b and a are in.

## Periods and offsets

| Constant | Value |
| --- | --- |
| `P_F`, `P_I` | 3 clocks per butterfly |
| `P_B` | 15 clocks per basemul piece |
| `P_S` | 3 clocks per scale coefficient (6 per output pair) |
| Input offsets, forward / inverse | b at 0, a at 1 |
| Input offsets, basemul | g1 at 0, f1 at 1, g0 at 2, f0 at 3 |
| Input offsets, scale | c at 0 |

Units may be spaced further apart than the period, never closer. Back-to-back NTT units
give a continuous byte stream with no gaps.

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
| Inverse NTT | 7 × (384 + 10) = 2758 |
| Basemul | 1920 + 23 = 1943 |
| Scale | 768 + 12 = 780 |
| Demo (3 NTT + basemul + scale) | 10,983 (streaming estimate 10,752) |

The pin-level test `test_full_flow` measures these counts.

## Faults

Any unpacked result ≥ 3329 is a hardware fault. Abort with a new sof, or reset
(`rst_n` low for several clocks).
