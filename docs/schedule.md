# Per-mode schedule

Owner: Control (Spec 3). Input offsets the host must keep, and what the chip does on each
clock. Values for `L_MUL` = 3, `PRE_LAT` = 1, `POST_LAT` = 0. The constants live in
`test/timing.py`.

**Multiplier use.** One start per clock in basemul and scale (full rate); once every 3
clocks in the NTT modes, where the 3 output bytes per butterfly set the rate. The operand
selects are registered one-hot signals decoded one clock ahead (Spec 3, Timing closure).

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
| 2 | idle | Pre ±: a + b and b − a into the Pre ± output register |
| 3 | next b | a + b into the bypass line, start (b − a) × `zeta_r` (still this unit's ζ′) |
| 3 + L_MUL = 6 | | `pair_valid`: r0 = a + b (delayed), r1 = (b − a) × ζ |

With `PRE_LAT` = 0 (allowed, but it misses timing closure), the start and everything
after it move one clock earlier.

## Basemul (op 10), period 5

One unit is one piece i (0..127): g1 = g[2i+1], f1 = f[2i+1], g0 = g[2i], f0 = f[2i]. The
four coefficients come on consecutive clocks; the next piece may start at offset 5.

| Offset | Host sends | Chip does |
| --- | --- | --- |
| 0 | g1 (valid) | g1 → R0. Look up γ′_i → `zeta_r` |
| 1 | f1 (valid) | f1 → R1 |
| 2 | g0 (valid) | g0 → R2. Start m1 = f1 × g1 |
| 3 | f0 (valid) | f0 → R3. Start m5 = f1 × g0 |
| 4 | idle | Start m4 = f0 × g1 (last use of g1) |
| 5 | next piece's g1 | Start m2 = m1 × γ′, m1 straight from `p_out` as it comes out. `zeta_r` still holds this piece's γ′ |
| 6 | next piece's f1 | Start m3 = f0 × g0. m5 back: acc1 ← m5 |
| 7 | next piece's g0 | m4 back: acc1 ← acc1 + m4 = c1. The next piece's m1 starts |
| 8 | | m2 back: acc0 ← m2 |
| 9 | | m3 back: acc0 ← acc0 + m3 = c0 |
| 10 = 6 + L_MUL + 1 | | `pair_valid`: r0 = c0, r1 = c1 (both 2197 × the plain value, K-RED) |

The multiplier starts on 5 consecutive clocks per piece (2 to 6), so back-to-back pieces
keep it busy every clock. m2 starts exactly `L_MUL` clocks after m1, which is why the
latency is fixed at 3. Each operand register is read for the last time before the next
piece overwrites it, and both sums hold until the pair goes out.

## Scale (op 11), period 3

One unit is a pair c, c′ on consecutive clocks.

| Offset | Host sends | Chip does |
| --- | --- | --- |
| 0 | c (valid) | c → R0 |
| 1 | c′ (valid) | c′ → R1. Start c × 304 |
| 2 | idle | Start c′ × 304 |
| 1 + L_MUL = 4 | | First product: hold as r0 |
| 2 + L_MUL = 5 | | `pair_valid`: r0 = held, r1 = second product |

Each result is 2197 × 304 × c = 3303 × 2197⁻¹ × c: on basemul output (which carries
2197) the net is c × 3303. Back-to-back pairs give a gap-free byte stream.

## Job lengths, streaming only

| Mode | Units | Clocks |
| --- | --- | --- |
| Forward NTT | 896 butterflies (7 layers × 128) | 2688 |
| Inverse NTT | 896 | 2688 |
| Basemul | 128 pieces | 640 |
| Scale | 128 pairs | 384 |

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

## Block mode (NTT cache)

Header at offset 0 (valid, bits 11:8 = 0xE, op = pass, bits 3:0 = block), entries 0..15
at offsets 1..16. Butterfly q reads C at core offset 17 + q, the multiplier starts at 18 + q,
the result is written back at 21 + q; one butterfly per clock in the order of
`test/blk_order.py`. Pairs at core offsets N + 14 + 3m. Full table: `docs/block_mode.md`.
