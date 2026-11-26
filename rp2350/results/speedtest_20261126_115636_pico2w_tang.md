# Hash chip speed test: pico2w_tang (2026-11-26T11:56:36)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 18750000 Hz (PIO div 1, low 4 + high 4 cycles)
  skew 0, calls 103, faults 0, clocks 57727, uo_out now 0x00
>
```

## Wire speed

Six known-answer jobs (bursts, packets + on-chip padding, CBD; SampleNTT and the valid / busy pins in mode 1), three rounds, at each clock and sample skew.

| RP2350 | Chip clock | skew 0 | skew 1 | skew 2 | skew 3 |
|---|---|---|---|---|---|
| 150 MHz | 1.00 MHz | pass | – | – | – |
| 150 MHz | 5.00 MHz | pass | – | – | – |
| 150 MHz | 10.00 MHz | pass | – | – | – |
| 150 MHz | 15.00 MHz | pass | – | – | – |
| 150 MHz | 18.75 MHz | pass | – | – | – |
| 150 MHz | 25.00 MHz | – | – | – | – |
| 150 MHz | 30.00 MHz | – | – | – | – |
| 150 MHz | 37.50 MHz | – | – | – | – |
| 150 MHz | 50.00 MHz | – | – | – | – |
| 150 MHz | 75.00 MHz | – | – | – | – |

Highest passing clock **18.75 MHz**; tried up to 75.00 MHz. Throughput measured at **18.75 MHz**, skew 0 (its skew also passes one step lower).

## Throughput

ML-KEM-512, the 32 hash calls of one key exchange, chip clock 18.75 MHz. Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, unpacking); *chip alone* is the chip's clocks at this clock.

| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |
|---|---|---|---|---|---|
| keygen | 2516 µs | 1806 µs | 1.39× | 14,368 | 766 µs |
| encaps | 2830 µs | 2071 µs | 1.37× | 16,318 | 870 µs |
| decaps | 2828 µs | 2070 µs | 1.37× | 16,318 | 870 µs |
| **exchange** | **8174 µs** | **5948 µs** | **1.37×** | 47,004 | 2507 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 5948 µs with the chip go: 2507 µs of chip clocks, 1550 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 116 µs of other transport work (DMA setup, splitting the samples), 1775 µs in the driver (building and checking the pin words).

### Three ways to drive the same chip

Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and `busy` pins, so the RP2350 samples 16 bits per clock instead of 8.

| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |
|---|---|---|---|---|---|
| newer commands, framing mode 0, SampleNTT on the RP2350 (the default) | 47,004 | 1775 µs | 4173 µs | 5948 µs | 1.37× |
| every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 7856 µs | 7279 µs | 15135 µs | 0.54× |
| the October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 2955 µs | 4498 µs | 7453 µs | 1.10× |

### Compute only: the chip against the software doing the same work

Software: each piece of software alone on the RP2350. Chip: its clocks for the same work, with CBD and SampleNTT on the chip (they run on the output stream and cost no clocks of their own). No transfers on either side.

| Work | RP2350 software | chip clocks | chip at this clock | chip at 50 MHz | speed-up at 50 MHz |
|---|---|---|---|---|---|
| Keccak-f[1600], one permutation | 73.0 µs | 368 | 19.6 µs | 7.4 µs | 10× |
| SamplePolyCBD eta = 3 (192 B) | 36.0 µs | 0 | – | – | in the output stream |
| SampleNTT, one polynomial | 43.0 µs | 0 | – | – | in the output stream |
| G = SHA3-512 (64 B) | 81.8 µs | 518 | 27.6 µs | 10.4 µs | 8× |
| H = SHA3-256 (800 B) | 480.5 µs | 3,091 | 164.9 µs | 61.8 µs | 8× |
| J = SHAKE256 (800 B) | 480.5 µs | 3,091 | 164.9 µs | 61.8 µs | 8× |
| PRF_3 + CBD, one polynomial | 195.8 µs | 1,004 | 53.5 µs | 20.1 µs | 10× |
| PRF_2 + CBD, one polynomial | 116.5 µs | 569 | 30.3 µs | 11.4 µs | 10× |
| XOF + SampleNTT, one polynomial | 290.3 µs | 1,700 | 90.7 µs | 34.0 µs | 9× |
| **one ML-KEM-512 exchange (32 calls)** | 8170.1 µs | 43,736 | 2332.6 µs | 874.7 µs | 9× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 1.66 MB/s | 2.27 MB/s | 4.89 MB/s |
| squeeze | 1344 | 2.03 MB/s | 2.75 MB/s | 5.75 MB/s |
