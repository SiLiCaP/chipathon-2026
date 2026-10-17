# Spec 2: Multiplier

Oct 17, 2026 · @A

## Your job

You build one block: a modular multiplier that returns (169 × A × B) mod 3329 in a fixed number of clocks (the factor 169 comes from K-RED reduction, explained under Behavior). The Control owner wires it in. You never touch pins, the ROM or any other block.

- **In scope:** a 12-bit by 12-bit multiply, 4-bit digit-serial, with K-RED reduction inside, one result every 3 clocks.
- **Out of scope:** choosing the operands (ROM, loop-back, registers), sequencing, adders, pins.
- **Why it matters:** it is the largest block on the chip. It sets the speed of basemul (5 products per piece) and scale, and it matches the output pin rate in the NTT modes.

The chip uses it four ways. The block is the same each time and never sees the mode.

| Mode | Products it computes (all mod 3329) |
| --- | --- |
| Forward NTT | b × ζ′ (ζ′ is the stored, scaled twiddle) |
| Inverse NTT | (b − a) × ζ′ |
| Basemul | f1 × g1, then that × γ′, then f0 × g0, f0 × g1, f1 × g0 |
| Scale | c × 1441 |

Each product comes out multiplied by 169. Control stores scaled constants so that this factor cancels in the NTT modes. You do not need to know how.

## Interface

The block takes one start pulse and gives one valid pulse, with fixed timing and no back-pressure. Module name: `modq_mul`.

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk` | in | 1 | Chip clock |
| `rst_n` | in | 1 | Synchronous reset, active low |
| `start` | in | 1 | High for one clock when `a_in` and `b_in` are valid. Control never starts less than 3 clocks after the previous start |
| `a_in` | in | 12 | First operand, 0..3328. Sampled on the `start` clock only |
| `b_in` | in | 12 | Second operand, 0..3328. Sampled on the `start` clock only |
| `flush` | in | 1 | Drops every operation in flight. `p_valid` stays low from the next clock |
| `p_out` | out | 12 | (169 × a\_in × b\_in) mod 3329. Valid when `p_valid` is high, and holds until the next `p_valid` |
| `p_valid` | out | 1 | High for one clock per result |
| `ready` | out | 1 | High when a `start` would be accepted. Used for assertions |

Rules:

- Sample both operands on the `start` clock. Control does not hold them for the 3 digit clocks.
- Latency can be longer than 3 clocks, so two operations may overlap: the newer one in its digit clocks while the older one is in reduction. Support that.
- No other ports. If you need one, ask the Control owner.

## Behavior

For every input pair in 0..3328, `p_out` equals (169 × a\_in × b\_in) mod 3329, fully reduced to 0..3328. The factor 169 is deliberate.

- **Digit-serial multiply.** Consume one operand 4 bits per clock over 3 clocks (your choice which, least significant digit first) and accumulate the partial products. The full product is at most 3328 × 3328 = 11,075,584, which needs 24 bits.
- **K-RED reduction inside the block.** 3329 = 13 × 2^8 + 1, so 13 × 2^8 ≡ −1. For a product x with low 8 bits xL and high bits xH (x >> 8), K-RED(x) = 13 × xL − xH, which is congruent to 13 × x. Apply it twice, then correct once: if the result is negative add 3329, if it is 3329 or more subtract 3329. Two rounds give 169 × x mod 3329. I checked all 3329 × 3329 input pairs. After round 1 the value is in −43264..3315, after round 2 in −12..3482, so the final correction always lands in 0..3328. A third round does not help. 13 × xL is 8xL + 4xL + xL, so no wide multiplier is needed. The partial results are signed, so carry a sign bit.
- **Your choice of hardware.** Any hardware is fine if the exhaustive test passes with p\_out = (169 × a\_in × b\_in) mod 3329. The port behavior must not change.
- **Bad inputs.** Operands of 3329 or more never occur. The result is undefined, but the block must not hang or corrupt the next operation.
- **No mode.** The block does the same thing every time.

Why a factor of 169: K-RED needs no wide multiplies, so it is much smaller than Barrett. The cost is that every product carries 169. The Control owner cancels it by storing the twiddles as ζ × 169⁻¹ mod 3329, and by using 1441 (= 3303 × 169⁻²) as the scale constant. Basemul results keep one factor of 169 and scale removes it. None of this is visible in your block.

## Timing

One operation every 3 clocks, with a fixed latency that you report.

| Quantity | Requirement |
| --- | --- |
| Throughput | One `start` every 3 clocks, sustained. This is the NTT rate (3 clocks per butterfly) and the basemul rate (5 products, 15 clocks per piece) |
| Latency, `start` to `p_valid` | Fixed for every input. Target 4 clocks (3 digit clocks plus reduction) or less. Report the real number |
| Data-dependent timing | None |
| `p_out` hold | Stable until the next `p_valid` |

Why latency matters: Control delays the bypass lane by exactly your latency, and the host's output-delay constants are built from it. The handoff assumed the first byte appears 5 clocks after the first input in forward mode and 7 in inverse mode. That only holds if your latency is small. If 4 is not possible, tell the Control owner early. A longer latency only changes constants. It does not break the design.

Report to the Control owner: the latency in clocks, the longest combinational path in your design, and a rough cell count.

## Tests and test vectors

Run these before you hand the block over.

- **Exhaustive.** All 3329 × 3329 pairs (about 11 million) compared with (169 × a × b) mod 3329. Use Verilator or similar. Sampled runs in a slower simulator are fine while developing, but the full run must pass before handover.
- **Back to back.** A `start` every 3 clocks for at least 1000 random pairs. Results arrive in order, each exactly your reported latency after its start.
- **Gaps.** Start spacings of 3, 4, 7 and 15 clocks, and a long pause followed by a restart.
- **Flush.** Start, flush one clock later, then start again. No stale `p_valid` appears.
- **Reset.** Reset in the middle of an operation, then run a normal operation.
- **Assertion.** Fail if `start` comes less than 3 clocks after the previous `start`.

| a\_in | b\_in | p\_out | Where it comes from |
| --- | --- | --- | --- |
| 1 | 1 | 169 | The factor itself |
| 2571 | 2000 | 2498 | Forward butterfly: b = 2000, ζ = 1729 stored as ζ′ = 2571, so the result is b × ζ |
| 3328 | 3328 | 169 | (−1) × (−1) × 169 |
| 3328 | 2 | 2991 | (−1) × 2 × 169 |
| 1234 | 2345 | 283 | Arbitrary |
| 0 | 3328 | 0 | Zero |
| 500 | 1441 | 2996 | Scale constant 1441 applied to a plain 500 |
| 3328 | 1441 | 2817 | Scale constant 1441 applied to a plain 3328 |

The Pi owner's reference model also provides vector files for this block.

## Deliverables and done when

**Hand over:**

- `modq_mul.v` (module `modq_mul`)
- A testbench with the tests above, runnable with one command
- A short README: latency in clocks, start spacing, longest combinational path, rough cell count

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
