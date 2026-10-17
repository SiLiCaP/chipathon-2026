# Per-mode schedule

Owner: Control (Spec 3). Input offsets the host must keep, and what the chip does on each
clock. Values for `L_MUL` = 3, `PRE_LAT` = `POST_LAT` = 0. The constants live in
`test/timing.py`.

**Offsets.** Clocks from the unit's first coefficient. The host column is the pin word in
that host clock. The chip column is in core clocks: the coefficient the host sends at
offset o is on `coeff_q` at core offset o, one clock later than the pins (input capture).

**Rules for every mode:**

- The first coefficient of a job has `valid` and `sof` high and carries `op`.
- Clocks with `valid` low change nothing. Units may be spaced further apart than the
  period, but never closer. Inside a unit, the coefficients must be on the listed offsets.
- The host never sends 3329 or more.

## Forward NTT (op 00), period 3

One unit is one butterfly: b = f[j + len], a = f[j]. Butterflies go in Algorithm 9 order:
len = 128, 64, …, 2, and for each block, j from `start` to `start + len − 1`.

| Offset | Host sends | Chip does |
| --- | --- | --- |
| 0 | b (valid) | b → R0. Look up ζ′ (ROM′[(128 + n) >> (7 − layer)]) → `zeta_r` |
| 1 | a (valid) | Start R0 × `zeta_r` = t (2197 × b × ζ′ = b × ζ). a enters the bypass line (length `L_MUL`) |
| 2 | idle | Multiplier pipeline |
| 1 + L_MUL = 4 | | t and a meet at Post ±: `pair_valid`, r0 = a + t (a'), r1 = a − t (b') |

## Inverse NTT (op 01), period 3

Same unit order as forward, but layers go len = 2, 4, …, 128 (Algorithm 10).

| Offset | Host sends | Chip does |
| --- | --- | --- |
| 0 | b (valid) | b → R0. Look up ζ′ (ROM′ index 127 down to 1) → `zeta_r` |
| 1 | a (valid) | a → R1 |
| 2 | idle | Pre ±: a + b into the bypass line, start (b − a) × `zeta_r` |
| 2 + L_MUL = 5 | | `pair_valid`: r0 = a + b (delayed), r1 = (b − a) × ζ |

With `PRE_LAT` = 1, the start and everything after it move one clock later.

## Basemul (op 10), period 15

One unit is one piece i (0..127): g1 = g[2i+1], f1 = f[2i+1], g0 = g[2i], f0 = f[2i].

| Offset | Host sends | Chip does |
| --- | --- | --- |
| 0 | g1 (valid) | g1 → R0. Look up γ′_i → `zeta_r`. Piece timer starts |
| 1 | f1 (valid) | f1 → R1 |
| 2 | g0 (valid) | g0 → R2. Start pass 1: f1 × g1 = m1 |
| 3 | f0 (valid) | f0 → R3 |
| 5 | idle | Start pass 3: f0 × g0 = m3. m1 back, stored in the loop-back register |
| 8 | idle | Start pass 2: m1 × γ′ = m2. m3 back: acc0 ← m3 |
| 11 | idle | Start pass 4: f0 × g1 = m4. m2 back: acc0 ← acc0 + m2 = c0 |
| 14 | idle | Start pass 5: f1 × g0 = m5. m4 back: acc1 ← m4 |
| 15 | next piece's g1 | |
| 17 | | m5 back: acc1 ← acc1 + m5 = c1 |
| 18 = 15 + L_MUL | | `pair_valid`: r0 = c0, r1 = c1 (both 2197 × the plain value, K-RED) |

m3 runs between m1 and m2 so m1 has up to 6 clocks to come back before m2 needs it. Any
multiplier latency from 2 to 6 fits; at 6, m1 is taken straight from `p_out` on the clock
it arrives. The multiplier starts every 3 clocks and a piece takes 15. The last part of
each piece overlaps the start of the next one.

## Scale (op 11), period 3

One unit is one coefficient c.

| Offset | Host sends | Chip does |
| --- | --- | --- |
| 0 | c (valid) | c → R0 |
| 1 | | Start c × 304 |
| 1 + L_MUL = 4 | | First of a pair: hold as r0 |

The second coefficient of the pair arrives at offset 3 and its product at 3 + 1 + L_MUL = 7:
`pair_valid` with r0 = held, r1 = product. One pair every 6 clocks. Each result is
2197 × 304 × c = 3303 × 2197⁻¹ × c: on basemul output (which carries 2197) the net is
c × 3303.

## Job lengths, streaming only

| Mode | Units | Clocks |
| --- | --- | --- |
| Forward NTT | 896 butterflies (7 layers × 128) | 2688 |
| Inverse NTT | 896 | 2688 |
| Basemul | 128 pieces | 1920 |
| Scale | 256 coefficients | 768 |

The NTT modes add the host's waits between layers (see `host_timing.md`).

## Cancel and reset

- A word with valid and sof while a job is running cancels it. In that core clock,
  `flush` is high: `io_out_pack` drops its pending bytes and `modq_mul` drops everything
  in flight. Nothing starts and no pair is emitted in that clock. The sof coefficient is
  input 0 of the new job. At the pins, old-job bytes can still appear up to the clock
  after the sof word, and never later.
- A sof while idle starts a job without a flush.
- `rst_n` low clears the counters, operand registers, delay lines, accumulators and the
  op latch.
