# Speed results: the hash chip against the RP2350 (Nov 25, 2026)

Measured with the firmware's `speedtest`, `bench` and `bench compute` (`rp2350/core/hash_bench.c`) through
`rp2350/speedtest.py`. Every chip result in every run was compared with the software result: **0 errors**. Raw data:
[rp2350/results/](../rp2350/results/README.md). Method: [getting_started.md](getting_started.md#the-speed-test).
Speed-ups against the software are in brackets.

**Setup:**
- **The chip:** the **hardened gate netlist** of `main` (a local LibreLane build of `60b31b8`: 74.1 %, 0 DRC / LVS /
  antenna, +6.35 ns setup at 50 MHz in the slow corner), running on a Tang Nano 20K.
- **The host:** the RP2350 firmware on a Pico 2 W wired pin for pin to it, the same firmware and the same RP2350 as
  on the Tiny Tapeout demo board, running from SRAM. RP2350 at 150 MHz unless noted.
- **Checks on this rig:** the gate netlist passes `selftest`, `fuzz 200` and the `ci` check (the ML-KEM-512 hash
  sequence, 32 jobs, 0 failures).

**The software baseline is the fastest available for this CPU:**
- Keccak-f[1600] in XKCP's ARMv7-M assembly (`rp/keccak_armv7m.s`, public domain, the implementation pqm4 uses on
  Cortex-M): 73 µs, about 11,000 cycles, per permutation;
- the sponge on top of it absorbs and squeezes whole blocks;
- SamplePolyCBD the way the Kyber reference code does it (bits added 32 / 24 at a time);
- SampleNTT as in FIPS 203.

The PC build uses the same code in portable C. The first measurements of the day (section 3) used portable C on the
RP2350 too, which is about 5.6× slower.

**Workload:** the 32 hash calls of one ML-KEM-512 key exchange: G, H, J, 12 × XOF with SampleNTT, 16 × PRF with
SamplePolyCBD.

## 1. Compute only: the chip against the software that does the same work

`bench compute` times each piece of software alone on the RP2350 and counts the chip's clocks for the same work.
CBD and SampleNTT run on the chip (framing mode 1), so both sides compute the same thing. No transfers on either
side.

| Work | RP2350 software | Chip clocks | Chip on the FPGA (18.75 MHz) | Real chip (50 MHz) |
|---|---|---|---|---|
| One Keccak-f[1600] permutation | 73.0 µs | 368 | 19.6 µs (3.7×) | 7.4 µs (9.9×) |
| G = SHA3-512 of 64 B | 81.8 µs | 518 | 27.6 µs (3.0×) | 10.4 µs (7.9×) |
| H = SHA3-256 of 800 B | 480.5 µs | 3,091 | 164.9 µs (2.9×) | 61.8 µs (7.8×) |
| J = SHAKE256 of 800 B, 32 out | 480.5 µs | 3,091 | 164.9 µs (2.9×) | 61.8 µs (7.8×) |
| PRF_3 + SamplePolyCBD, one polynomial | 195.8 µs | 1,004 | 53.5 µs (3.7×) | 20.1 µs (9.7×) |
| PRF_2 + SamplePolyCBD, one polynomial | 116.5 µs | 569 | 30.3 µs (3.8×) | 11.4 µs (10.2×) |
| XOF + SampleNTT, one polynomial | 290.3 µs | 1,700 | 90.7 µs (3.2×) | 34.0 µs (8.5×) |
| SamplePolyCBD alone (η = 3) | 36.0 µs | 0 | on the output stream | |
| SampleNTT alone | 43.0 µs | 0 (a 3-clock tail) | on the output stream | |
| **One ML-KEM-512 exchange (32 calls)** | **8.17 ms** | **43,736** | **2.33 ms (3.5×)** | **0.87 ms (9.3×)** |

- **CBD and SampleNTT cost the chip no clocks of their own:** they work on the bytes as they stream out of the
  squeeze.
- **The chip's clock counts are exact.** They come from the gate netlist itself and are the same at any clock.
- **With the RP2350 at 250 MHz** the software takes 4.90 ms for the exchange, and the chip at 50 MHz is still 5.6×
  faster.

