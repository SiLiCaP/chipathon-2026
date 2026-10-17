<!---

This file is used to generate your project datasheet. Please fill in the information below and delete any unused
sections.

You can also include images in this folder and reference them in the markdown. Each image must be less than
512 kb in size, and the combined size of all images must be less than 1 MB.
-->

## How it works

The chip computes the polynomial arithmetic of ML-KEM (FIPS 203, q = 3329) in four modes:
forward NTT, inverse NTT, basemul (pointwise multiply in the NTT domain) and scale by
128^-1. It has no storage. A host streams 12-bit coefficients in on fixed clocks, and
results come back as bytes on `uo_out` after a fixed delay, with no handshake.

Inside the chip, one modular multiplier is shared by all modes, together with modular
adders and two accumulators. The multiplier is pipelined with three rounds of K-RED
reduction and returns 2197 x a x b. `ntt_core` latches the op on the start-of-frame clock,
counts butterflies and basemul pieces, and picks every constant itself. A 128-entry ROM
holds the twiddles scaled by 2197^-1, which cancels the factor: zeta for the NTT layers,
gamma for basemul, and 304 for scale. The NTT outputs are plain FIPS 203 values. Basemul
outputs carry a factor 2197, which the scale step removes, so forward, basemul,
inverse and scale together give the exact product. The host only sends coefficients in
order.

| op (`uio[7:6]`) | Mode | Inputs per job | Result pairs |
| --- | --- | --- | --- |
| 00 | Forward NTT, 7 layers | 1792 (b then a per butterfly) | 896 |
| 01 | Inverse NTT, 7 layers | 1792 (b then a per butterfly) | 896 |
| 10 | Basemul | 512 (g1, f1, g0, f0 per piece) | 128 |
| 11 | Scale (x 304 x 2197 = x 3303 x 2197^-1) | 256 | 128 |

Each result pair (r0, r1) leaves as three bytes on consecutive clocks (ByteEncode12):
`r0[7:0]`, `{r1[3:0], r0[11:8]}`, `r1[11:4]`. Between results `uo_out` is 0x00.

## How to test

| Pin | Direction | Function |
| --- | --- | --- |
| `ui[7:0]` | in | Coefficient bits 7:0 |
| `uio[3:0]` | in | Coefficient bits 11:8 |
| `uio[4]` | in | valid: the data pins hold a coefficient this clock |
| `uio[5]` | in | sof: with valid, the first coefficient of a job |
| `uio[7:6]` | in | op: 00 forward NTT, 01 inverse NTT, 10 basemul, 11 scale (read on the sof clock) |
| `uo[7:0]` | out | Result bytes at a fixed delay, 0x00 otherwise |

Every `uio` pin is an input. Each result pair (r0, r1) leaves as three bytes on consecutive
clocks: `r0[7:0]`, `{r1[3:0], r0[11:8]}`, `r1[11:4]`.

The host drives `clk` and sets the input pins once per clock. Coefficients are 0..3328.
Send one unit per period with valid low on the other clocks: an NTT butterfly (b at
clock 0, a at clock 1) every 3 clocks, a basemul piece (g1, f1, g0, f0 at clocks 0..3)
every 15 clocks, a scale coefficient every 3 clocks. Read the first byte of each result
D clocks after the unit's first coefficient: D = 6 (forward), 7 (inverse), 20 (basemul)
and 9 (scale, counted from the first coefficient of the pair). For the NTT modes, send each
layer after the previous layer's results have come back; the chip keeps its counters while
valid is low. A new sof cancels a running job.

The cocotb tests in `test/` run a whole polynomial multiply (forward NTT of f and g, basemul,
inverse NTT, scale) and compare it with a schoolbook product.

## External hardware

A host that drives the clock and the 16 input pins and samples `uo_out` on every clock,
for example an RP2350 with PIO and DMA.
