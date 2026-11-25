# Hash chip speed test: pico2w_tang (2026-11-25T19:16:13)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 250 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 19230769 Hz (PIO div 1, low 6 + high 7 cycles)
  skew 0, calls 994, faults 32, clocks 577629, uo_out now 0x00
>
```

## Throughput

ML-KEM-512, the 32 hash calls of one key exchange, chip clock 18.75 MHz. Times from the RP2350's cycle counter. *With the chip* is the firmware as written (pin words, PIO + DMA, unpacking); *chip alone* is the chip's clocks at this clock.

| | RP2350 software | RP2350 + chip | speed-up | chip clocks | chip alone |
|---|---|---|---|---|---|
| keygen | 12434 µs | 6593 µs | 1.89× | 15,896 | 848 µs |
| encaps | 13725 µs | 6554 µs | 2.09× | 16,128 | 860 µs |
| decaps | 13724 µs | 6550 µs | 2.10× | 16,128 | 860 µs |
| **exchange** | **39883 µs** | **19697 µs** | **2.02×** | 48,152 | 2568 µs |

Errors: 0 (every chip result is compared with the software one).

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 0.78 MB/s | 4.89 MB/s |
| squeeze | 1344 | 0.38 MB/s | 0.74 MB/s | 5.75 MB/s |
