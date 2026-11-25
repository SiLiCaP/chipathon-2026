# Speed results: the hash chip against the RP2350 (Nov 25, 2026)

Measured with the firmware's `speedtest` and `bench` (`rp2350/core/hash_bench.c`) through `rp2350/speedtest.py`.
Every chip result in every run was compared with the software result: **0 errors**. Raw data:
[rp2350/results/](../rp2350/results/README.md). Method: [getting_started.md](getting_started.md#the-speed-test).

**Setup:**
- **The chip:** the **hardened gate netlist** of `main` (a local LibreLane build of `60b31b8`: 74.1 %, 0 DRC / LVS /
  antenna, +6.35 ns setup at 50 MHz in the slow corner), running on a Tang Nano 20K.
- **The host:** the RP2350 firmware on a Pico 2 W wired pin for pin to it, the same firmware and the same RP2350 as
  on the Tiny Tapeout demo board.
- **Checks on this rig:** the gate netlist passes `selftest`, `fuzz 200` and the `ci` check (the ML-KEM-512 hash
  sequence, 32 jobs, 0 failures).

**Workload:** the 32 hash calls of one ML-KEM-512 key exchange: G, H, J, 12 × XOF with SampleNTT, 16 × PRF with
SamplePolyCBD. **Software** is the same work in portable C on the same RP2350 (SHA-3, CBD, SampleNTT).

## 1. Results

Two ways to count:
- **Whole time** is everything the RP2350 waits for: building the pin words, the transfers, the chip, unpacking.
- **Accelerated part only** is the hashing computation itself: in software, the C code; on the chip, its clocks.

| ML-KEM-512, hashing of one exchange | Whole time | | Accelerated part only | |
|---|---|---|---|---|
| RP2350 alone, C (measured) | 39.9 ms | | 39.9 ms | |
| **FPGA, measured** (gate netlist, chip at 18.75 MHz) | **6.36 ms** | **6.3×** | **2.51 ms** | **16×** |
| **Real chip, guesstimate:** demo board, chip at 50 MHz | **≈ 3.2 ms** | **≈ 12×** | **≈ 0.94 ms** | **≈ 42×** |
| Real chip, guesstimate: chip at 66 MHz | ≈ 3.0 ms | ≈ 13× | ≈ 0.71 ms | ≈ 57× |

All rows have the RP2350 at 150 MHz. With the RP2350 at 250 MHz (measured, chip at 19.23 MHz): software 23.9 ms,
with the chip 4.7 ms, 5.1×; the guesstimate at 50 MHz is ≈ 2.3 ms, ≈ 10.5×.

**How the guesstimate is made.** It keeps the RP2350's measured work (2.15 ms of driver, 0.16 ms of transfer
set-up). It drops what exists only on the FPGA rig: reshuffling every word onto the Pico's split GPIOs (1.54 ms),
since the demo board's 16 input pins are consecutive. And it runs the chip's 47,004 clocks at its own speed. How fast
the real pins run is unknown until the chips exist. The chip's timing closes with +6.35 ns of slack at 50 MHz in the
slow corner, so the logic would do about 73 MHz, and the speed test measures the pins (section 4).

**Against hand-optimized software.** pqm4-class assembly SHA-3 does this hashing in about 5.8 ms on a 150 MHz RP2350
(cited, [tile_routes.md](tile_routes.md)). The real chip's ≈ 3.2 ms whole time beats that by about 1.8×, and its
0.94 ms of computation by about 6×.

**Raw rates** (FPGA, 18.75 MHz): absorb 0.30 MB/s in software against 2.16 MB/s with the chip; squeeze 0.38 against
2.33 MB/s. The chip alone does 4.9 / 5.7 MB/s at this clock, and 13 / 15 MB/s at 50 MHz.

## 2. What changed in the firmware

The chip is the same gate netlist throughout. Only the RP2350 firmware changed, so the same silicon went from 1.58× to
6.27× on the same rig in one day. The driver's work per chip clock dropped from about 47 to about 7 RP2350 cycles:

| Step (all on the rig, RP2350 at 150 MHz, chip at 18.75 MHz) | Whole time | vs C |
|---|---|---|
| First measurement (framing mode 1, every newer command) | 25.2 ms | 1.58× |
| Idle runs written with `memset`, the quiet check four clocks at a time | 19.7 ms | 2.02× |
| Framing mode 0 with packets, on-chip padding and self-continuing bursts | 12.6 ms | 3.16× |
| Unpacking only the clocks that carry bytes, clearing only what was used, idle words fast-pathed on the rig | 7.2 ms | 5.55× |
| Checking the samples while the chip is still running (overlap) | **6.36 ms** | **6.27×** |

Every step kept the checks exact:
- the fast quiet / valid check was fuzzed against the exact per-clock rule: 200,000 runs with injected faults, none
  missed, no false alarms;
- the PC selftest, fuzz and golden vectors pass;
- the driver's pin stream replays clock for clock through the RTL (`make -C test rp2350`);
- `selftest`, `fuzz 200` and `ci` pass on the rig.

### Three ways to drive the chip

Same chip, workload and clock (final firmware):

| Driven | chip clocks | Whole time | vs C |
|---|---|---|---|
| **Framing mode 0, packets + on-chip padding + one burst word, SampleNTT on the RP2350** (`bench`, the default) | 47,004 | **6.36 ms** | **6.27×** |
| The October 2026 way: host padding, a word per byte, a burst word per block (`bench legacy`) | 46,888 | 7.79 ms | 5.12× |
| Framing mode 1, SampleNTT on chip (`bench mode1`) | 48,152 | 15.3 ms | 2.61× |

**Framing mode 1 is slower with this firmware.** The RP2350 samples 16 bits per clock instead of 8 and splits out the
`valid` and `busy` pins, which costs more than moving SampleNTT on chip saves. It would pay off if the firmware read
those pins only where output can come. That's the next firmware step, but mode 0 already gives the numbers above.

## 3. Where the time goes now (FPGA, final firmware)

| Part | Time |
|---|---|
| The chip's clocks | 2.51 ms |
| Reshuffling words onto the Pico's split GPIOs (rig only) | 1.54 ms |
| Transfer set-up (DMA, per call) | 0.16 ms |
| The driver: building the pin words (about 4 cycles per clock), software SampleNTT (0.64 ms), what is left of the check and the unpacking | 2.15 ms |

What's left for later, all firmware: build the next call's words while the DMA plays the current one, so the driver
hides under the chip's clocks; and the cheap mode 1 sampling above.

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
