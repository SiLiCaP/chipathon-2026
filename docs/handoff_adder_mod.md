# Handoff: adder_mod + testbench

## State
- `src/adder_mod.v`: `ks_adder` (Kogge-Stone) and `adder_mod` (r = (a+b) mod N, a,b < N). Combinational. Generate label `bit` renamed `bitl` (SV keyword broke `-g2012`).
- `test/tb_adder_mod.v`: standalone bench. Edge cases, exhaustive 200x200 (W=8, N=200), 256-entry stream through in/out regs, watchdog. VCD dumps all signals.
- Run: in the `ttdev` container, `cd test && source /ttsetup/venv/bin/activate && make adder_mod` -> `RESULT: PASS`.
- View: `gtkwave tb_adder_mod.vcd -S tb_adder_mod_all.tcl`. From a snap-VSCode terminal, first unset `LD_LIBRARY_PATH GTK_PATH GTK_EXE_PREFIX GIO_MODULE_DIR GTK_IM_MODULE_FILE`.
- Cycle result: 256 entries -> 258 cycles (2 from the bench's own registers; the adder has 0 latency).

## Open items
1. Failing run still exits 0 (`$finish`); use `$fatal` so `make` fails.
2. Cycle/throughput print is partly hard-coded (10 ns, "1 entry/cycle"); the 258 count includes one extra sampling edge.
3. Bench uses W=8, N=200. Kyber needs W=12, N=3329 (reviewer reports it passes exhaustively in Verilator; not run in this repo).
4. `adder_mod.v` is not in `info.yaml` `source_files` or Makefile `PROJECT_SOURCES`; nothing instantiates it.
5. `make adder_mod` needs cocotb (container) and must run from `test/`.
6. `ks_adder` invalid for W=1; no check that a,b < N.
7. Two parallel KS adders is ~2x area; consider Brent-Kung.

## Efficiency review notes (estimates, generic-gate counts via Yosys; no STA)
8. Area (only matters once instantiated; W=12, N=3329): replacing both `ks_adder` with `+` cuts ~260 -> ~171 gates (-34%). A single adder plus constant subtract (`s=a+b; t=s-N`) gives ~141 gates (-46%), at logic depth 11 -> 19. Ripple is ~100 gates, depth 25. Clock period in `src/config.json` is 20 ns. Keep KS only if adder_mod shares a stage with the multiplier.
9. Waveform: the VCD is ~9 MB because it covers the 40k-vector exhaustive loop. Use `$dumpoff` after `$dumpvars` and `$dumpon` before the stream phase (~99 KB), or dump FST.
10. Exhaustive loop does not scale to W=12 (~11M vectors, ~11-14 min, ~3 GB VCD, extrapolated). Check only i<=j, or use Verilator.
11. `test/tb_adder_mod_all.tcl` adds ~177 constant `parameter` rows (genvar `i`/`l`, `W`, `N`, `L`); filter them out.
12. `make clean` does not remove `sim_build/tb_adder_mod` or the VCD.

## Review status
Four review agents (line-by-line, altitude, simplification, language-pitfall) died on the session limit; their results were not collected.
