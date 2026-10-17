# Adders (Spec 4)

`src/modq_addsub.v` and `src/modq_acc.v`, with the ports and behavior of
[Spec 4](../specs/Spec%204%20Adders.md). Every result is in 0..3328.

## Report for the Control owner

| Module | Latency | Longest path (sky130 hd, pre-PnR) | Size (LibreLane synthesis, `AREA 0`) |
| --- | --- | --- | --- |
| `modq_addsub`, `REG_OUT = 0` | 0 clocks | x / y to `sum` / `diff`: 6.9 ns slow corner (ss, 100 °C, 1.60 V), 3.4 ns typical | about 180 cells, 1,690 µm² |
| `modq_addsub`, `REG_OUT = 1` | 1 clock | same logic, ends in a register | plus 24 flip-flops, about 510 µm² |
| `modq_acc` | `acc` valid 1 clock after `ld` / `add` | `acc` and `y` registers back to `acc`: 7.1 ns slow corner, 3.6 ns typical (clock-to-Q and setup included) | about 140 cells, 1,480 µm² including its 12 flip-flops |

Both fit easily in one 20 ns clock (50 MHz). In the full chip at 50 MHz, before
place and route, the adder paths keep at least 10.5 ns of slack at the slow
corner. The chip's worst path is in the multiplier, not here.

## How it works

`modq_addsub` uses one plain adder and one plain subtractor, each followed by
one conditional correction. Because q = 3329 = 0xD01, adding or subtracting q
only changes the low byte by 1 and the high nibble by 13:

```
s = x + y                       d = x - y
s >= q   : s[12:8] > 13, or s[12:8] == 13 and s[7:0] != 0
s - q    = {s[12:8] - 13 - (s[7:0] == 0), s[7:0] - 1}
d < 0    : d[12]
d + q    = {d[11:8] + 13 + (d[7:0] == 255), d[7:0] + 1}
```

`modq_acc` uses the same `sum` correction on `acc + y`.

## Choosing the design: fast and small

Each candidate was synthesized with LibreLane 3.0.14 as part of the full chip,
then timed at the slow corner (50 MHz).

| Design | Chip area | Cells | Slack into packer (Post ±) | Slack into acc0 / acc1 |
| --- | --- | --- | --- | --- |
| Textbook: s − 3329 and d + 3329 as full 13-bit adders (old placeholder) | 30,906 µm² | 3,018 | 8.58 ns | 10.42 / 10.90 ns |
| **Low-byte / high-nibble correction (this design)** | **30,758 µm²** | **2,959** | **10.51 ns** | **11.10 / 10.88 ns** |
| s − q and d + q computed straight from x, y in parallel | 30,745 µm² | 2,984 | 8.93 ns | 11.88 / 10.77 ns |
| Kogge-Stone adders with a carry-save row for ±q (`adder_mod` style) | 32,322 µm² | 3,217 | 8.25 ns | 11.30 / 12.14 ns |

- The chosen design is the smallest of these within 0.05% of total chip area,
  and has the most slack on the paths it drives.
- Explicit prefix structures (Kogge-Stone, Sklansky) and carry-select splits
  at bit 4 or bit 8 did not survive ABC's area mode. They came out larger and
  no faster.
- ABC's results move by about ±5% with small rewrites of the same logic, so
  only differences measured in the full chip count.

The bigger saving is in Control, not in these modules. Pre ± and Post ± are
never active together, so one shared `modq_addsub` instance would save about
1,300 µm² net.

## Tests

`make -C test/adders` (quick, a few seconds) or `make -C test/adders exhaustive`
(all 3329 × 3329 pairs, about 2 minutes; this is what CI runs). The test
checks:

- `modq_addsub` with `REG_OUT` = 0 and 1 side by side, the Spec 4 vector table,
  and the edge values 0 and 3328.
- `modq_acc`: the Spec 4 vectors, `ld` winning over `add`, hold, reset, and
  200,000 random operations against a model.

The testbench stops with `$fatal` on any error, so `make` fails.
