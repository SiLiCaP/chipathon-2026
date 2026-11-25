# Hash chip speed test: pico2w_tang (2026-11-25T19:01:39)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 18750000 Hz (PIO div 1, low 4 + high 4 cycles)
  skew 0, calls 108, faults 0, clocks 68245, uo_out now 0x00
>
```

## Throughput

ML-KEM-512, the 32 hash calls of one key exchange, chip clock 10.00 MHz. Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, unpacking); *chip alone* is the chip's clocks at this clock.

| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |
|---|---|---|---|---|---|
| keygen | 12420 µs | 9197 µs | 1.35× | 15,896 | 1590 µs |
| encaps | 13750 µs | 9152 µs | 1.50× | 16,128 | 1613 µs |
| decaps | 13745 µs | 9154 µs | 1.50× | 16,128 | 1613 µs |
| **exchange** | **39915 µs** | **27503 µs** | **1.45×** | 48,152 | 4815 µs |

Errors: 0 (every chip result is compared with the software one).

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 0.52 MB/s | 2.61 MB/s |
| squeeze | 1344 | 0.39 MB/s | 0.54 MB/s | 3.06 MB/s |
