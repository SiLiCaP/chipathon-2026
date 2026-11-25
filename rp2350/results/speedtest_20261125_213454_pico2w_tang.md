# Hash chip speed test: pico2w_tang (2026-11-25T21:34:54)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 18750000 Hz (PIO div 1, low 4 + high 4 cycles)
  skew 0, calls 2053, faults 23, clocks 1210254, uo_out now 0x00
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
| keygen | 1508 µs | 1377 µs | 1.10× | 14,368 | 747 µs |
| encaps | 1698 µs | 1563 µs | 1.09× | 16,318 | 849 µs |
| decaps | 1697 µs | 1562 µs | 1.09× | 16,318 | 849 µs |
| **exchange** | **4902 µs** | **4501 µs** | **1.09×** | 47,004 | 2444 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 4501 µs with the chip go: 2444 µs of chip clocks, 933 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 84 µs of other transport work (DMA setup, splitting the samples), 1040 µs in the driver (building and checking the pin words).

### Three ways to drive the same chip

Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and `busy` pins, so the RP2350 samples 16 bits per clock instead of 8.

| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |
|---|---|---|---|---|---|
| newer commands, framing mode 0, SampleNTT on the RP2350 (the default) | 47,004 | 1040 µs | 3461 µs | 4501 µs | 1.09× |
| every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 4716 µs | 5326 µs | 10041 µs | 0.49× |
| the October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 1802 µs | 3536 µs | 5338 µs | 0.92× |

### Compute only: the chip against the C code doing the same work

Software: each piece of C code alone on the RP2350. Chip: its clocks for the same work, with CBD and SampleNTT on the chip (they run on the output stream and cost no clocks of their own). No transfers on either side.

| Work | C on the RP2350 | chip clocks | chip at this clock | chip at 50 MHz | speed-up at 50 MHz |
|---|---|---|---|---|---|
| Keccak-f[1600], one permutation | 43.8 µs | 368 | 19.1 µs | 7.4 µs | 6× |
| SamplePolyCBD eta = 3 (192 B) | 21.6 µs | 0 | – | – | in the output stream |
| SampleNTT, one polynomial | 25.8 µs | 0 | – | – | in the output stream |
| G = SHA3-512 (64 B) | 49.1 µs | 518 | 26.9 µs | 10.4 µs | 5× |
| H = SHA3-256 (800 B) | 288.5 µs | 3,091 | 160.7 µs | 61.8 µs | 5× |
| J = SHAKE256 (800 B) | 288.4 µs | 3,091 | 160.7 µs | 61.8 µs | 5× |
| PRF_3 + CBD, one polynomial | 117.5 µs | 1,004 | 52.2 µs | 20.1 µs | 6× |
| PRF_2 + CBD, one polynomial | 69.9 µs | 569 | 29.6 µs | 11.4 µs | 6× |
| XOF + SampleNTT, one polynomial | 174.3 µs | 1,700 | 88.4 µs | 34.0 µs | 5× |
| **one ML-KEM-512 exchange (32 calls)** | 4904.3 µs | 43,736 | 2274.3 µs | 874.7 µs | 6× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 2.77 MB/s | 2.93 MB/s | 5.02 MB/s |
| squeeze | 1344 | 3.41 MB/s | 3.57 MB/s | 5.89 MB/s |
