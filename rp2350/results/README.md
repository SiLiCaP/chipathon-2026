# Speed test results

Each run of `rp2350/speedtest.py` saves a `.json` (every number and the full log) and a `.md` (the tables).
Summary and what the numbers mean: [docs/speed_results.md](../../docs/speed_results.md).

Only runs of the current firmware are kept here (software baseline: XKCP's assembly Keccak). Runs of earlier
firmware, measured against portable C, are in
[docs/archive/speedtest_runs/](../../docs/archive/speedtest_runs/README.md).

| Run | Board | Firmware | RP2350 | What | ML-KEM-512 hashing: software → with the chip |
|---|---|---|---|---|---|
| `20261125_213448` | gate netlist | **software baseline now XKCP assembly + Kyber-reference CBD** | 150 MHz | sweep, compute only, all three ways | **compute only: one exchange 8.17 ms in software, 43,736 chip clocks (2.33 ms here, 3.5×; 0.87 ms at 50 MHz, 9.3×); whole: 5.95 ms (1.37×)** |
| `20261125_213454` | gate netlist | same | 250 MHz | same | compute only at 50 MHz 5.6×; whole 4.90 → 4.50 ms (1.09×) |
| `20261126_115636` | gate netlist | + `bench compute` checks every exchange result; the software chip flags the command rules | 150 MHz | sweep, compute only, all three ways | the same as `20261125_213448`: compute only 8.17 ms → 43,736 clocks (9× at 50 MHz); whole 8.17 → 5.95 ms (1.37×); 0 errors |

Add your own: run the script on any board with the firmware, and commit the two files it writes here.
