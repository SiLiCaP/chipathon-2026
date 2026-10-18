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

Next to the NTT core sits a Keccak-f[1600] sponge (`keccak_lat`) for SHA-3 and SHAKE. Its
1600-bit state lives in a latch array (25 lanes of 64 latches) and a 64-bit datapath
processes one lane per clock: one permutation takes 1,464 clocks (24 rounds of 61). A captured word with valid high and coefficient bits 11:8 =
0xF (never a valid coefficient) is a hash command instead of a coefficient; the NTT core
never sees it. The host does the padding and absorbs and squeezes whole 8-byte lanes; an
all-zero lane of a padded block can be passed over with one SKIP word.

| {sof, op} (`uio[5]`, `uio[7:6]`) | Hash command | Data (`ui[7:0]`) | Busy afterwards |
| --- | --- | --- | --- |
| 000 | Absorb: XOR the byte into the next state byte | byte | - |
| 001 | Squeeze: the next state byte on `uo_out` 2 clocks later | - | - |
| 010 | Permute, rewinds the byte pointer | - | 1,464 clocks |
| 011 | Clear the state, rewinds the byte pointer | - | 1 clock |
| 100 | Skip: the pointer moves to the next lane, which stays untouched (only at a lane boundary) | - | - |
| 101-111 | Reserved, no-op | - | - |

The host has no busy flag: it waits the busy clocks before the next hash command. Hash
outputs are ORed onto `uo_out` and are 0x00 when no squeeze is running, so NTT jobs and
hash jobs can share the pins as long as their output bytes don't fall on the same clocks.

## How to test

| Pin | Direction | Function |
| --- | --- | --- |
| `ui[7:0]` | in | Coefficient bits 7:0 |
| `uio[3:0]` | in | Coefficient bits 11:8 |
| `uio[4]` | in | valid: the data pins hold a coefficient (or, with bits 11:8 = 0xF, a hash command) this clock |
| `uio[5]` | in | sof: with valid, the first coefficient of a job |
| `uio[7:6]` | in | op: 00 forward NTT, 01 inverse NTT, 10 basemul, 11 scale (read on the sof clock) |
| `uo[7:0]` | out | Result bytes at a fixed delay, 0x00 otherwise |

Every `uio` pin is an input. Each result pair (r0, r1) leaves as three bytes on consecutive
clocks: `r0[7:0]`, `{r1[3:0], r0[11:8]}`, `r1[11:4]`.

The host drives `clk` and sets the input pins once per clock. Coefficients are 0..3328.
Send one unit per period with valid low on the other clocks: an NTT butterfly (b at
clock 0, a at clock 1) every 3 clocks, a basemul piece (g1, f1, g0, f0 at clocks 0..3)
every 5 clocks, a scale pair (c at clock 0, c′ at clock 1) every 3 clocks. Read the first
byte of each result D clocks after the unit's first coefficient: D = 6 (forward),
8 (inverse), 12 (basemul) and 7 (scale). For the NTT modes, send each
layer after the previous layer's results have come back; the chip keeps its counters while
valid is low. A new sof cancels a running job.

The cocotb tests in `test/` run a whole polynomial multiply (forward NTT of f and g, basemul,
inverse NTT, scale) and compare it with a schoolbook product. `test/test_hash.py` checks
SHA3-256, SHA3-512, SHAKE128 and SHAKE256 against Python's `hashlib`, and an NTT job
interleaved with a hash job.

## External hardware

A host that drives the clock and the 16 input pins and samples `uo_out` on every clock,
for example an RP2350 with PIO and DMA.
