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
368 clocks, 7.4 us at 50 MHz. The host absorbs bytes and squeezes bytes or whole 8-byte lanes; it pads the
message itself or lets Finish do it.

On the way out the chip can also run SamplePolyCBD (FIPS 203 Algorithm 8), so a PRF call returns
ML-KEM's secret and noise coefficients directly instead of raw bytes, and SampleNTT (Algorithm 7), so
an XOF call returns a polynomial of the matrix A as its 384 ByteEncode_12 bytes. On the way in it can
take a message as packets of bare bytes and do the SHA-3 padding itself.

A word on the pins with `uio[4]` = 1 and `uio[3:0]` = 0xF is a command; the command number is
{`uio[5]`, `uio[7:6]`} and the data byte is `ui[7:0]`.

| cmd | Command | Data (`ui[7:0]`) | Busy afterwards |
| --- | --- | --- | --- |
| 0 | Absorb: XOR the byte into the next state byte | byte | - |
| 1 | Squeeze: the next state byte on `uo_out` 2 clocks later | - | - |
| 2 | Permute, rewinds the byte pointer | - | 368 clocks |
| 3 | Clear the state, rewinds the byte pointer | - | 8 clocks |
| 4 | Skip: the pointer moves to the next lane, which stays untouched (only at a lane boundary) | - | - |
| 5 | Burst: the next 8n state bytes on `uo_out`, one per clock, the first 2 clocks later | {x, n}: n = 1..21, x = 0..7 more blocks | 8n clocks per block |
| 6 | Burst CBD: like Burst, each byte as SamplePolyCBD coefficients | {eta3, 1'b0, auto, n} | 8n clocks per block |
| 6 | Burst NTT: like Burst, the bytes go through SampleNTT (mode 1 only) | {1'b0, 1'b1, auto, n} | 8n + 3 clocks per block |
| 7 | Ext: one of the four commands below, picked by data bits 7:6 | | |

| Ext, `ui[7:6]` | Command | `ui[5:0]` | Busy afterwards |
| --- | --- | --- | --- |
| 00 | Reserved, no-op | - | - |
| 01 | Absorb N: the data bytes of the next n clocks are absorbed as they are, whatever `uio` says | n - 1 (n = 1..64) | n clocks |
| 10 | Finish: the chip pads the block from the byte pointer on (domain byte, zeros, 0x80) and permutes | function: 0 SHA3-256, 1 SHA3-512, 2 SHAKE128, 3 SHAKE256 | s + 1 + 368 clocks |
| 11 | Mode: set the framing mode | mode (0 or 1) | - |

Finish from byte pointer p in a block of `rate` bytes (136, 72, 168, 136 for the four functions) takes
s = 8 - p % 8 padding steps when p is in the block's last lane, else (8 - p % 8) + (rate / 8 - 2 - p / 8) + 8.
When a message fills its block exactly, send Permute first and then Finish (p = 0).

**Blocks that follow by themselves.** One burst word can cover a whole squeeze: after its n lanes the
chip permutes and bursts again, each block starting 8n + 3 + 368 clocks after the one before.
Burst: x more blocks of n lanes. Burst CBD with `auto` (eta = 3): the other 24 - n lanes, so n = 17 returns
all 192 bytes of PRF_3. Burst NTT with `auto`: blocks until the polynomial's 256 coefficients are out.
Hash words in between are ignored.

**Framing mode 1** (after Mode 1; reset returns to mode 0): a command is `uio[4]` = 1 alone, without the
0xF marker, and `uio[1:0]` become outputs. `uio[0]` = `valid`: high on every clock where `uo_out` carries
a byte. `uio[1]` = `busy`: high while the chip takes no command (Permute, Clear, a burst, Absorb N, Finish,
blocks that follow by themselves), from 2 clocks after the command word. Mode 1 itself is sent as a normal
mode 0 word, with the 0xF marker. The chip drives `uio[1:0]` from 2 clocks after it, both low; the host drives
them low on the idle words after Mode 1 and then stops driving them. Send at least one idle word after
every Mode word: the word right after it is still read in the old framing. In mode 1 the eta = 3 CBD
output is packed like eta = 2: of every three clocks the first has no byte (`valid` low), the next two
carry `{f[4j+1], f[4j]}` and `{f[4j+3], f[4j+2]}`, 128 bytes for PRF_3.

Burst NTT output: every pair of accepted coefficients (x, y), each below 3329, leaves as the three bytes
`x[7:0]`, `{y[3:0], x[11:8]}`, `y[11:4]`, on clocks marked by `valid`, the last ones up to 3 clocks after
the burst. The number of bytes per burst depends on the data. After 256 coefficients (384 bytes) the chip
sends nothing more until Clear, so the host bursts and permutes until it has 384 bytes. Like the eta = 3
grouping, the sampler's position carries on across Permute and restarts at Clear.

Burst CBD output (4-bit two's complement coefficients f[i] in -eta..eta): for eta = 2 each byte
gives `{f[2i+1], f[2i]}`; for eta = 3 every three bytes give `{0, f[4j]}`, `{0, f[4j+1]}`,
`{f[4j+3], f[4j+2]}`. The eta = 3 grouping carries on across a Permute and restarts at Clear, so
PRF_3 (192 bytes, more than SHAKE256's 136-byte rate) is two bursts with a Permute between them.

## How to test

| Pin | Direction | Function |
| --- | --- | --- |
| `ui[7:0]` | in | Data byte |
| `uio[3:0]` | in | 0xF: marks a command word (mode 0). Mode 1: unused, and `uio[0]` is the output `valid`, `uio[1]` the output `busy` |
| `uio[4]` | in | valid: a command this clock |
| `uio[5]`, `uio[7:6]` | in | command bit 2, bits 1:0 |
| `uo[7:0]` | out | Squeezed bytes (or CBD coefficients) 2 clocks after the word that asked for them, 0x00 otherwise |

After reset every `uio` pin is an input. The host drives `clk`, sets the input pins once per clock and
samples `uo_out` every clock. In mode 0 it has no busy flag: it waits the busy clocks before the next
command, and sends idle words (valid low) during a burst. A command sent during Permute, Clear or the
permute of Finish is not ignored cleanly: the state does not take it, but the byte pointer and the
samplers do, so later results are wrong until the next Clear. Burst and Burst CBD need n = 1..21: n = 0
streams 256 bytes. In the up to 3 clocks after a Burst NTT, while its last bytes leave, `busy` is still
high but Permute is taken; Squeeze and Clear must wait. Every command has a fixed length; only the
number of Burst NTT output bytes depends on the data.

The cocotb tests in `test/` check SHA3-256/512 and SHAKE128/256 against Python's `hashlib`
(byte and burst squeeze, with and without Skip, with Finish and Absorb N), Burst CBD against FIPS 203
SamplePolyCBD for eta = 2 and 3, Burst NTT against FIPS 203 SampleNTT in framing mode 1, and the
blocks that follow by themselves with the `busy` pin.
`test/hash_jobs.py` builds the pin words; the same file drives the FPGA rig.

## External hardware

A host that drives the clock and the 16 input pins and samples `uo_out` on every clock
(in framing mode 1: 14 input pins, and it also samples `uio[1:0]`), for example an RP2350 with PIO and DMA.
