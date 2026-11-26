# Speed history: the firmware steps of Nov 25, 2026, measured against portable C

Historical. The current figures are in [speed_results.md](../speed_results.md), which compares the chip with
XKCP's assembly Keccak. Everything here except the last table row was measured against the portable C Keccak the
firmware used until `f3099cf` (about 5.6× slower than the assembly), so the speed-ups on this page are not
comparable with the current ones. Raw data: [speedtest_runs/](speedtest_runs/README.md).

## What changed in the firmware (all on the rig, chip at 18.75 MHz)

The first row was measured on the chip's RTL, the others on its gate netlist (the second row's 19.7 ms was measured
on both). The driver's work per chip clock dropped from about 47 to about 6
RP2350 cycles. Until the last row, the software side was portable C (39.8 ms for the exchange's hashing); the last
row compares with the assembly baseline.

| Step | RP2350 + chip | vs portable C |
|---|---|---|
| First measurement (framing mode 1, every newer command) | 25.2 ms | 1.58× |
| Idle runs written with `memset`, the quiet check four clocks at a time | 19.7 ms | 2.02× |
| Framing mode 0 with packets, on-chip padding and self-continuing bursts | 12.6 ms | 3.16× |
| Unpacking only the clocks that carry bytes, clearing only what was used, idle words fast-pathed on the rig (no result file was saved for this step) | 7.2 ms | 5.55× |
| Checking the samples while the chip is still running | 6.36 ms | 6.27× |
| The firmware running from SRAM instead of flash (`TT_COPY_TO_RAM`, now the default) | **5.95 ms** | 6.7× (1.37× vs the assembly baseline) |

Every step kept the checks exact:
- the fast quiet / valid check was fuzzed against the exact per-clock rule: 200,000 runs with injected faults, none
  missed, no false alarms;
- the PC selftest, fuzz and golden vectors pass;
- the driver's pin stream replays clock for clock through the RTL (`make -C test rp2350`);
- `selftest`, `fuzz 200` and `ci` pass on the rig.
