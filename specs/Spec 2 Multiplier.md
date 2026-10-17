# Spec 2: Multiplier

Oct 17, 2026 · @A

## Your job

You build one block: a modular multiplier that returns (2197 × A × B) mod 3329 in a fixed number of clocks (the factor 2197 = 13³ comes from K-RED reduction, explained under Behavior). The Control owner wires it in. You never touch pins, the ROM or any other block.

- **In scope:** a 12-bit by 12-bit multiply with three K-RED rounds inside, pipelined, one result every clock, latency 3.
- **Out of scope:** choosing the operands (ROM, loop-back, registers), sequencing, adders, pins.
- **Why it matters:** it is the largest block on the chip. It sets the speed of basemul (5 products per piece) and scale, and it matches the output pin rate in the NTT modes.

The chip uses it four ways. The block is the same each time and never sees the mode.

| Mode | Products it computes (all mod 3329) |
| --- | --- |
| Forward NTT | b × ζ′ (ζ′ is the stored, scaled twiddle) |
| Inverse NTT | (b − a) × ζ′ |
| Basemul | f1 × g1, then that × γ′, then f0 × g0, f0 × g1, f1 × g0 |
| Scale | c × 304 |

Each product comes out multiplied by 2197. Control stores scaled constants so that this factor cancels in the NTT modes. You do not need to know how.

## Interface

