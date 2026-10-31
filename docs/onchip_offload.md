# Moving more ML-KEM steps into the chip: does it cut the pin traffic? (Oct 31, 2026)

For the team. Question (owner): bigger tiles leave room; if a step is small and cuts transfers over the pins to the
RP2350, put it on the chip. Answer in short: **transfers are the bottleneck once the RP2350 has to feed the chip
(as written 3x the permutation time, up to ~10x in the worst case; section 1b), so transfer cuts weigh much more than
raw pin clocks suggest.** The cheapest big cut is firmware (cache A, 0 um2); on the chip the only candidate with
real weight is on-chip SampleNTT, and it is blocked by the missing `valid` pin. No RTL was prototyped.
Method: tt-viktor-chiptor's own model (`sim/route_model.py` `hash_words`, `perms`, `FEED`, `CORES`, `SETUP_S`;
`sim/config.py` fw_* costs; `docs/ESTIMATES.md`), run unchanged for the hash-only chip. Tags: **measured** (yosys + abc on the sky130 hd tt_025C_1v80
liberty, blocks alone, `research/offload/*.v`), **model** (`scripts/offload_budget.py`, built on `test/hash_jobs.py`
words, same call list as tt-viktor-chiptor `sim/route_model.py`; the rig's measured 42,900 chip clocks per
exchange in `docs/FPGA_DEMO.md` there matches the 42,432 here), **estimate**.

## 1. Transfer budget, one exchange (keygen + encaps + decaps, 32 calls), burst squeeze + SKIP

One pin word per clock in (a data byte), one byte per clock out.

| Step | Calls | Message in (B) | Words in, padded blocks (B) | Out (B) | Permutations | Chip clocks |
|---|---|---|---|---|---|---|
| G (SHA3-512) | 3 | 161 | 216 | 192 | 3 | 1,533 |
| XOF, 4 polys of A, 3x | 12 | 408 | 2,016 | 6,048 | 36 | 20,244 |
| PRF eta=3 (CBD out) | 8 | 264 | 1,088 | 1,536 | 16 | 8,016 |
| PRF eta=2 (CBD out) | 6 | 198 | 816 | 768 | 6 | 3,414 |
| H(ek) | 2 | 1,600 | 1,632 | 64 | 12 | 6,150 |
| J(z, c) | 1 | 800 | 816 | 32 | 6 | 3,075 |
| **Total** | 32 | **3,431** | 6,584 (SKIP sends ~4.3k words) | **8,640** | **79** | **42,432 (0.85 ms @ 50 MHz)** |

### 1b. Corrected budget: viktor's model, hash-only chip, K = 8, burst + CBD, per exchange

`route_model.py` (I/O clocks, permutation clocks, host words and bytes; 300 CPU cycles setup per call; RP2350 150 MHz):

