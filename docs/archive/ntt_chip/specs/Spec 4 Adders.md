# Spec 4: Adders

Oct 17, 2026 · @A

## Your job

You build the modular add, subtract and accumulate blocks. The Control owner wires them between the multiplier, the bypass lane and the packer. You never touch pins, the ROM or the multiplier.

- **In scope:** two small modules, `modq_addsub` and `modq_acc`, with every result reduced to 0..3328.
- **Out of scope:** choosing operands, pass-through muxes, mode selection and timing alignment. Control owns those.
- **Where they are used:** before the multiplier in the inverse NTT (Pre ±), and after it in the forward NTT and in basemul (Post ±).

| Mode | What your blocks compute (all mod 3329) |
| --- | --- |
| Forward NTT | a' = a + t and b' = a − t |
| Inverse NTT | a + b and b − a |
| Basemul | two running sums: c0 = m2 + m3 and c1 = m5 + m4 |
| Scale | not used |

## Modules and ports

Two modules, with no mode inputs on either.

**`modq_addsub`** computes the sum and the difference of two values at the same time.

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Present on every module. Unused when `REG_OUT` is 0 |
| `x` | in | 12 | First operand, 0..3328 |
| `y` | in | 12 | Second operand, 0..3328 |
| `sum` | out | 12 | (x + y) mod 3329 |
| `diff` | out | 12 | (x − y) mod 3329 |

Parameter `REG_OUT`: 0 means purely combinational. 1 means `sum` and `diff` come from registers one clock later. Default 0.

**`modq_acc`** is a running sum.

| Port | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `clk`, `rst_n` | in | 1 each | Synchronous reset clears `acc` to 0 |
| `ld` | in | 1 | acc ← y |
| `add` | in | 1 | acc ← (acc + y) mod 3329 |
| `y` | in | 12 | Value to load or add, 0..3328 |
| `acc` | out | 12 | Registered running value |

If `ld` and `add` are both high, `ld` wins. If both are low, `acc` holds.

## Behavior and reduction rule

Every output is reduced to 0..3328 with one conditional correction. No division and no multiplication.

| Output | Rule |
| --- | --- |
| `sum` | s = x + y (up to 6656, 13 bits). If s ≥ 3329, subtract 3329 |
| `diff` | d = x − y (signed, 13 bits). If d < 0, add 3329 |
| `acc` on `add` | Same rule as `sum`, with x = `acc` and y = `y` |

- Inputs are always 0..3328. A larger input gives an undefined result but must not hang anything.
- A cheap way: compute the corrected and uncorrected value in parallel and pick one using the carry or sign bit.
- The chip needs both `sum` and `diff` from the same pair, so compute them together and share logic where it saves area.
- These modules are tiny next to the multiplier. Keep them that way: no extra modes, and no state in `modq_addsub`.

## How Control uses your modules

Control picks the operands and decides which result to use. You only need the modules to be correct for any x and y.

| Mode and stage | Module | Inputs | Result used |
| --- | --- | --- | --- |
| Forward NTT, Post ± | `modq_addsub` | x = a (bypass, delayed), y = t | `sum` = a' and `diff` = b' |
| Inverse NTT, Pre ± | `modq_addsub` | x = b, y = a | `sum` = a + b (to the bypass lane) and `diff` = b − a (to the multiplier) |
| Basemul, c0 | `modq_acc` | y = m2 on `ld`, then y = m3 on `add` | `acc` = c0 |
| Basemul, c1 | `modq_acc` | y = m5 on `ld`, then y = m4 on `add` | `acc` = c1 |
| Scale | none | no inputs | Control routes around the adders |

Notes:

- `diff` is always x − y. Pre ± uses x = b and y = a to get b − a, so the order matters.
- In basemul, m1 = f1 × g1 is not summed. It loops back into the multiplier to be multiplied by γ and become m2.
- The two `modq_addsub` instances (Pre and Post) are never active together. They could be merged later, so keep the module stateless.
- Pass-through (Pre ± in forward, basemul and scale; Post ± in inverse and scale) is a mux that Control builds. It is not part of your modules.

## Timing options

Combinational by default, with an optional output register.

- **Rate needed.** One add/subtract per 3 clocks in the NTT modes, and one accumulate per clock in basemul (the multiplier returns a product every clock). Anything that settles inside one clock is fast enough.
- **`REG_OUT`.** 0 gives latency 0 clocks. 1 gives latency 1 clock. Control picks per instance, depending on the multiplier's latency and the longest path. Support both.
- **`modq_acc`.** `acc` is valid 1 clock after `ld` or `add`.
- **Report** to the Control owner: the longest combinational path with `REG_OUT` = 0, the latency for each setting, and a rough cell count for each module.
- Small area beats speed here. The chip clock is not set yet, so do not spend gates on going faster than one clock.

## Tests and test vectors

- **`modq_addsub`, exhaustive.** All 3329 × 3329 pairs. `sum` equals (x + y) mod 3329 and `diff` equals (x − y) mod 3329. Run it for `REG_OUT` = 0 and 1.
- **`modq_acc`.** `ld` then `add` gives (y0 + y1) mod 3329. `acc` holds when both are low. `ld` wins when both are high. Run long random sequences against a software model. Reset clears `acc`.
- **Edge values.** x or y equal to 0 and to 3328.

| Case | Inputs | Expected |
| --- | --- | --- |
| Sum wraps to 0 | x = 3328, y = 1 | `sum` = 0 |
| Difference wraps | x = 0, y = 1 | `diff` = 3328 |
| Sum near the top | x = 3328, y = 3328 | `sum` = 3327 |
| Difference wraps | x = 1000, y = 2000 | `diff` = 2329 |
| Sum lands on 0 | x = 2000, y = 1329 | `sum` = 0 |
| Forward butterfly | x = a = 1000, y = t = 2498 | `sum` = 169, `diff` = 1831 |
| Inverse pre | x = b = 2000, y = a = 1000 | `sum` = 3000, `diff` = 1000 |
| Basemul c0 | `ld` 1547, then `add` 55 | `acc` = 1602 |
| Basemul c1 | `ld` 65, then `add` 77 | `acc` = 142 |
| Basemul c0, large values | `ld` 1148, then `add` 2493 | `acc` = 312 |
| Basemul c1, large values | `ld` 362, then `add` 413 | `acc` = 775 |

## Deliverables and done when

**Hand over:**

- `modq_addsub.v` and `modq_acc.v`
- A testbench with the tests above, runnable with one command
- A short README: latency of each module (0 or 1 clock), longest combinational path, rough cell count

**Done when:**

- The exhaustive addsub test passes for both `REG_OUT` settings.
- The accumulator tests pass against the software model.
- The Control owner has instantiated both modules without changing your ports.

**Do not:** add mode logic or op decoding. The pass muxes belong to the Control owner.

## Shared rules for all five specs

- **One chip, one clock.** Everything runs on `clk`. Reset is `rst_n`, active low. Synchronous logic only, no latches, no generated clocks.
- **Numbers.** Every coefficient is 12 bits, unsigned, always in 0..3328 on every port.
- **Ports are frozen.** Tell the Control owner before changing a port name or width.
- **Latency is reported.** State the clocks each block takes in your README.
- **Reference.** The Pi owner's Python model is the source of truth.
- **Code.** Plain Verilog-2001 or SystemVerilog the Tiny Tapeout flow accepts, one module per file.
- **Sharing.** These docs are private. Share them with your teammates.
