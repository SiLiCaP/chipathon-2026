# Moving more ML-KEM steps into the chip: does it cut the pin traffic? (Oct 31, 2026)

For the team. Question (owner): bigger tiles leave room; if a step is small and cuts transfers over the pins to the
RP2350, put it on the chip. Answer in short: **the pins are not where the time goes, and the one big transfer item
(the matrix A, 6,048 of 8,640 bytes out) is best removed in firmware, for 0 um2.** No RTL was prototyped; nothing
in the list below is "clearly worth it and small". Tags: **measured** (yosys + abc on the sky130 hd tt_025C_1v80
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

Ranked by saving per um2: **A** (0 area, 0.27 ms chip + up to 0.57 ms feed), **D** (0.7k, 0.13 ms as written only),
**B** (3.5k, 0.03 ms pins + 0.1-0.28 ms CPU), **E** (12.2k, 0.31 ms, needs 4x2), **C** (16k, 0.15 ms as written only).

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

1. **Do not spend gates on transfer reduction in 3x2.** The best pin-side candidate (B) saves 0.03 ms of pin time.
2. **Firmware first:** cache A (candidate A), and build the PIO/DMA feed; as written the feed costs ~1.8 ms of a 4.57 ms exchange,
   the pins 0.26 ms. With A cached the model's exchange drops from 3.18 ms by roughly 0.3-0.4 ms (chip 0.27 + feed
   + rejection); the cache holds only while keygen / encaps / decaps are on one device (decaps with a stored key
   always profits: 4 of its 11 calls).
3. **If a bigger tile is on the table, use it for K = 16** (-0.31 ms, 10 %), after 4x2 closes hold timing.
4. **Candidate D** only if the firmware cannot be made to use PIO/DMA.
5. Open: a `valid` output pin would unlock B; ask whether a `uio` pin may become an output.

## 4. Prototype results

None. No candidate met "clearly worth it and small"; no 3x2 harden was run (today's 55.5 % / +8.17 ns stands).
Reproduce: `python3 scripts/offload_budget.py`; blocks: `research/offload/{sampler,seed_reg,autopad}.v` (area
only, not wired into the chip, not simulated). Sources read: `docs/tile_routes.md`, `docs/sha3_report.md`,
`docs/host_timing.md`, `docs/ntt_cache_study.md`, and from tt-viktor-chiptor (af81927) `docs/FPGA_DEMO.md`,
`sim/route_model.py` (call list, feed costs 23/15 vs 2/2).
