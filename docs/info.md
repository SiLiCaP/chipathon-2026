<!---

This file is used to generate your project datasheet. Please fill in the information below and delete any unused
sections.

You can also include images in this folder and reference them in the markdown. Each image must be less than
512 kb in size, and the combined size of all images must be less than 1 MB.
-->

## How it works

The chip is a Keccak-f[1600] sponge engine for the hash calls of ML-KEM (FIPS 203): SHA3-256 (H),
SHA3-512 (G), SHAKE128 (XOF, matrix A) and SHAKE256 (PRF, J). On a microcontroller these calls are
about 57 % of ML-KEM-512's work; the polynomial arithmetic stays in software. The 1600-bit state
lives in clock-gated shift rings (`keccak_ring`) and moves 8 slices per clock: one permutation takes
368 clocks, 7.4 us at 50 MHz. The host pads the message and absorbs and squeezes whole 8-byte lanes.

On the way out the chip can also run SamplePolyCBD (FIPS 203 Algorithm 8), so a PRF call returns
ML-KEM's secret and noise coefficients directly instead of raw bytes.

A word on the pins with `uio[4]` = 1 and `uio[3:0]` = 0xF is a command; the command number is
{`uio[5]`, `uio[7:6]`} and the data byte is `ui[7:0]`.

| cmd | Command | Data (`ui[7:0]`) | Busy afterwards |
| --- | --- | --- | --- |
| 0 | Absorb: XOR the byte into the next state byte | byte | - |
| 1 | Squeeze: the next state byte on `uo_out` 2 clocks later | - | - |
| 2 | Permute, rewinds the byte pointer | - | 368 clocks |
| 3 | Clear the state, rewinds the byte pointer | - | 8 clocks |
| 4 | Skip: the pointer moves to the next lane, which stays untouched (only at a lane boundary) | - | - |
| 5 | Burst: the next 8n state bytes on `uo_out`, one per clock, the first 2 clocks later | n (1..21) | 8n clocks |
| 6 | Burst CBD: like Burst, each byte as SamplePolyCBD coefficients | {eta3, 2'b0, n} | 8n clocks |
| 7 | Reserved, no-op | - | - |

Burst CBD output (4-bit two's complement coefficients f[i] in -eta..eta): for eta = 2 each byte
gives `{f[2i+1], f[2i]}`; for eta = 3 every three bytes give `{0, f[4j]}`, `{0, f[4j+1]}`,
`{f[4j+3], f[4j+2]}`. The eta = 3 grouping carries on across a Permute and restarts at Clear, so
PRF_3 (192 bytes, more than SHAKE256's 136-byte rate) is two bursts with a Permute between them.

## How to test

| Pin | Direction | Function |
| --- | --- | --- |
| `ui[7:0]` | in | Data byte |
| `uio[3:0]` | in | 0xF: marks a command word |
| `uio[4]` | in | valid: a command this clock |
| `uio[5]`, `uio[7:6]` | in | command bit 2, bits 1:0 |
| `uo[7:0]` | out | Squeezed bytes (or CBD coefficients) 2 clocks after the word that asked for them, 0x00 otherwise |

Every `uio` pin is an input. The host drives `clk`, sets the input pins once per clock and
samples `uo_out` every clock. It has no busy flag: it waits the busy clocks before the next
command, and sends idle words (valid low) during a burst. Every command has a fixed length.

The cocotb tests in `test/` check SHA3-256/512 and SHAKE128/256 against Python's `hashlib`
(byte and burst squeeze, with and without Skip) and Burst CBD against FIPS 203 SamplePolyCBD
for eta = 2 and 3. `test/hash_jobs.py` builds the pin words; the same file drives the FPGA rig.

## External hardware

A host that drives the clock and the 16 input pins and samples `uo_out` on every clock,
for example an RP2350 with PIO and DMA.
