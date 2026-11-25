# Speed test results

Each run of `rp2350/speedtest.py` saves a `.json` (every number and the full log) and a `.md` (the tables).
Summary and what the numbers mean: [docs/speed_results.md](../../docs/speed_results.md).

| Run | Board | Firmware | RP2350 | What | ML-KEM-512 hashing: software → with the chip |
|---|---|---|---|---|---|
| `20261125_185841` | Pico 2 W + Tang Nano rig, chip RTL of `main` | first speed-test build | 150 MHz | wire-speed sweep, bench at 18.75 MHz | 39.9 → 25.2 ms (1.58×) |
| `20261125_190139` | same | same | 150 MHz | bench at 10 MHz | 39.9 → 27.5 ms (1.45×) |
| `20261125_190142` | same | same | 150 MHz | bench at 5 MHz | 39.9 → 32.3 ms (1.24×) |
| `20261125_191601` | same | faster driver (4 clocks per check) | **250 MHz** | sweep to 125 MHz, skews 0-3; bench at 19.23 MHz | 23.9 → 12.8 ms (1.88×) |
| `20261125_191613` | same | faster driver | 150 MHz | bench at 18.75 MHz | 39.9 → 19.7 ms (2.02×) |
| `20261125_194049` | **gate netlist** of `main` | faster driver + `bench legacy` | 150 MHz | sweep, bench, legacy | mode 1: 19.7 ms (2.02×); October way: 14.4 ms (2.78×) |
| `20261125_194054` | gate netlist | same | 250 MHz | same | mode 1: 12.8 ms (1.87×); October way: 9.6 ms (2.51×) |
| `20261125_194154` | gate netlist | + `bench mode0` | 150 MHz | sweep, all three ways | **mode 0: 39.9 → 12.6 ms (3.16×)**; mode 1 19.7; October 14.3 |
| `20261125_194159` | gate netlist | same | 250 MHz | same | mode 0: 24.0 → 8.5 ms (2.82×); mode 1 12.8; October 9.5 |
| `20261125_200715` | gate netlist | **final driver**: windows, quiet check 16 clocks at a time, checked while the chip runs; mode 0 the default | 150 MHz | sweep, all three ways | **mode 0: 39.9 → 6.36 ms (6.27×)**; October 7.79 (5.12×); mode 1 15.3 (2.61×) |
| `20261125_200720` | gate netlist | final driver | 250 MHz | same | mode 0: 23.9 → 4.71 ms (5.08×); October 5.59; mode 1 10.2 |
| `20261125_203328` | gate netlist | + `bench compute`, running from flash | 150 MHz | sweep, compute only, all three ways | compute only 46× (one exchange 40.2 ms → 43,736 clocks); whole 6.75 ms (5.96×) |
| `20261125_203334` | gate netlist | same | 250 MHz | same | compute only 28×; whole 4.97 ms (4.86×) |
| `20261125_203446` | gate netlist | **final: running from SRAM** | 150 MHz | sweep, compute only, all three ways | **compute only: one exchange 39.76 ms in C, 43,736 chip clocks (2.33 ms here, 0.87 ms at 50 MHz, 46×); whole: 5.97 ms (6.67×)** |
| `20261125_203452` | gate netlist | final | 250 MHz | same | compute only 27×; whole 23.9 → 4.50 ms (5.30×) |

Add your own: run the script on any board with the firmware, and commit the two files it writes here.
