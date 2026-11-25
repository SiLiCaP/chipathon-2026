# Speed results: the hash chip against the RP2350 (Nov 25, 2026)

Measured with the firmware's `speedtest` / `bench` (`rp2350/core/hash_bench.c`) through `rp2350/speedtest.py`.
Every chip result in every run was compared with the software result: 0 errors. Raw data:
[rp2350/results/](../rp2350/results/README.md). Method: [getting_started.md](getting_started.md#the-speed-test).

**Board:** before silicon, the Pico 2 W + Tang Nano 20K rig (the chip's RTL from `main` in the FPGA, the RP2350
firmware on the Pico, 26 jumper wires). The same commands run unchanged on the Tiny Tapeout demo board.

## 1. The chip is fast; the RP2350's feed decides the result

ML-KEM-512, the 32 hash calls of one key exchange (G, H, J, 12 × XOF through SampleNTT, 16 × PRF through CBD),
RP2350 at 150 MHz, chip at 18.75 MHz:

| | Time | |
|---|---|---|
| RP2350 alone, portable C (SHA-3 + CBD + SampleNTT) | 39.9 ms | measured |
| RP2350 + chip, firmware as written | **19.7 ms** | measured: **2.02× faster** |
| of which the chip's clocks (48,152) | 2.57 ms | measured clock count |
| of which the transport runs' overhead (word reshuffling for the Pico's split pins) | 7.5 ms | measured; rig only |
| of which the driver (building pin words, checking every clock, unpacking) | 9.6 ms | measured |
| The chip's clocks at 50 MHz | 0.96 ms | from the clock count |

- **Absorb / squeeze:** 0.78 / 0.75 MB/s through the chip against 0.30 / 0.39 MB/s in software. The chip alone
  could do 4.9 / 5.7 MB/s at this clock, and 13 / 15 MB/s at 50 MHz.
- **The RP2350 at 250 MHz:** software 23.9 ms against 12.8 ms with the chip, 1.88×.
- **The clock barely matters on the rig.** At 5, 10 and 18.75 MHz the chip path took 32.3, 27.5 and 25.2 ms
  (before the driver change below). Only the 2.6–9.6 ms of chip clocks changes; the RP2350's own work stays.
- **This session's driver change** cut the driver from 15.2 to 9.6 ms (`core/hash_chip.c`: idle runs written
  with `memset`, the quiet / valid check four clocks at a time, empty stretches skipped when unpacking). The
  checks are unchanged: the fast check was fuzzed against the exact per-clock rule (2 million cases, no fault
  missed), the PC selftest, fuzz and golden vectors pass, and the self-test passes on the rig.

## 2. Wire speed

| Board | Passes up to | Fails from | Notes |
|---|---|---|---|
| Pico rig, RP2350 at 150 MHz | 18.75 MHz | 25 MHz | skew 0 only; no skew helps above, so the inputs set the limit |
| Pico rig, RP2350 at 250 MHz | 19.23 MHz | 25 MHz | the sweep tried up to 125 MHz and skews 0-3 |

The rig's limit is its jumper wires. **The real chip on its PCB will be faster**: short traces, and the chip's
own timing closes with +6.35 ns of slack at 50 MHz in the slow corner (about 73 MHz for the logic). The speed
test is built for that:
- it tries every clock the RP2350 can make, up to half its system clock;
- if even the fastest passes, `speedtest.py` raises the RP2350 to 200 and then 250 MHz (core at 1.20 V) and sweeps
  again, up to 125 MHz chip clock;
- it searches sample skews 0-3 at every clock, because at high clocks the pad, mux and clock-insertion delays
  (up to about 10 ns on Tiny Tapeout) push the output bytes a clock or two later.

## 3. What to expect on the demo board

Estimates for the real chip, scaled from the measurements above:

| | Hashing per ML-KEM-512 exchange | vs portable C on the RP2350 |
|---|---|---|
| Firmware as written, chip at 50 MHz, RP2350 at 150 MHz | about 12–13 ms (9.6 driver + about 2 word handling + 0.96 chip) | about 3× |
| Same, RP2350 at 250 MHz | about 8 ms | about 3× (software also speeds up) |
| The chip alone at 50 MHz | 0.96 ms | the ceiling |

The demo board needs no word reshuffling: its 16 input pins are 16 consecutive GPIOs.

**Against optimized software.** The cited pqm4-class assembly SHA-3 does the same hashing in about 5.8 ms on an
RP2350 at 150 MHz (`docs/tile_routes.md`). The firmware as written doesn't beat that yet. The chip's own 0.96 ms
does, by 6×.

The gap is the driver's per-clock work: it builds and checks a word for every chip clock, and about three
quarters of the clocks are the idle 368-clock waits of each permutation. The next firmware step, with no silicon
change, is to let the PIO play the idle runs itself (a count instead of 368 words), checking only the clocks that
carry output. That would cut the driver's work about 4×, to an estimated 3–4 ms per exchange at 50 MHz. The route
model's 3.18 ms per full exchange assumed a feed of that kind.
