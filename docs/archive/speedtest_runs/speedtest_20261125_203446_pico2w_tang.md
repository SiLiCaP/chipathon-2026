# Hash chip speed test: pico2w_tang (2026-11-25T20:34:46)

Port `/dev/ttyACM0`. Firmware status at the start:

```
status
Hash chip harness on the Pico 2 W + Tang Nano 20K rig, RP2350 at 150 MHz
  transport : chip via PIO + DMA (Pico 2 W + Tang Nano 20K rig)
  chip clock: 1000000 Hz (PIO div 5, low 15 + high 15 cycles)
  skew 0, calls 1217, faults 0, clocks 664433, uo_out now 0x00
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
| keygen | 12326 µs | 1809 µs | 6.81× | 14,368 | 766 µs |
| encaps | 13721 µs | 2078 µs | 6.60× | 16,318 | 870 µs |
| decaps | 13720 µs | 2078 µs | 6.60× | 16,318 | 870 µs |
| **exchange** | **39767 µs** | **5966 µs** | **6.67×** | 47,004 | 2507 µs |

Errors: 0 (every chip result is compared with the software one).

Where the 5966 µs with the chip go: 2507 µs of chip clocks, 1552 µs reshuffling words onto the Pico rig's split pins (none on the demo board), 105 µs of other transport work (DMA setup, splitting the samples), 1802 µs in the driver (building and checking the pin words).

### Three ways to drive the same chip

Same chip, same workload, same clock, every result checked. Framing mode 1 adds the `valid` and `busy` pins, so the RP2350 samples 16 bits per clock instead of 8.

| Driven | chip clocks | driver | transport | RP2350 + chip | vs software |
|---|---|---|---|---|---|
| newer commands, framing mode 0, SampleNTT on the RP2350 (the default) | 47,004 | 1802 µs | 4164 µs | 5966 µs | 6.67× |
| every newer command, framing mode 1 (SampleNTT on chip) | 48,152 | 7885 µs | 7270 µs | 15155 µs | 2.62× |
| the October 2026 way: host padding, a word per byte, a burst word per block | 46,888 | 3181 µs | 4262 µs | 7444 µs | 5.34× |

### Compute only: the chip against the C code doing the same work

Software: each piece of C code alone on the RP2350. Chip: its clocks for the same work, with CBD and SampleNTT on the chip (they run on the output stream and cost no clocks of their own). No transfers on either side.

| Work | C on the RP2350 | chip clocks | chip at this clock | chip at 50 MHz | speed-up at 50 MHz |
|---|---|---|---|---|---|
| Keccak-f[1600], one permutation | 410.5 µs | 368 | 19.6 µs | 7.4 µs | 56× |
| SamplePolyCBD eta = 3 (192 B) | 118.7 µs | 0 | – | – | in the output stream |
| SampleNTT, one polynomial | 42.9 µs | 0 | – | – | in the output stream |
| G = SHA3-512 (64 B) | 432.4 µs | 518 | 27.6 µs | 10.4 µs | 42× |
| H = SHA3-256 (800 B) | 2614.1 µs | 3,091 | 164.9 µs | 61.8 µs | 42× |
| J = SHAKE256 (800 B) | 2614.1 µs | 3,091 | 164.9 µs | 61.8 µs | 42× |
| PRF_3 + CBD, one polynomial | 971.6 µs | 1,004 | 53.5 µs | 20.1 µs | 48× |
| PRF_2 + CBD, one polynomial | 525.5 µs | 569 | 30.3 µs | 11.4 µs | 46× |
| XOF + SampleNTT, one polynomial | 1345.8 µs | 1,700 | 90.7 µs | 34.0 µs | 40× |
| **one ML-KEM-512 exchange (32 calls)** | 39756.3 µs | 43,736 | 2332.6 µs | 874.7 µs | 45× |

| Rate | bytes | software | with the chip | chip alone |
|---|---|---|---|---|
| absorb | 4096 | 0.30 MB/s | 2.26 MB/s | 4.89 MB/s |
| squeeze | 1344 | 0.39 MB/s | 2.77 MB/s | 5.75 MB/s |
