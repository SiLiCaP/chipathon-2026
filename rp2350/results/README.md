# Speed test results

Each run of `rp2350/speedtest.py` saves a `.json` (every number and the full log) and a `.md` (the tables).
Summary and what the numbers mean: [docs/speed_results.md](../../docs/speed_results.md).

| Run | Board | Firmware | RP2350 | What | ML-KEM-512 hashing: software → with the chip |
|---|---|---|---|---|---|
| `20261125_185841` | Pico 2 W + Tang Nano rig, chip RTL | first speed-test build | 150 MHz | wire-speed sweep, bench at 18.75 MHz | 39.9 → 25.2 ms (1.58×) |
| `20261125_190139` | same | same | 150 MHz | bench at 10 MHz | 39.9 → 27.5 ms (1.45×) |
| `20261125_190142` | same | same | 150 MHz | bench at 5 MHz | 39.9 → 32.3 ms (1.24×) |
| `20261125_191601` | same | faster driver (4 clocks per check) | **250 MHz** | sweep to 125 MHz, skews 0-3; bench at 19.23 MHz | 23.9 → 12.8 ms (1.88×) |
| `20261125_191613` | same | faster driver | 150 MHz | bench at 18.75 MHz | 39.9 → 19.7 ms (**2.02×**) |

Add your own: run the script on any board with the firmware, and commit the two files it writes here.
