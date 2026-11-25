# Speed results: the hash chip against the RP2350 (Nov 25, 2026)

Measured with the firmware's `speedtest`, `bench` and `bench compute` (`rp2350/core/hash_bench.c`) through
`rp2350/speedtest.py`. Every chip result in every run was compared with the software result: **0 errors**. Raw data:
[rp2350/results/](../rp2350/results/README.md). Method: [getting_started.md](getting_started.md#the-speed-test).

**Setup:**
- **The chip:** the **hardened gate netlist** of `main` (a local LibreLane build of `60b31b8`: 74.1 %, 0 DRC / LVS /
  antenna, +6.35 ns setup at 50 MHz in the slow corner), running on a Tang Nano 20K.
- **The host:** the RP2350 firmware on a Pico 2 W wired pin for pin to it, the same firmware and the same RP2350 as
  on the Tiny Tapeout demo board. RP2350 at 150 MHz unless noted.
- **Checks on this rig:** the gate netlist passes `selftest`, `fuzz 200` and the `ci` check (the ML-KEM-512 hash
  sequence, 32 jobs, 0 failures).

**Workload:** the 32 hash calls of one ML-KEM-512 key exchange: G, H, J, 12 × XOF with SampleNTT, 16 × PRF with
SamplePolyCBD.

## 1. Compute only: the chip against the code that does the same work

`bench compute` times each piece of C code alone on the RP2350 (portable FIPS 202 / 203 code, `core/sha3_sw.c`). It
counts the chip's clocks for the same work, with CBD and SampleNTT on the chip (framing mode 1), so both sides compute
the same thing. No transfers on either side.

| Work | C on the RP2350 | Chip clocks | Chip on the FPGA (18.75 MHz) | Chip at 50 MHz | Speed-up at 50 MHz |
|---|---|---|---|---|---|
| One Keccak-f[1600] permutation | 410.5 µs | 368 | 19.6 µs | 7.4 µs | **56×** |
| G = SHA3-512 of 64 B | 432 µs | 518 | 27.6 µs | 10.4 µs | 42× |
| H = SHA3-256 of 800 B | 2,614 µs | 3,091 | 165 µs | 61.8 µs | 42× |
| J = SHAKE256 of 800 B, 32 out | 2,614 µs | 3,091 | 165 µs | 61.8 µs | 42× |
| PRF_3 + SamplePolyCBD, one polynomial | 972 µs | 1,004 | 53.5 µs | 20.1 µs | 48× |
| PRF_2 + SamplePolyCBD, one polynomial | 525 µs | 569 | 30.3 µs | 11.4 µs | 46× |
| XOF + SampleNTT, one polynomial | 1,346 µs | 1,700 | 90.7 µs | 34.0 µs | 40× |
| SamplePolyCBD alone (η = 3) | 118.7 µs | 0 | on the output stream | | |
| SampleNTT alone | 42.9 µs | 0 (a 3-clock tail) | on the output stream | | |
| **One ML-KEM-512 exchange (32 calls)** | **39.76 ms** | **43,736** | **2.33 ms** | **0.87 ms** | **46×** |

- **CBD and SampleNTT cost the chip no clocks of their own.** They work on the bytes as they stream out of the
  squeeze; SampleNTT only adds a 3-clock tail.
- **The chip's clock counts are exact.** They come from the gate netlist itself, and the chip's calls have fixed
  lengths. They're the same at any clock: 50 MHz just divides them by 50 MHz.
- **With the RP2350 at 250 MHz,** the C code is faster (one exchange 23.9 ms) and the chip at 50 MHz is still 27×
  faster.
- **Against hand-optimized software.** pqm4-class assembly does the exchange's hashing in about 5.8 ms on a 150 MHz
  RP2350 (cited, [tile_routes.md](tile_routes.md)). The chip's 0.87 ms is 6.6× faster than that.

## 2. The whole system: everything the RP2350 waits for

`bench` times the hashing of one exchange end to end: building the pin words, the PIO + DMA transfers, the chip,
unpacking, checking every clock. The chip is driven the fastest way known: framing mode 0, packets with on-chip
padding, one burst word per squeeze, SampleNTT on the RP2350.

| | Time | vs C |
|---|---|---|
| RP2350 alone, C (measured) | 39.8 ms | |
| **FPGA, measured** (chip at 18.75 MHz) | **5.97 ms** | **6.7×** |
| **Real chip, guesstimate:** demo board, chip at 50 MHz | **≈ 2.85 ms** | **≈ 14×** |
| Real chip, guesstimate: chip at 66 MHz | ≈ 2.6 ms | ≈ 15× |
| RP2350 at 250 MHz, FPGA measured (chip at 19.23 MHz) | 4.50 ms against 23.9 ms | 5.3× |

**The guesstimate** keeps the RP2350's measured work: 1.80 ms of driver and 0.11 ms of transfer set-up. It drops what
exists only on the FPGA rig, 1.55 ms of reshuffling every word onto the Pico's split GPIOs (the demo board's 16
input pins are consecutive). And it runs the chip's 47,004 clocks at its own speed. Against hand-optimized assembly
(≈ 5.8 ms), the whole real system would still be about 2× faster.

