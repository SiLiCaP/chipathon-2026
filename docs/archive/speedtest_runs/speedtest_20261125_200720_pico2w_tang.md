# Hash chip speed test: pico2w_tang (2026-11-25T20:07:20)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 18750000 Hz (PIO div 1, low 4 + high 4 cycles)
  skew 0, calls 1776, faults 23, clocks 1036685, uo_out now 0x00
>
```

## Wire speed

Six known-answer jobs (bursts, packets + on-chip padding, CBD; SampleNTT and the valid / busy pins in mode 1), three rounds, at each clock and sample skew.

| RP2350 | Chip clock | skew 0 | skew 1 | skew 2 | skew 3 |
|---|---|---|---|---|---|
| 250 MHz | 1.01 MHz | pass | – | – | – |
| 250 MHz | 5.00 MHz | pass | – | – | – |
| 250 MHz | 10.00 MHz | pass | – | – | – |
| 250 MHz | 14.71 MHz | pass | – | – | – |
| 250 MHz | 19.23 MHz | pass | – | – | – |
| 250 MHz | 25.00 MHz | – | – | – | – |
| 250 MHz | 31.25 MHz | – | – | – | – |
| 250 MHz | 35.71 MHz | – | – | – | – |
| 250 MHz | 41.67 MHz | – | – | – | – |
| 250 MHz | 50.00 MHz | – | – | – | – |
| 250 MHz | 62.50 MHz | – | – | – | – |
| 250 MHz | 83.33 MHz | – | – | – | – |
| 250 MHz | 125.00 MHz | – | – | – | – |

Highest passing clock **19.23 MHz**; tried up to 125.00 MHz. Throughput measured at **19.23 MHz**, skew 0 (its skew also passes one step lower).

## Throughput

ML-KEM-512, the 32 hash calls of one key exchange, chip clock 19.23 MHz. Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, unpacking); *chip alone* is the chip's clocks at this clock.

| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |
|---|---|---|---|---|---|
| keygen | 7455 µs | 1513 µs | 4.93× | 14,368 | 747 µs |
| encaps | 8243 µs | 1599 µs | 5.15× | 16,318 | 849 µs |
| decaps | 8240 µs | 1596 µs | 5.16× | 16,318 | 849 µs |
| **exchange** | **23938 µs** | **4708 µs** | **5.08×** | 47,004 | 2444 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 4708 µs with the chip go: 2444 µs of chip clocks, 927 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 86 µs of other transport work (DMA setup, splitting the samples), 1251 µs in the driver (building and checking the pin words).

### Three ways to drive the same chip

Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and `busy` pins, so the RP2350 samples 16 bits per clock instead of 8.

| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |
|---|---|---|---|---|---|
| newer commands, framing mode 0, SampleNTT on the RP2350 (the default) | 47,004 | 1251 µs | 3457 µs | 4708 µs | 5.08× |
| every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 4839 µs | 5312 µs | 10151 µs | 2.36× |
| the October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 1980 µs | 3611 µs | 5592 µs | 4.28× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.51 MB/s | 2.80 MB/s | 5.02 MB/s |
| squeeze | 1344 | 0.64 MB/s | 3.15 MB/s | 5.89 MB/s |
