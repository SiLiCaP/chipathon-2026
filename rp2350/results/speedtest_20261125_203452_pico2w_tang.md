# Hash chip speed test: pico2w_tang (2026-11-25T20:34:52)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 18750000 Hz (PIO div 1, low 4 + high 4 cycles)
  skew 0, calls 1912, faults 23, clocks 1098045, uo_out now 0x00
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
| keygen | 7403 µs | 1368 µs | 5.41× | 14,368 | 747 µs |
| encaps | 8233 µs | 1565 µs | 5.26× | 16,318 | 849 µs |
| decaps | 8232 µs | 1565 µs | 5.26× | 16,318 | 849 µs |
| **exchange** | **23867 µs** | **4499 µs** | **5.30×** | 47,004 | 2444 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 4499 µs with the chip go: 2444 µs of chip clocks, 931 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 65 µs of other transport work (DMA setup, splitting the samples), 1059 µs in the driver (building and checking the pin words).

### Three ways to drive the same chip

Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and `busy` pins, so the RP2350 samples 16 bits per clock instead of 8.

| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |
|---|---|---|---|---|---|
| newer commands, framing mode 0, SampleNTT on the RP2350 (the default) | 47,004 | 1059 µs | 3440 µs | 4499 µs | 5.30× |
| every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 4733 µs | 5324 µs | 10056 µs | 2.37× |
| the October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 1802 µs | 3545 µs | 5347 µs | 4.46× |

### Compute only: the chip against the C code doing the same work

Software: each piece of C code alone on the RP2350. Chip: its clocks for the same work, with CBD and SampleNTT on the chip (they run on the output stream and cost no clocks of their own). No transfers on either side.

| Work | C on the RP2350 | chip clocks | chip at this clock | chip at 50 MHz | speed-up at 50 MHz |
|---|---|---|---|---|---|
| Keccak-f[1600], one permutation | 246.4 µs | 368 | 19.1 µs | 7.4 µs | 33× |
| SamplePolyCBD eta = 3 (192 B) | 71.2 µs | 0 | – | – | in the output stream |
| SampleNTT, one polynomial | 25.8 µs | 0 | – | – | in the output stream |
| G = SHA3-512 (64 B) | 259.3 µs | 518 | 26.9 µs | 10.4 µs | 25× |
| H = SHA3-256 (800 B) | 1568.4 µs | 3,091 | 160.7 µs | 61.8 µs | 25× |
| J = SHAKE256 (800 B) | 1568.4 µs | 3,091 | 160.7 µs | 61.8 µs | 25× |
| PRF_3 + CBD, one polynomial | 583.1 µs | 1,004 | 52.2 µs | 20.1 µs | 29× |
| PRF_2 + CBD, one polynomial | 315.4 µs | 569 | 29.6 µs | 11.4 µs | 28× |
| XOF + SampleNTT, one polynomial | 807.5 µs | 1,700 | 88.4 µs | 34.0 µs | 24× |
| **one ML-KEM-512 exchange (32 calls)** | 23862.0 µs | 43,736 | 2274.3 µs | 874.7 µs | 27× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.51 MB/s | 2.92 MB/s | 5.02 MB/s |
| squeeze | 1344 | 0.65 MB/s | 3.60 MB/s | 5.89 MB/s |
