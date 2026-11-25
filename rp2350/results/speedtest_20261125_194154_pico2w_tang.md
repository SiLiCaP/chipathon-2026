# Hash chip speed test: pico2w_tang (2026-11-25T19:41:54)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 1000000 Hz (PIO div 5, low 15 + high 15 cycles)
  skew 0, calls 0, faults 0, clocks 0, uo_out now 0x00
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
| keygen | 12441 µs | 6601 µs | 1.88× | 15,896 | 848 µs |
| encaps | 13760 µs | 6556 µs | 2.10× | 16,128 | 860 µs |
| decaps | 13754 µs | 6549 µs | 2.10× | 16,128 | 860 µs |
| **exchange** | **39954 µs** | **19705 µs** | **2.03×** | 48,152 | 2568 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 19705 µs with the chip go: 2568 µs of chip clocks, 2917 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 4611 µs of other transport work (DMA setup, splitting the samples), 9609 µs in the driver (building and checking the pin words).

### Three ways to drive the same chip

Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and `busy` pins, so the RP2350 samples 16 bits per clock instead of 8.

| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |
|---|---|---|---|---|---|
| now: every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 9609 µs | 10096 µs | 19705 µs | 2.03× |
| newer commands in framing mode 0, SampleNTT on the RP2350 | 47,004 | 7176 µs | 5446 µs | 12622 µs | 3.17× |
| the October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 8900 µs | 5442 µs | 14342 µs | 2.78× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 0.78 MB/s | 4.89 MB/s |
| squeeze | 1344 | 0.38 MB/s | 0.74 MB/s | 5.75 MB/s |
