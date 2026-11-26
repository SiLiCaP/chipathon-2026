# Hash chip speed test: pico2w_tang (2026-11-25T18:58:41)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 1000000 Hz (PIO div 5, low 15 + high 15 cycles)
  skew 0, calls 569, faults 0, clocks 312417, uo_out now 0x00
>
```

## Wire speed

Six known-answer jobs (bursts, packets + on-chip padding, CBD; SampleNTT and the valid / busy pins in mode 1), three rounds, at each clock and sample skew.

| Chip clock | skew 0 | skew 1 | skew 2 |
|---|---|---|---|
| 1.00 MHz | pass | – | – |
| 5.00 MHz | pass | – | – |
| 10.00 MHz | pass | – | – |
| 15.00 MHz | pass | – | – |
| 18.75 MHz | pass | – | – |
| 25.00 MHz | – | – | – |
| 30.00 MHz | – | – | – |
| 37.50 MHz | – | – | – |
| 50.00 MHz | – | – | – |
| 75.00 MHz | – | – | – |

Highest passing clock **18.75 MHz**; tried up to 75.00 MHz. Throughput measured at **18.75 MHz**, skew 0 (its skew also passes one step lower).

## Throughput

ML-KEM-512, the 32 hash calls of one key exchange, chip clock 18.75 MHz. Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, unpacking); *chip alone* is the chip's clocks at this clock.

| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |
|---|---|---|---|---|---|
| keygen | 12399 µs | 8404 µs | 1.48× | 15,896 | 848 µs |
| encaps | 13742 µs | 8388 µs | 1.64× | 16,128 | 860 µs |
| decaps | 13743 µs | 8387 µs | 1.64× | 16,128 | 860 µs |
| **exchange** | **39885 µs** | **25179 µs** | **1.58×** | 48,152 | 2568 µs |

Errors: 0 (every chip result is compared with the software one).

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 0.57 MB/s | 4.89 MB/s |
| squeeze | 1344 | 0.39 MB/s | 0.58 MB/s | 5.75 MB/s |
