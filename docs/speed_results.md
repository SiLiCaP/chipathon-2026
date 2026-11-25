# Speed results: the hash chip against the RP2350 (Nov 25, 2026)

Measured with the firmware's `speedtest` and `bench` (`rp2350/core/hash_bench.c`) through `rp2350/speedtest.py`.
Every chip result in every run was compared with the software result: **0 errors**. Raw data:
[rp2350/results/](../rp2350/results/README.md). Method: [getting_started.md](getting_started.md#the-speed-test).

**Setup:** the chip's **hardened gate netlist** from `main` (a local LibreLane build of `main` at `60b31b8`: 74.1 %, 0
DRC / LVS / antenna, +6.35 ns setup at 50 MHz in the slow corner) runs on a Tang Nano 20K. The RP2350 firmware runs on a
Pico 2 W wired pin for pin to it: the same firmware and the same RP2350 as on the Tiny Tapeout demo board. On this rig
the gate netlist also passed `selftest`, `fuzz 200` and the `ci` check (the ML-KEM-512 hash sequence, 32 jobs).

**Workload:** the 32 hash calls of one ML-KEM-512 key exchange: G, H, J, 12 × XOF with SampleNTT, 16 × PRF with
SamplePolyCBD. **Software** means the same work in portable C on the same RP2350 (SHA-3, CBD, SampleNTT).

## 1. Measured on the FPGA rig

RP2350 at 150 MHz, chip at 18.75 MHz (the rig's wire limit):

| ML-KEM-512, hashing of one exchange | Time | |
|---|---|---|
| RP2350 alone, C | 39.9 ms | |
| **The chip's calculation time** (47,004 chip clocks at 18.75 MHz) | **2.51 ms** | **16× less than software** |
| RP2350 + chip, end to end, best driving mode | **12.6 ms** | **3.16× faster** |
| … of which the RP2350 building and checking the pin words | 7.18 ms | |
| … of which reshuffling words onto the Pico's split pins (rig only) | 2.89 ms | |
| … of which other transport work | 0.05 ms | |

With the RP2350 at 250 MHz (chip at 19.23 MHz): software 24.0 ms, with the chip 8.5 ms, 2.82×.

**Raw rates** (same run): absorb 0.30 MB/s in software against 1.24 MB/s with the chip, squeeze 0.38 against
1.07 MB/s. The chip alone could do 4.9 / 5.7 MB/s at 18.75 MHz, and 13 / 15 MB/s at 50 MHz.

### Three ways to drive the chip

The chip's November commands (packets, on-chip padding, bursts that continue by themselves, SampleNTT on chip, framing
mode 1 with `valid` and `busy` pins) give the firmware choices. Same chip, workload and clock:

| Driven | chip clocks | RP2350 work | RP2350 + chip | vs software |
|---|---|---|---|---|
| **Newer commands in framing mode 0; SampleNTT on the RP2350** | 47,004 | 10.1 ms | **12.6 ms** | **3.16×** |
| The October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 11.8 ms | 14.3 ms | 2.78× |
| Every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 17.1 ms | 19.7 ms | 2.03× |

- **Packets and on-chip padding** cut the RP2350's work by 1.7 ms against the October way.
- **Framing mode 1** costs more than it saves with today's firmware. The RP2350 samples 16 bits per clock instead of
  8 and splits the `valid` and `busy` pins out of every sample, which costs about 4.5 ms. Moving SampleNTT on chip
  saves less than that.
- **Use mode 0 for speed today.** Mode 1 stays useful for SampleNTT, once the sampling is cheaper (section 3).

## 2. Guesstimate for the real chip

Scaled from the measurements above, with the best driving mode:
- the RP2350's own work stays as measured;
- the rig-only reshuffling drops out, because the demo board's 16 input pins are consecutive GPIOs;
- the chip runs at its own clock.

| Real chip on the demo board | Hashing of one ML-KEM-512 exchange | vs C on the RP2350 |
|---|---|---|
| RP2350 150 MHz, chip 50 MHz | **≈ 8.2 ms** (7.2 RP2350 + 0.94 chip) | **≈ 4.9×** |
| RP2350 150 MHz, chip 66 MHz | ≈ 7.9 ms | ≈ 5.0× |
| RP2350 250 MHz, chip 50 MHz | ≈ 5.3 ms (4.3 + 0.94), against 24.0 ms | ≈ 4.5× |
| The chip's calculation alone at 50 MHz | **0.94 ms** | 42× |

How fast the real pins run is unknown until the chips exist. The chip's timing closes with +6.35 ns of slack at 50 MHz
in the slow corner (the logic would do about 73 MHz), and the speed test measures the rest (section 4).

**Against hand-optimized software.** pqm4-class assembly SHA-3 does this hashing in about 5.8 ms on a 150 MHz RP2350
(cited, [tile_routes.md](tile_routes.md)). The firmware as written (≈ 8.2 ms) doesn't beat that yet. The chip's own
0.94 ms does, by 6×.

## 3. Where the next speed-up is

The RP2350, not the chip, sets the result: about 23 RP2350 cycles of work per chip clock, while a chip clock at 50 MHz
lasts 3 RP2350 cycles.
The driver builds and checks a 16-bit pin word for every chip clock, and about three quarters of those clocks are the
368-clock waits of each permutation. The firmware can change without touching the silicon:

1. **Idle runs in the PIO.** One count instead of 368 zero words, and checks only on the clocks that carry output.
   That cuts the RP2350's work about 4× (estimate), to roughly 2–3 ms per exchange at 50 MHz.
2. **Cheaper mode 1 sampling.** Read `valid` and `busy` only where output can come, so SampleNTT on chip pays off.
3. **Overlap.** Build the next call's words while the DMA plays the current one.

The route model's 3.18 ms for a whole exchange (from October) assumed a feed of the kind in step 1.

## 4. Wire speed

| Board | Passes up to | Fails from | Notes |
|---|---|---|---|
| Pico rig, RP2350 at 150 MHz | 18.75 MHz | 25 MHz | skew 0 only; no skew helps above, so the inputs set the limit |
| Pico rig, RP2350 at 250 MHz | 19.23 MHz | 25 MHz | the sweep tried up to 125 MHz and skews 0-3 |

The rig's limit is its jumper wires. **The real chip on its PCB will be faster**, and the speed test is built for it:
- it tries every clock the RP2350 can make, up to half its system clock;
- if even the fastest passes, `speedtest.py` raises the RP2350 to 200 and then 250 MHz (core at 1.20 V) and sweeps
  again, up to 125 MHz;
- at every clock it searches sample skews 0-3, because the pad, mux and clock-insertion delays (up to about 10 ns
  on Tiny Tapeout) push output bytes a clock or more later at high clocks.

## History

- Earlier on the same day, the chip's RTL (not its gate netlist) gave the same chip clock counts.
- Before this session's driver change (idle runs by `memset`, checks four clocks at a time), mode 1 took 25.2 ms.
- All runs are in [rp2350/results/](../rp2350/results/README.md).