**Raw rates** (FPGA, 18.75 MHz): absorb 0.30 MB/s in C against 2.26 MB/s with the chip; squeeze 0.39 against
2.77 MB/s. The chip alone does 4.9 / 5.7 MB/s at this clock, and 13 / 15 MB/s at 50 MHz.

## 3. What changed in the firmware

The chip is the same gate netlist throughout. Only the RP2350 firmware changed. The driver's work per chip clock
dropped from about 47 to about 6 RP2350 cycles:

| Step (rig, RP2350 at 150 MHz, chip at 18.75 MHz) | Whole time | vs C |
|---|---|---|
| First measurement (framing mode 1, every newer command) | 25.2 ms | 1.58× |
| Idle runs written with `memset`, the quiet check four clocks at a time | 19.7 ms | 2.02× |
| Framing mode 0 with packets, on-chip padding and self-continuing bursts | 12.6 ms | 3.16× |
| Unpacking only the clocks that carry bytes, clearing only what was used, idle words fast-pathed on the rig | 7.2 ms | 5.55× |
| Checking the samples while the chip is still running | 6.36 ms | 6.27× |
| The firmware running from SRAM instead of flash (`TT_COPY_TO_RAM`, now the default) | **5.97 ms** | **6.67×** |

Every step kept the checks exact:
- the fast quiet / valid check was fuzzed against the exact per-clock rule: 200,000 runs with injected faults, none
  missed, no false alarms;
- the PC selftest, fuzz and golden vectors pass;
- the driver's pin stream replays clock for clock through the RTL (`make -C test rp2350`);
- `selftest`, `fuzz 200` and `ci` pass on the rig.

### Three ways to drive the chip (final firmware)

| Driven | chip clocks | Whole time | vs C |
|---|---|---|---|
| **Framing mode 0, packets + on-chip padding + one burst word, SampleNTT on the RP2350** (`bench`, the default) | 47,004 | **5.97 ms** | **6.67×** |
| The October 2026 way: host padding, a word per byte, a burst word per block (`bench legacy`) | 46,888 | 7.44 ms | 5.34× |
| Framing mode 1, SampleNTT on chip (`bench mode1`) | 48,152 | 15.2 ms | 2.62× |

**Framing mode 1 is slower with this firmware.** The RP2350 samples 16 bits per clock instead of 8 and splits out
the `valid` and `busy` pins, which costs more than moving SampleNTT on chip saves. Reading those pins only where
output can come would fix it; that's left for later, together with building the next call's words while the
current one plays.

## 4. Wire speed

| Board | Passes up to | Fails from | Notes |
|---|---|---|---|
| Pico rig, RP2350 at 150 MHz | 18.75 MHz | 25 MHz | skew 0 only; no skew helps above, so the inputs set the limit |
| Pico rig, RP2350 at 250 MHz | 19.23 MHz | 25 MHz | the sweep tried up to 125 MHz and skews 0-3 |

The rig's limit is its jumper wires. **The real chip on its PCB will be faster**, and the speed test is built for it:
- it tries every clock the RP2350 can make, up to half its system clock;
- if even the fastest passes, `speedtest.py` raises the RP2350 to 200 and then 250 MHz (core at 1.20 V) and sweeps
  again, up to 125 MHz;
- at every clock it searches sample skews 0-3, because the pad, mux and clock-insertion delays (up to about 10 ns on
  Tiny Tapeout) push output bytes a clock or more later at high clocks.
