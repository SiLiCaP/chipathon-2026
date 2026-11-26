# Hash chip speed test: pico2w_tang (2026-11-25T19:40:49)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 1000000 Hz (PIO div 5, low 15 + high 15 cycles)
  skew 0, calls 1168, faults 0, clocks 652263, uo_out now 0x00
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
| keygen | 12435 µs | 6597 µs | 1.88× | 15,896 | 848 µs |
| encaps | 13751 µs | 6570 µs | 2.09× | 16,128 | 860 µs |
| decaps | 13748 µs | 6563 µs | 2.09× | 16,128 | 860 µs |
| **exchange** | **39934 µs** | **19730 µs** | **2.02×** | 48,152 | 2568 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 19730 µs with the chip go: 2568 µs of chip clocks, 2905 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 4667 µs of other transport work (DMA setup, splitting the samples), 9590 µs in the driver (building and checking the pin words).

### The chip's newer commands vs the October 2026 way

Same chip, same workload, same clock. *October way*: host padding, a word per byte, a burst word per block, SampleNTT on the RP2350. *Now*: packets with on-chip padding, one burst word per squeeze, SampleNTT on the chip.

| | chip clocks | RP2350 + chip | vs software |
|---|---|---|---|
| October way | 46,888 | 14365 µs | 2.78× |
| now | 48,152 | 19730 µs | 2.02× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 0.78 MB/s | 4.89 MB/s |
| squeeze | 1344 | 0.39 MB/s | 0.75 MB/s | 5.75 MB/s |