| Quantity | Value |
|---|---|
| Chip I/O (pin) clocks | 13,137 (4,561 host words in, 8,640 bytes out; with byte squeeze, no burst: 13,137 words) |
| On-chip compute clocks (79 permutations x 368) | 29,072 |
| Pin time / compute time at **any** f_chip (lockstep) | **0.45** (0.26 ms vs 0.58 ms at 50 MHz; 0.97 vs 2.15 ms at the rig's 13.5 MHz) |
| RP2350 feed, firmware as written (23 cyc/word, 15/byte, 1/idle clock) | **1.56 ms** (burst), 2.88 ms (byte squeeze) |
| RP2350 feed, optimized PIO/DMA (2 + 2) | 0.18 ms (burst), 0.29 ms (byte squeeze) |
| Setup, 32 calls x 300 cycles | 0.064 ms |

Pin + feed + setup against compute (0.58 ms):

| Case | Transfer time | Ratio to compute |
|---|---|---|
| ideal lockstep, f_chip any | 0.26 ms | 0.45x |
| optimized PIO/DMA feed, burst | 0.50 ms | 0.9x |
| firmware as written, burst | 1.88 ms | **3.2x** |
| firmware as written, byte squeeze | 3.2 ms | 5.6x |
| K = 16 (compute 0.27 ms), as written, byte squeeze | 3.2 ms | **~11x** |

So the "about 10x" reproduces only when the host feed is as written (in `docs/ESTIMATES.md` it is the NTT chip,
"CPU-bound by about 10x") and the compute is made small; for the K = 8 hash chip with bursts it is ~3x as written and
~1x with PIO/DMA. Because the chip has to be fed word by word, **the price of a byte over the pins is
~120 ns (out) / ~170 ns (in) as written, ~33 ns optimized, against 20 ns ideal.** That price list is used below.
With this budget the whole exchange, as written, is 4.57 ms: pins + feed 1.9 ms (41 %), compute 0.58 ms (13 %),
the RP2350's NTT and polynomial work the rest. The earlier rule "pins are 8 % of the exchange" holds only for
the ideal 1 B/clk feed.

### 1a. Detail (ideal pin clocks, my count)
Where the chip's time goes (model, 50 MHz): permutations 79 x 369 = 29.2k clocks (**69 %**, 0.58 ms), bytes out
8.6k (20 %, 0.17 ms), words in ~4.3k (10 %, 0.09 ms), clears ~0.3k. **All pin traffic together is ~31 % of chip time,
0.26 ms of the 3.18 ms exchange (8 %).** The hashing as a whole is 1.09 ms of 3.18; the other ~2.1 ms is the
RP2350's NTT / polynomial work (tile_routes.md section 5), which does not cross the pins at all.
Time by who does it: core (permutations) 0.58 ms; pins 0.26 ms; RP2350 feed + setup ~0.25 ms with the optimized
PIO/DMA feed (2 cycles per word and per byte), but **~1.8 ms with the firmware as written** (23 cycles per word in,
15 per byte out, 1 per idle clock: ~271k cycles), of which the 8,640 output bytes alone are ~130k cycles (0.87 ms).
So the CPU feed, not the pins, decides; one byte less out is worth 15 cycles as written, 2 optimized.

Rule of thumb: at 1 byte per clock each way, **every byte that stays off the pins is one chip clock (20 ns) and
2-23 CPU cycles**. Cutting the whole 12 kB payload is worth at most 0.26 ms + feed.

## 2. Candidates

Savings per exchange. "Feed" = RP2350 cycles at 23/15 (as written) and 2/2 (optimized). Area: yosys + abc, block alone
(the chip is 48.6k um2 synthesized; 3x2 core is 110.9k um2 at 55.5 %, so ~49k um2 is free today).

| # | Candidate | Out / in saved | Chip clocks | RP2350 saved | Area | Fits 3x2? |
|---|---|---|---|---|---|---|
| A | **Keep A in firmware** (cache the 4 polys, 1.5 kB packed 12-bit, across keygen / encaps / decaps of one device; 8 of 12 XOF calls vanish) | -4,032 B out, -0.3k in | **-13.5k (-32 %, 0.27 ms)**, -24 permutations | feed -85k cyc as written (0.57 ms) / -9k optimized; plus 8 of 12 rejection-sampling runs (-28k instr, clean C) | **0** (1.5 kB RAM) | n/a |
| B | On-chip SampleNTT: rejection + 12-bit packing, A comes out as 384 B per poly instead of 504 | -1,440 B out (-17 %) | -1.4k (0.03 ms) | rejection loop 42k instr (clean C, <= 0.28 ms; pqm4 scale ~0.1 ms) + feed -22k as written | **3.5k** (measured, comparator, assembler, 24-bit pack; the bare sampler without the missing handshake) | yes, but see below |
| C | Seed registers: keep rho and sigma / r on chip (2 x 256 b) plus a nonce counter, no re-absorb | -~960 B in (18 PRF, 12 XOF seeds) | 0 (the replay still clocks 1 byte per clock into the ring) | -22k as written / -2k optimized | **16.0k** (measured, mostly 512 flops) | yes |
| D | FINISH command: the chip pads (domain byte, zeros, 0x80 at rate-1) | -~870 words of padding / SKIP | 0 | -19k as written / ~0 optimized | **0.7k** (measured, sequencer + rate compare; hookup into hash_io not counted) | yes |
| E | K = 16 core (not a transfer cut: halves the permutation, the 69 %) | 0 | **-15.5k (0.31 ms)** | 0 | +12.2k synthesized (60.8k vs 48.6k) | **no, needs 4x2**; today 4x2 fails hold with the 0.14 ns SDC |

Saving per exchange in time, with viktor's byte prices (as written / optimized feed), including pins and permutations:

| # | Candidate | Time saved as written | Optimized feed | Area | ms (as written) per 1k um2 |
|---|---|---|---|---|---|
| A | Keep A in firmware | 0.27 + 0.57 = **0.84 ms** (+ rejection 0.19 ms clean C) | **0.33 ms** (+ 0.07 ms) | 0 | inf |
| D | FINISH (pad on chip) | 0.15 ms | 0.03 ms | 0.7k | 0.21 |
| B | On-chip SampleNTT + packing | 1,440 B x 120 ns = 0.17 ms + rejection 0.28 ms (clean C) = **0.45 ms** | 0.05 + ~0.10 = **0.15 ms** | 3.5k | 0.13 |
| E | K = 16 core | 0.31 ms | 0.31 ms | 12.2k | 0.025 |
| C | Seeds + nonce counter on chip | 960 x 170 ns = 0.17 ms | 0.03 ms | 16.0k | 0.011 |

Ranking: **A, D, B, E, C**. Under the corrected prices B and D move up, but each is still 1-5 % of the exchange.
Compressed / smaller outputs: the CBD output is already 4 bits per coefficient (eta = 3 could pack 192 -> 128 B, 512 B
in all, 0.06 ms as written, with output gaps); ByteEncode / compress act on host data and do not shrink what crosses the pins.
A wider or valid-flag protocol: a `valid` output pin does not add bandwidth, it unlocks B (see below); a wider
output has no pins (8 `uo`, all `uio` inputs).

Why B is not a clean win. A rejection sampler's output rate depends on the data, and the chip has no valid / busy
pin (`uio` are all inputs, `uo_out` is 0 when idle, and 0 is also a legal byte). Options: a tagged 2-byte
coefficient (no byte saving, CPU saving only); a count pass with a pointer rewind (cmd 7 is free, but the extra
read pass costs ~168 clocks per block, more than the 1.4k saved); or one `uio` pin turned into a `valid` output
(changes the rig wiring and the command decode). The bytes saved are only the 18.7 % rejects plus the overshoot
(336 candidates read, 256 needed); the permutations (the real cost) stay.
Why C is poor: the ring state cannot be loaded in parallel, so the 32 bytes still take 32 clocks; only the host
words go away, for 16k um2. Why D and E are listed: D is the cheapest chip-side gain if the firmware stays as
written; E is the best use of a 4x2 tile, and it is the permutation, not a transfer.

Not worth it (the data lives on the RP2350 and the pins cost more than the step; profile in tile_routes.md section 5):
ByteEncode / compress / decompress (outputs do not shrink on the way through: eta=3 CBD could pack 192 -> 128 B
but with data-dependent gaps, 512 B total), polynomial add / reduce, NTT (a poly is 384 B each way, 3,072 flops to
hold it: no tile has the room), FO compare. The two absorb-heavy inputs, ek (800 B, in H) and c (800 B, in J), are
host data and are the floor.

## 3. Recommendation

1. **Cache A in firmware and build the PIO/DMA feed first.** Together they remove ~1.5 ms of the as-written 4.57 ms
   (feed 1.56 -> 0.18 ms; A cache another 0.3-0.8 ms); no gate does that.
2. **Candidate B (3.5k um2, fits 3x2's free ~49k um2 with room to spare) is the only on-chip transfer cut worth a
   prototype, and only after the pin question is settled:** it needs a `valid` output (one `uio` pin as output) or the
   tagged 2-byte format. Without the pin its pin saving is ~0 and only the 0.1-0.28 ms of CPU remains.
3. **D (0.7k um2)** is nearly free but only matters while the feed is as written; do it with the next RTL change, not alone.
4. **E (K = 16) is the best use of 4x2** (-0.31 ms), after 4x2 closes hold timing. C is not worth its 16k um2.
5. Nothing here needs 4x2 except E.

## 4. Prototype results

None. Under the corrected budget no candidate met "clearly worth it and small"; no 3x2 harden was run (today's 55.5 % / +8.17 ns stands).
Reproduce: `python3 scripts/offload_budget.py`; blocks: `research/offload/{sampler,seed_reg,autopad}.v` (area
only, not wired into the chip, not simulated). Sources read: `docs/tile_routes.md`, `docs/sha3_report.md`,
`docs/host_timing.md`, `docs/ntt_cache_study.md`, and from tt-viktor-chiptor (af81927) `docs/FPGA_DEMO.md`,
`sim/route_model.py` (call list, feed costs 23/15 vs 2/2).
