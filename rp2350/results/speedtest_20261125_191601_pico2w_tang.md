# Hash chip speed test: pico2w_tang (2026-11-25T19:16:01)

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
| keygen | 7454 µs | 4253 µs | 1.75× | 15,896 | 827 µs |
| encaps | 8235 µs | 4255 µs | 1.94× | 16,128 | 839 µs |
| decaps | 8232 µs | 4251 µs | 1.94× | 16,128 | 839 µs |
| **exchange** | **23922 µs** | **12759 µs** | **1.88×** | 48,152 | 2504 µs |

Errors: 0 (every chip result is compared with the software one).

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.51 MB/s | 1.18 MB/s | 5.02 MB/s |
| squeeze | 1344 | 0.64 MB/s | 1.14 MB/s | 5.89 MB/s |
