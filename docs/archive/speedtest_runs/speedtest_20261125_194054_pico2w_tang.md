# Hash chip speed test: pico2w_tang (2026-11-25T19:40:54)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 18750000 Hz (PIO div 1, low 4 + high 4 cycles)
  skew 0, calls 1688, faults 23, clocks 965069, uo_out now 0x00
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
| keygen | 7459 µs | 4281 µs | 1.74× | 15,896 | 827 µs |
| encaps | 8250 µs | 4264 µs | 1.93× | 16,128 | 839 µs |
| decaps | 8249 µs | 4260 µs | 1.94× | 16,128 | 839 µs |
| **exchange** | **23958 µs** | **12806 µs** | **1.87×** | 48,152 | 2504 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 12806 µs with the chip go: 2504 µs of chip clocks, 1743 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 2795 µs of other transport work (DMA setup, splitting the samples), 5763 µs in the driver (building and checking the pin words).

### The chip's newer commands vs the October 2026 way

Same chip, same workload, same clock. *October way*: host padding, a word per byte, a burst word per block, SampleNTT on the RP2350. *Now*: packets with on-chip padding, one burst word per squeeze, SampleNTT on the chip.

| | chip clocks | RP2350 + chip | vs software |
|---|---|---|---|
| October way | 46,888 | 9554 µs | 2.51× |
| now | 48,152 | 12806 µs | 1.87× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.50 MB/s | 1.19 MB/s | 5.02 MB/s |
| squeeze | 1344 | 0.64 MB/s | 1.16 MB/s | 5.89 MB/s |