## 2. The whole system: everything the RP2350 waits for

`bench` times the hashing of one exchange end to end: building the pin words, the PIO + DMA transfers, the chip,
unpacking, checking every clock. The chip is driven the fastest way known: framing mode 0, packets with on-chip
padding, one burst word per squeeze, SampleNTT on the RP2350.

| | Time |
|---|---|
| RP2350 software alone | 8.17 ms |
| **FPGA, measured** (chip at 18.75 MHz) | **5.95 ms (1.37×)** |
| **Real chip, guesstimate:** demo board, chip at 50 MHz | **≈ 2.83 ms (≈ 2.9×)** |
| Real chip, guesstimate: chip at 66 MHz | ≈ 2.60 ms (≈ 3.1×) |
| RP2350 at 250 MHz: software 4.90 ms; FPGA measured (19.23 MHz) | 4.50 ms (1.09×); guesstimate at 50 MHz ≈ 2.06 ms (≈ 2.4×) |

**The guesstimate** keeps the RP2350's measured work: 1.77 ms of driver and 0.12 ms of transfer set-up. It drops what
exists only on the FPGA rig: 1.55 ms of reshuffling every word onto the Pico's split GPIOs, since the demo board's 16
input pins are consecutive. And it runs the chip's 47,004 clocks at its own speed.

**Raw rates** (FPGA, 18.75 MHz): absorb 1.66 MB/s in software against 2.27 MB/s with the chip; squeeze 2.03 against
2.75 MB/s. The chip alone does 4.9 / 5.7 MB/s at this clock, and 13 / 15 MB/s at 50 MHz.

**Where the whole time goes.** At 50 MHz the chip's own share is 0.94 ms; the RP2350's driver, about 1.8 ms. The
next speed-up is in the firmware:
- build the next call's pin words while the DMA plays the current call, so the driver hides under the chip's clocks;
- read the `valid` / `busy` pins only where output can come, so framing mode 1 (SampleNTT on chip) pays off.

## 3. What changed in the firmware (all on the rig, chip at 18.75 MHz)

The chip is the same gate netlist throughout. The driver's work per chip clock dropped from about 47 to about 6
RP2350 cycles. Until the last row, the software side was portable C (39.8 ms for the exchange's hashing); the last
row compares with the assembly baseline.

| Step | RP2350 + chip | vs portable C |
|---|---|---|
| First measurement (framing mode 1, every newer command) | 25.2 ms | 1.58× |
| Idle runs written with `memset`, the quiet check four clocks at a time | 19.7 ms | 2.02× |
| Framing mode 0 with packets, on-chip padding and self-continuing bursts | 12.6 ms | 3.16× |
| Unpacking only the clocks that carry bytes, clearing only what was used, idle words fast-pathed on the rig | 7.2 ms | 5.55× |
| Checking the samples while the chip is still running | 6.36 ms | 6.27× |
| The firmware running from SRAM instead of flash (`TT_COPY_TO_RAM`, now the default) | **5.95 ms** | 6.7× (1.37× vs the assembly baseline) |

Every step kept the checks exact:
- the fast quiet / valid check was fuzzed against the exact per-clock rule: 200,000 runs with injected faults, none
  missed, no false alarms;
- the PC selftest, fuzz and golden vectors pass;
- the driver's pin stream replays clock for clock through the RTL (`make -C test rp2350`);
- `selftest`, `fuzz 200` and `ci` pass on the rig.

### Three ways to drive the chip (final firmware, against the assembly baseline)

| Driven | chip clocks | RP2350 + chip |
|---|---|---|
| **Framing mode 0, packets + on-chip padding + one burst word, SampleNTT on the RP2350** (`bench`, the default) | 47,004 | **5.95 ms (1.37×)** |
| The October 2026 way: host padding, a word per byte, a burst word per block (`bench legacy`) | 46,888 | 7.46 ms (1.10×) |
| Framing mode 1, SampleNTT on chip (`bench mode1`) | 48,152 | 15.1 ms (0.54×) |

**Framing mode 1 is slower with this firmware.** The RP2350 samples 16 bits per clock instead of 8 and splits out
the `valid` and `busy` pins, which costs more than moving SampleNTT on chip saves.

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
