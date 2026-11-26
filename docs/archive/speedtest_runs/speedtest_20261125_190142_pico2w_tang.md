# Hash chip speed test: pico2w_tang (2026-11-25T19:01:42)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 10000000 Hz (PIO div 1, low 7 + high 8 cycles)
  skew 0, calls 216, faults 0, clocks 136490, uo_out now 0x00
>
```

## Throughput

ML-KEM-512, the 32 hash calls of one key exchange, chip clock 5.00 MHz. Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, unpacking); *chip alone* is the chip's clocks at this clock.

| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |
|---|---|---|---|---|---|
| keygen | 12423 µs | 10779 µs | 1.15× | 15,896 | 3179 µs |
| encaps | 13752 µs | 10760 µs | 1.28× | 16,128 | 3226 µs |
| decaps | 13749 µs | 10767 µs | 1.28× | 16,128 | 3226 µs |
| **exchange** | **39924 µs** | **32306 µs** | **1.24×** | 48,152 | 9630 µs |

Errors: 0 (every chip result is compared with the software one).

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 0.43 MB/s | 1.30 MB/s |
| squeeze | 1344 | 0.39 MB/s | 0.46 MB/s | 1.53 MB/s |