The block takes one start pulse and gives one valid pulse, with fixed timing and no back-pressure. Module name: `modq_mul`.

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk` | in | 1 | Chip clock |
| `rst_n` | in | 1 | Synchronous reset, active low |
| `start` | in | 1 | High for one clock when `a_in` and `b_in` are valid. Accepted on every clock |
| `a_in` | in | 12 | First operand, 0..3328. Sampled on the `start` clock only |
| `b_in` | in | 12 | Second operand, 0..3328. Sampled on the `start` clock only |
| `flush` | in | 1 | Drops every operation in flight. `p_valid` stays low from the next clock |
| `p_out` | out | 12 | (2197 × a\_in × b\_in) mod 3329. Valid when `p_valid` is high, and holds until the next `p_valid` |
| `p_valid` | out | 1 | High for one clock per result |
| `ready` | out | 1 | High when a `start` would be accepted. Always high for a pipelined block. Used for assertions |

Rules:

- Sample both operands on the `start` clock. Control does not hold them.
- The block is pipelined: a `start` on every clock gives a result on every clock, each exactly the latency after its start. Control starts it on consecutive clocks in basemul (5 starts per piece) and in scale, and once every 3 clocks in the NTT modes, where the output pins set the rate.
- `flush` drops everything in flight, and a `start` on the clock right after `flush` is accepted. Control never raises `start` in the same clock as `flush`.
- No other ports. If you need one, ask the Control owner.

## Behavior

For every input pair in 0..3328, `p_out` equals (2197 × a\_in × b\_in) mod 3329, fully reduced to 0..3328. The factor 2197 = 13³ is deliberate.

- **Pipeline, three stages.**
  - S1: C = a × b, one 12 × 12 multiply. The product is at most 3328 × 3328 = 11,075,584, which needs 24 bits.
  - S2: two K-RED rounds at once on the bytes of C (C = C2·2¹⁶ + C1·2⁸ + C0): t = 169 × C0 − 13 × C1 + C2, which is congruent to 13² × C. t lies in −3315..43264 and needs 17 bits, signed.
  - S3: a third round on t = t1·2⁸ + t0 (t0 = t\[7:0\], t1 = t >>> 8, signed): u = 13 × t0 − t1, which is congruent to 13³ × C. u lies in −169..3328, so the only correction is: if u is negative, add 3329.
- **Why K-RED works.** 3329 = 13 × 2⁸ + 1, so 13 × 2⁸ ≡ −1. For x = xH·2⁸ + xL, K-RED(x) = 13 × xL − xH, which is congruent to 13 × x. All ranges above were checked for every C in 0..3328².
- **Small constant multiplies.** 13 × x is 8x + 4x + x and 169 × x is 128x + 32x + 8x + x, so the only real multiplier is the 12 × 12 in S1.
- **Your choice of hardware.** Any hardware is fine if the exhaustive test passes with p\_out = (2197 × a\_in × b\_in) mod 3329 and the timing below holds. The port behavior must not change.
- **Bad inputs.** Operands of 3329 or more never occur. The result is undefined, but the block must not hang or corrupt the next operation.
- **No mode.** The block does the same thing every time.

Why a factor of 2197: K-RED needs no wide constant multiplies, so it is much smaller than Barrett. The cost is that every product carries 13³ = 2197. The Control owner cancels it by storing the twiddles as ζ × 2197⁻¹ mod 3329 (2197⁻¹ = 944), and by using 304 (= 3303 × 2197⁻²) as the scale constant. Basemul results keep one factor of 2197 and scale removes it. None of this is visible in your block.

## Design decision: full-rate pipeline, three K-RED rounds (Oct 17)

Two designs were compared: the digit-serial multiplier first proposed here (one 12 × 4 multiplier used over 3 clocks, two K-RED rounds, factor 169, latency 4), and `modmul_kred` by Maksims Latkovskis on the `feature/multiplier` branch (full 12 × 12, one result per clock, three K-RED rounds, factor 2197, latency 3). Area with the sky130 library (Yosys, typical corner):

| Design | Area |
| --- | --- |
| Full 12 × 12, one result per clock, three K-RED rounds (`modmul_kred`) | 8,481 µm² |
| Digit-serial (one shared 12 × 4), three K-RED rounds | 6,813 µm² |
| Digit-serial, two K-RED rounds | 6,256 µm² |

Decision: the full-rate design. It is about a third larger. In return:

- it has latency 3 instead of 4, so every output delay D is one clock shorter;
- three rounds leave only a one-sided correction;
- one product per clock lets basemul and scale run at full rate (Spec 3, Oct 17 change): basemul 15 → 5 clocks per piece, scale 6 → 3 clocks per pair. The NTT modes stay at 3 clocks per butterfly, set by the three output bytes per pair.

To be accepted, a multiplier must:

- be module `modq_mul` with exactly the ports above, including `flush` and `ready`;
- use a synchronous reset (shared rules), not an asynchronous one;
- return 2197 × a × b;
- accept a `start` on every clock, with a latency of exactly 3.

`src/modq_mul.v` is `modmul_kred` with these interface changes, and it passes the exhaustive test.

## Timing

One operation per clock, with a fixed latency that you report.

| Quantity | Requirement |
| --- | --- |
| Throughput | One `start` per clock, sustained. Control uses it: 5 products in 5 clocks per basemul piece, 2 in 3 clocks per scale pair, one every 3 clocks per butterfly |
| Latency, `start` to `p_valid` | Exactly 3 clocks for every input. The basemul schedule in Spec 3 depends on it |
| Data-dependent timing | None |
| `p_out` hold | Stable until the next `p_valid` |

Why latency matters: Control delays the bypass lane by exactly your latency, and the host's output-delay constants are built from it. With latency 3 the first byte appears 6 clocks after the first input in forward mode and 8 in inverse mode. Basemul starts m2 = m1 × γ′ on the clock m1 comes out, so its 5-clock schedule only works with latency 3. Keep it at 3.

Report to the Control owner: the latency in clocks, the longest combinational path in your design (likely the 12 × 12 multiply in S1), and a rough cell count.

Clock target: the chip must meet 50 MHz at the slow corner after place and route (Spec 3, Timing closure). S1 takes its operands from Control's one-level operand mux, which is driven by flip-flops, so S1 holds the 12 × 12 multiply and nothing else. Measured, the multiply takes about 9.5 ns of the 20 ns. Splitting it into two 12 × 6 halves was tried and gave only 0.26 ns, so the block stays as it is.

## Tests and test vectors

Run these before you hand the block over. `test/mul` has a ready testbench: `make -C test/mul` (quick) and `make -C test/mul exhaustive`.

- **Exhaustive.** All 3329 × 3329 pairs (about 11 million) compared with (2197 × a × b) mod 3329, with a `start` on every clock. Use Verilator or similar. Sampled runs in a slower simulator are fine while developing, but the full run must pass before handover.
- **Back to back.** A `start` on every clock, and every 3 clocks, for at least 1000 random pairs. Results arrive in order, each exactly your reported latency after its start.
- **Gaps.** Start spacings of 1, 2, 3, 4, 7 and 15 clocks, and a long pause followed by a restart.
- **Flush.** Start, flush one clock later, then start again. No stale `p_valid` appears.
- **Reset.** Reset in the middle of an operation, then run a normal operation.

| a\_in | b\_in | p\_out | Where it comes from |
| --- | --- | --- | --- |
| 1 | 1 | 2197 | The factor itself |
| 966 | 2000 | 2498 | Forward butterfly: b = 2000, ζ = 1729 stored as ζ′ = 966, so the result is b × ζ |
| 3328 | 3328 | 2197 | (−1) × (−1) × 2197 |
| 3328 | 2 | 2264 | (−1) × 2 × 2197 |
| 1234 | 2345 | 350 | Arbitrary |
| 0 | 3328 | 0 | Zero |
| 500 | 304 | 2023 | Scale constant 304 applied to a plain 500 |
| 3328 | 304 | 1241 | Scale constant 304 applied to a plain 3328 |

The Pi owner's reference model also provides vector files for this block.

## Deliverables and done when

**Hand over:**

- `modq_mul.v` (module `modq_mul`)
- A testbench with the tests above, runnable with one command
- A short README: latency in clocks, longest combinational path, rough cell count

**Done when:**

- The exhaustive test passes.
- The back-to-back, gap, flush and reset tests pass.
- The latency is reported to the Control owner.
- The Control owner has instantiated the block in a small top-level test without changing your ports.

**Do not:** add ports, add handshakes, or make timing depend on the data.

## Shared rules for all five specs

- **One chip, one clock.** Everything runs on `clk`. Reset is `rst_n`, active low. Use synchronous logic only, with no latches and no generated clocks.
- **Numbers.** Every coefficient is 12 bits, unsigned, and always in 0..3328 on every port. Never pass a value of 3329 or more between blocks.
- **Ports are frozen.** Port names and widths in your spec do not change without telling the Control owner, who holds the interface table.
- **Latency is reported, not hidden.** Say how many clocks each of your blocks takes in your README.
- **Reference.** The Pi owner's Python model is the single source of truth. If your block disagrees with it, find out why before changing either.
- **Code.** Plain Verilog-2001 or SystemVerilog that the Tiny Tapeout flow accepts. One module per file, named like the file.
- **Sharing.** These docs and the reference pages linked here are private. Share them with your teammates before relying on the links.
