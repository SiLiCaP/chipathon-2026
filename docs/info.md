<!---

This file is used to generate your project datasheet. Please fill in the information below and delete any unused
sections.

You can also include images in this folder and reference them in the markdown. Each image must be less than
512 kb in size, and the combined size of all images must be less than 1 MB.
-->

## How it works

The chip computes the polynomial arithmetic of ML-KEM (FIPS 203, q = 3329): forward NTT,
inverse NTT, basemul and scale. A host streams 12-bit coefficients in on fixed clocks and
reads results back as bytes on `uo_out` at a fixed delay. There is no handshake.

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

## External hardware

List external hardware used in your project (e.g. PMOD, LED display, etc), if any
